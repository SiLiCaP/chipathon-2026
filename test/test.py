# SPDX-License-Identifier: Apache-2.0
"""Chip-level tests through the Tiny Tapeout pins, the way the RP2350 host
drives the chip: one input word and one uo_out sample per clock, results read
at the fixed delays from timing.py. Also used for the gate-level test (GATES=yes),
where a reduced set runs because gate-level simulation is slow."""

import os
import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, RisingEdge

import ntt_ref as ref
import timing as T
from ntt_host import (DELAY, IDLE, OP_BM, OP_FWD, OP_INV, OP_NAMES, OP_SC, basemul_job,
                      ntt_job, pair_starts, pin_word, rand_poly, run_units, scale_job, stream)

GL = os.environ.get("GATES") == "yes"


class PinChip:
    """Drives the TT pins. samples[k] is uo_out during the clock of word k."""

    def __init__(self, dut):
        self.dut = dut
        self.clocks = 0

    async def start(self):
        cocotb.start_soon(Clock(self.dut.clk, 10, unit="us").start())
        self.dut.ena.value = 1
        await self.reset()

    async def reset(self, cycles=5):
        self.dut.ui_in.value = 0
        self.dut.uio_in.value = 0
        self.dut.rst_n.value = 0
        await ClockCycles(self.dut.clk, cycles)
        await FallingEdge(self.dut.clk)
        self.dut.rst_n.value = 1

    def tail(self, op):
        return DELAY[op] + 3

    async def run(self, words, tail=0):
        samples = []
        for w in list(words) + [IDLE] * tail:
            await FallingEdge(self.dut.clk)
            ui, uio = pin_word(*w)
            self.dut.ui_in.value = ui
            self.dut.uio_in.value = uio
            await RisingEdge(self.dut.clk)
            samples.append(int(self.dut.uo_out.value))
            self.clocks += 1
        return samples

    def pairs(self, op, starts, samples, strict=True):
        """Bytes at start + D, +1, +2 of each pair; every other sample must be 0."""
        d = DELAY[op]
        used, pairs = set(), []
        for p in pair_starts(op, starts):
            b = samples[p + d:p + d + 3]
            used.update(range(p + d, p + d + 3))
            r0, r1 = ref.unpack_pair(*b)
            assert r0 < ref.Q and r1 < ref.Q, f"hardware fault: result {r0}, {r1} >= 3329"
            pairs.append((r0, r1))
        if strict:
            stray = [(k, v) for k, v in enumerate(samples) if v and k not in used]
            assert not stray, f"{OP_NAMES[op]}: bytes outside the scheduled clocks: {stray[:8]}"
        return pairs

    def check_asserts(self):
        """RTL only: the ntt_core simulation assertions (-DNTT_ASSERT)."""
        if not GL:
            errs = int(self.dut.user_project.u_core.assert_errors.value)
            assert errs == 0, f"{errs} ntt_core assertion(s) failed, see the log"


async def new_chip(dut):
    chip = PinChip(dut)
    await chip.start()
    return chip


@cocotb.test()
async def test_reference_model(dut):
    """The reference model reproduces every known answer in the specs."""
    ref.self_check()


@cocotb.test()
async def test_idle(dut):
    """After reset, and with coefficients but no sof, every output is 0x00; uio are inputs."""
    chip = await new_chip(dut)
    rng = random.Random(7)
    words = [(rng.randrange(ref.Q), rng.randrange(2), 0, rng.randrange(4)) for _ in range(60)]
    samples = await chip.run(words, tail=30)
    assert not any(samples)
    assert int(dut.uio_oe.value) == 0 and int(dut.uio_out.value) == 0
    chip.check_asserts()


@cocotb.test()
async def test_forward_ntt(dut):
    """Forward NTT, all 7 layers in one job, against the reference model."""
    chip = await new_chip(dut)
    rng = random.Random(1)
    f = list(range(256)) if GL else rand_poly(rng)
    assert await ntt_job(chip, f) == ref.ntt(f)
    chip.check_asserts()


@cocotb.test(skip=GL)
async def test_inverse_ntt(dut):
    """Inverse NTT (before the scale), all 7 layers, against the reference model."""
    chip = await new_chip(dut)
    rng = random.Random(2)
    f = rand_poly(rng)
    assert await ntt_job(chip, f, inverse=True) == ref.intt(f)
    chip.check_asserts()


@cocotb.test()
async def test_basemul(dut):
    chip = await new_chip(dut)
    rng = random.Random(3)
    f, g = rand_poly(rng), rand_poly(rng)
    # basemul bytes carry 2197 x the plain basemul (K-RED), see Spec 3
    assert await basemul_job(chip, f, g) == ref.chip_basemul(f, g)
    assert ref.chip_basemul(f, g) == [ref.KM * c % ref.Q for c in ref.basemul(f, g)]
    chip.check_asserts()


@cocotb.test()
async def test_scale(dut):
    chip = await new_chip(dut)
    rng = random.Random(4)
    f = rand_poly(rng)
    assert await scale_job(chip, f) == ref.chip_scale(f)
    chip.check_asserts()


@cocotb.test()
async def test_edge_values(dut):
    """All zeros, all 3328, and a single 1, through scale and basemul."""
    chip = await new_chip(dut)
    for f in ([0] * 256, [3328] * 256, [1 if i == 37 else 0 for i in range(256)]):
        assert await scale_job(chip, f) == ref.chip_scale(f)
        assert await basemul_job(chip, f, f[::-1]) == ref.chip_basemul(f, f[::-1])
    chip.check_asserts()


@cocotb.test(skip=GL)
async def test_full_flow(dut):
    """The demo: forward NTT of f and g, basemul, inverse NTT, scale ==
    schoolbook negacyclic product mod 3329. Prints the clocks per step."""
    chip = await new_chip(dut)
    rng = random.Random(5)
    f, g = rand_poly(rng), rand_poly(rng)
    steps = []

    async def timed(name, coro):
        c0 = chip.clocks
        r = await coro
        steps.append((name, chip.clocks - c0))
        return r

    fh = await timed("forward f", ntt_job(chip, f, check_layers=False))
    gh = await timed("forward g", ntt_job(chip, g, check_layers=False))
    hh = await timed("basemul", basemul_job(chip, fh, gh))
    h = await timed("inverse", ntt_job(chip, hh, inverse=True, check_layers=False))
    h = await timed("scale", scale_job(chip, h))
    for name, n in steps:
        dut._log.info(f"{name:10s} {n:6d} clocks")
    dut._log.info(f"total      {chip.clocks:6d} clocks (streaming estimate 10752 + layer waits)")
    assert h == ref.schoolbook(f, g)
    chip.check_asserts()


@cocotb.test()
async def test_output_delay(dut):
    """Measure D per mode: first nonzero byte after one unit, compare with timing.py."""
    chip = await new_chip(dut)
    cases = {
        OP_FWD: [(5, 7)],                      # b, a -> a' = 7 + 5*zeta[1] != 0
        OP_INV: [(5, 7)],                      # r0 = a + b = 12
        OP_BM: [(1, 2, 3, 4)],                 # c0 = f0 g0 + ...
        OP_SC: [(7,), (7,)],                   # r0 = 7 * 304 * 2197 = 1300
    }
    for op, units in cases.items():
        words, starts = stream(op, units, sof=True)
        samples = await chip.run(words, tail=40)
        first = next(k for k, v in enumerate(samples) if v)
        dut._log.info(f"D {OP_NAMES[op]:8s} = {first} (timing.py: {DELAY[op]})")
        assert first - starts[0] == DELAY[op]
        # a one-unit job stays running; the next case's sof cancels it
    chip.check_asserts()


@cocotb.test()
async def test_rates(dut):
    """Back-to-back units give results every P clocks; NTT bytes have no gaps."""
    chip = await new_chip(dut)
    rng = random.Random(6)
    f = rand_poly(rng)
    units = [(f[2 * i], f[2 * i + 1]) for i in range(20)]
    words, starts = stream(OP_FWD, units, sof=True)
    samples = await chip.run(words, tail=T.D_F + 3)
    assert all(b - a == T.P_F for a, b in zip(starts, starts[1:]))
    pairs = chip.pairs(OP_FWD, starts, samples)
    assert pairs == unit_results(OP_FWD, units, lambda n: 1)
    words, starts = stream(OP_BM, [tuple(f[4 * i:4 * i + 4]) for i in range(4)], sof=True)
    samples = await chip.run(words, tail=T.D_B + 3)
    assert all(b - a == T.P_B for a, b in zip(starts, starts[1:]))
    units = [tuple(f[4 * i:4 * i + 4]) for i in range(4)]
    assert chip.pairs(OP_BM, starts, samples) == unit_results(OP_BM, units, None)
    chip.check_asserts()


def unit_results(op, units, layer_zeta_index):
    """Expected pairs for the first units of a job (layer 0 for NTT jobs), from
    the chip-level model (NTT plain, basemul x2197, scale c * 304 * 2197)."""
    q = ref.Q
    out = []
    if op == OP_SC:
        cs = ref.chip_scale([u[0] for u in units])
        return [(cs[i], cs[i + 1]) for i in range(0, len(cs), 2)]
    for n, u in enumerate(units):
        if op == OP_FWD:
            b, a = u
            t = ref.ZETA[layer_zeta_index(n)] * b % q
            out.append(((a + t) % q, (a - t) % q))
        elif op == OP_INV:
            b, a = u
            out.append(((a + b) % q, ref.ZETA[layer_zeta_index(n)] * (b - a) % q))
        else:
            g1, f1, g0, f0 = u
            out.append(ref.chip_basemul_piece(f0, f1, g0, g1, ref.GAMMA_S[n]))
    return out


@cocotb.test()
async def test_cancel(dut):
    """A new sof mid-job cancels it, at several points and for every pair of
    modes: no stale bytes after the flush, and the new job is correct."""
    chip = await new_chip(dut)
    rng = random.Random(8)
    f, g = rand_poly(rng), rand_poly(rng)
    old_jobs = {
        OP_FWD: stream(OP_FWD, [(f[i + 128], f[i]) for i in range(128)], sof=True)[0],
        OP_INV: stream(OP_INV, [(f[i + 2 * (i // 2) + 2], f[i + 2 * (i // 2)]) for i in range(128)], sof=True)[0],
        OP_BM: stream(OP_BM, [tuple(f[4 * i:4 * i + 4]) for i in range(64)], sof=True)[0],
        OP_SC: stream(OP_SC, [(c,) for c in f], sof=True)[0],
    }
    zeta_index = {OP_FWD: lambda n: 1, OP_INV: lambda n: 127 - n // 2, OP_BM: None, OP_SC: None}
    points = [1, 2, 3, 4, 9, 17, 100] if not GL else [4, 17]
    for old_op, old in old_jobs.items():
        for cut in points:
            new_op = (old_op + cut) % 4
            if new_op in (OP_FWD, OP_INV):
                units = [(g[2 * i + 1], g[2 * i]) for i in range(8)]
            elif new_op == OP_BM:
                units = [tuple(g[4 * i:4 * i + 4]) for i in range(4)]
            else:
                units = [(c,) for c in g[:16]]
            words = old[:cut]
            new_words, new_starts = stream(new_op, units, sof=True)
            samples = await chip.run(words + new_words, tail=DELAY[new_op] + 3)
            # The flush reaches the packer in the clock after the sof word, so
            # old bytes may still show up to sample cut + 1, never after.
            skip = cut + 2
            starts = [s + cut - skip for s in new_starts]
            pairs = chip.pairs(new_op, starts, samples[skip:])
            assert pairs == unit_results(new_op, units, zeta_index[new_op]), \
                f"cancel {OP_NAMES[old_op]} at {cut} -> {OP_NAMES[new_op]}"
    chip.check_asserts()


@cocotb.test()
async def test_reset_mid_job(dut):
    """Reset in the middle of a job leaves no stale bytes; the next job works."""
    chip = await new_chip(dut)
    rng = random.Random(9)
    f = rand_poly(rng)
    words, _ = stream(OP_FWD, [(f[i + 128], f[i]) for i in range(128)], sof=True)
    await chip.run(words[:50])
    await chip.reset(cycles=3)
    samples = await chip.run([IDLE] * 30)
    assert not any(samples), "stale bytes after reset"
    assert await scale_job(chip, f) == ref.chip_scale(f)
    chip.check_asserts()


@cocotb.test()
async def test_extra_inputs_ignored(dut):
    """Coefficients after the last expected one are ignored until the next sof."""
    chip = await new_chip(dut)
    rng = random.Random(10)
    f = rand_poly(rng)
    words, starts = stream(OP_SC, [(c,) for c in f], sof=True)
    extra, _ = stream(OP_SC, [(c,) for c in f[:20]], sof=False)
    samples = await chip.run(words + extra, tail=T.D_S + 3)
    pairs = chip.pairs(OP_SC, starts, samples)
    assert [x for p in pairs for x in p] == ref.chip_scale(f)
    chip.check_asserts()


@cocotb.test(skip=GL)
async def test_capture_pins(dut):
    """io_in_capture: each _q output equals the pins one clock earlier, and sof_q
    is high only when valid and sof were both high (Spec 5)."""
    chip = await new_chip(dut)
    cap = dut.user_project.u_in
    # Random pin words break the host protocol on purpose (coefficients >= 3329,
    # sof at any time), so the core's protocol assertions are expected to fire
    # here; their count is put back afterwards.
    errs_before = int(dut.user_project.u_core.assert_errors.value)
    rng = random.Random(11)
    prev = None
    for _ in range(2000):
        await FallingEdge(dut.clk)
        if prev is not None:
            ui, uio = prev
            assert int(cap.coeff_q.value) == ((uio & 0xF) << 8) | ui
            assert int(cap.valid_q.value) == (uio >> 4) & 1
            assert int(cap.sof_q.value) == ((uio >> 5) & (uio >> 4) & 1)
            assert int(cap.op_q.value) == uio >> 6
        prev = (rng.randrange(256), rng.randrange(256))
        dut.ui_in.value, dut.uio_in.value = prev
    # pin examples from Spec 5: inverse NTT sof word 1000, then 2000 with valid only
    for ui, uio, coeff, valid, sof, op in ((0xE8, 0x73, 1000, 1, 1, 1), (0xD0, 0x17, 2000, 1, 0, 0)):
        await FallingEdge(dut.clk)
        dut.ui_in.value, dut.uio_in.value = ui, uio
        await FallingEdge(dut.clk)
        assert (int(cap.coeff_q.value), int(cap.valid_q.value), int(cap.sof_q.value), int(cap.op_q.value)) == \
            (coeff, valid, sof, op)
    await chip.reset()
    dut.user_project.u_core.assert_errors.value = errs_before
