# SPDX-License-Identifier: Apache-2.0
"""Whole-core tests: ntt_core with modq_mul, modq_addsub and modq_acc, driven
at its own ports. Checks every mode against the reference model, the
pair_valid offset of every unit (the core part of D), the rates, cancel /
flush, reset, and the ROM against the reference tables."""

import os
import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, RisingEdge, Timer

import ntt_ref as ref
import timing as T
from ntt_host import (IDLE, OP_BM, OP_FWD, OP_INV, OP_NAMES, OP_SC, basemul_job, layer_units,
                      ntt_job, pair_starts, rand_poly, scale_units, scale_job, stream)

PRE_LAT = int(os.environ.get("PRE_LAT", "1"))
POST_LAT = int(os.environ.get("POST_LAT", "0"))
L_MUL = T.L_MUL
PV = T.core_timing(L_MUL, PRE_LAT, POST_LAT)


class CoreChip:
    """samples[k] = (pair_valid, r0, r1, flush) in the clock of word k."""

    def __init__(self, dut):
        self.dut = dut
        self.clocks = 0

    async def start(self):
        cocotb.start_soon(Clock(self.dut.clk, 10, unit="ns").start())
        await self.reset()

    async def reset(self, cycles=4):
        self.drive(IDLE)
        self.dut.rst_n.value = 0
        await ClockCycles(self.dut.clk, cycles)
        await FallingEdge(self.dut.clk)
        self.dut.rst_n.value = 1

    def drive(self, w):
        c, v, s, op = w
        self.dut.coeff_q.value = c
        self.dut.valid_q.value = v
        self.dut.sof_q.value = s & v
        self.dut.op_q.value = op

    def tail(self, op):
        return PV[op] + 2

    async def run(self, words, tail=0):
        samples = []
        for w in list(words) + [IDLE] * tail:
            await FallingEdge(self.dut.clk)
            self.drive(w)
            await RisingEdge(self.dut.clk)
            d = self.dut
            samples.append((int(d.pair_valid.value), int(d.r0.value), int(d.r1.value), int(d.flush.value)))
            self.clocks += 1
        return samples

    def pairs(self, op, starts, samples):
        """pair_valid exactly PV[op] clocks after each pair's first input, nowhere else."""
        expect = [p + PV[op] for p in pair_starts(op, starts)]
        got = [k for k, s in enumerate(samples) if s[0]]
        assert got == expect, f"{OP_NAMES[op]}: pair_valid at {got[:6]}..., expected {expect[:6]}..."
        assert all(b - a >= 3 for a, b in zip(got, got[1:]))
        return [(samples[k][1], samples[k][2]) for k in got]

    def check_asserts(self):
        errs = int(self.dut.dut.assert_errors.value)
        assert errs == 0, f"{errs} ntt_core assertion(s) failed, see the log"


async def new_chip(dut):
    chip = CoreChip(dut)
    await chip.start()
    dut._log.info(f"L_MUL={L_MUL} PRE_LAT={PRE_LAT} POST_LAT={POST_LAT} pair_valid offsets {PV}")
    return chip


@cocotb.test()
async def test_zeta_rom(dut):
    """All 128 zeta_rom entries equal the scaled table ROM'[k] = 17^BitRev7(k) * 2197^-1."""
    for k in range(128):
        dut.rom_addr.value = k
        await Timer(1, unit="ns")
        assert int(dut.rom_data.value) == ref.ZETA_S[k], f"ROM'[{k}]"


@cocotb.test()
async def test_twiddle_select(dut):
    """The zeta' / gamma' register for every unit of every mode: forward ROM'[1..127]
    and inverse ROM'[127..1] in zeta_r at the unit's start, gamma' for all 128 pieces
    in gamma_r one clock later (the negation for odd pieces happens there)."""
    chip = await new_chip(dut)
    rng = random.Random(11)
    f = rand_poly(rng)
    for op in (OP_FWD, OP_INV, OP_BM):
        if op == OP_BM:
            units = [tuple(f[4 * (i % 64):4 * (i % 64) + 4]) for i in range(128)]
            expect = [ref.GAMMA_S[i] for i in range(128)]
        else:
            units = [(f[i % 256], f[(i * 7) % 256]) for i in range(896)]
            expect = []
            for n in range(896):
                layer, bf = divmod(n, 128)
                if op == OP_FWD:
                    expect.append(ref.ZETA_S[(128 + bf) >> (7 - layer)])
                else:
                    expect.append(ref.ZETA_S[(256 >> (layer + 1)) - 1 - (bf >> (layer + 1))])
        words, starts = stream(op, units, sof=True)
        seen = []
        for k, w in enumerate(words):
            await FallingEdge(dut.clk)
            chip.drive(w)
            if op != OP_BM and k - 1 in starts:     # zeta_r: loaded at the unit's first input
                seen.append(int(dut.dut.zeta_r.value))
            if op == OP_BM and k - 2 in starts:     # gamma_r: one clock after zeta_r
                seen.append(int(dut.dut.gamma_r.value))
        chip.drive(IDLE)
        assert seen == expect, f"{OP_NAMES[op]} twiddles differ"
        await chip.reset()
    chip.check_asserts()


@cocotb.test()
async def test_forward_inverse(dut):
    """Forward then inverse are plain FIPS 203; scale alone gives c * 3303 * 2197^-1,
    so forward, inverse, scale returns f * 2197^-1 (ramp and random)."""
    chip = await new_chip(dut)
    rng = random.Random(12)
    for f in (list(range(256)), rand_poly(rng)):
        fh = await ntt_job(chip, f)
        assert fh == ref.ntt(f)
        fi = await ntt_job(chip, fh, inverse=True)
        assert fi == ref.intt(fh)
        assert await scale_job(chip, fi) == [c * ref.KM_INV % ref.Q for c in f]
    chip.check_asserts()


@cocotb.test()
async def test_full_flow(dut):
    """Forward f, forward g, basemul, inverse, scale == schoolbook product."""
    chip = await new_chip(dut)
    rng = random.Random(13)
    f, g = rand_poly(rng), rand_poly(rng)
    fh = await ntt_job(chip, f, check_layers=False)
    gh = await ntt_job(chip, g, check_layers=False)
    hh = await basemul_job(chip, fh, gh)
    assert hh == ref.chip_basemul(fh, gh)
    h = await ntt_job(chip, hh, inverse=True, check_layers=False)
    assert await scale_job(chip, h) == ref.schoolbook(f, g)
    chip.check_asserts()


@cocotb.test()
async def test_cancel_and_flush(dut):
    """sof while running: flush for exactly that clock, no stale pair_valid,
    the new job correct. sof while idle: no flush."""
    chip = await new_chip(dut)
    rng = random.Random(14)
    f = rand_poly(rng)
    scale_words, scale_starts = stream(OP_SC, scale_units(f), sof=True)
    s = await chip.run(scale_words, tail=chip.tail(OP_SC))
    assert not any(x[3] for x in s), "flush with no job running"
    old_units = {
        OP_FWD: [(f[2 * i + 1], f[2 * i]) for i in range(64)],
        OP_INV: [(f[2 * i + 1], f[2 * i]) for i in range(64)],
        OP_BM: [tuple(f[4 * i:4 * i + 4]) for i in range(64)],
        OP_SC: scale_units(f[:64]),
    }
    for old_op, units in old_units.items():
        old, _ = stream(old_op, units, sof=True)
        for cut in (1, 2, 3, 5, 8, 16, 40):
            s = await chip.run(old[:cut] + scale_words, tail=chip.tail(OP_SC))
            flushes = [k for k, x in enumerate(s) if x[3]]
            assert flushes == [cut], f"flush at {flushes}, expected [{cut}]"
            # pairs of the old job before the cancel clock are legitimate
            after = [x if k >= cut else (0, 0, 0, 0) for k, x in enumerate(s)]
            pairs = chip.pairs(OP_SC, [p + cut for p in scale_starts], after)
            assert [c for p in pairs for c in p] == ref.chip_scale(f), f"{OP_NAMES[old_op]} cut {cut}"
    chip.check_asserts()


@cocotb.test()
async def test_idle_and_gaps(dut):
    """Valid low changes nothing: extra idle clocks between layers and before a
    job; coefficients without sof are ignored."""
    chip = await new_chip(dut)
    rng = random.Random(15)
    junk = [(rng.randrange(ref.Q), 1, 0, rng.randrange(4)) for _ in range(30)]
    s = await chip.run(junk, tail=10)
    assert not any(x[0] for x in s)
    f = rand_poly(rng)
    # forward layer 0 then a long pause, then layer 1, in one job
    idx0, u0 = layer_units(f, 0, False)
    w0, st0 = stream(OP_FWD, u0, sof=True)
    s0 = await chip.run(w0, tail=200)
    g = list(f)
    for (j, jl), (r0, r1) in zip(idx0, chip.pairs(OP_FWD, st0, s0)):
        g[j], g[jl] = r0, r1
    assert g == ref.ntt_layer(f, 0)
    idx1, u1 = layer_units(g, 1, False)
    w1, st1 = stream(OP_FWD, u1, sof=False)
    s1 = await chip.run(w1, tail=chip.tail(OP_FWD))
    h = list(g)
    for (j, jl), (r0, r1) in zip(idx1, chip.pairs(OP_FWD, st1, s1)):
        h[j], h[jl] = r0, r1
    assert h == ref.ntt_layer(g, 1)
    chip.check_asserts()


@cocotb.test()
async def test_reset_mid_job(dut):
    chip = await new_chip(dut)
    rng = random.Random(16)
    f = rand_poly(rng)
    w, _ = stream(OP_BM, [tuple(f[4 * i:4 * i + 4]) for i in range(64)], sof=True)
    await chip.run(w[:100])
    await chip.reset(cycles=2)
    s = await chip.run([IDLE] * 40)
    assert not any(x[0] for x in s)
    assert int(dut.dut.u_ctrl.busy.value) == 0
    assert await scale_job(chip, f) == ref.chip_scale(f)
    chip.check_asserts()
