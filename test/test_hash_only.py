# SPDX-License-Identifier: Apache-2.0
"""The hash-only chip through the Tiny Tapeout pins, against hashlib and FIPS 203 SamplePolyCBD.

hash_jobs.py builds every clock's pin word (it is the same file the host tools use), so these tests check
the exact streams the RP2350 and the FPGA rig send. A hash word is valid = 1 with uio[3:0] = 0xF, the command
on {uio[5], uio[7:6]} and the data byte on ui_in; the host has no busy flag, so it waits the fixed busy times
(K = 8: CLEAR 8, PERMUTE 368 clocks; K = 16: CLEAR 4, PERMUTE 172). samples[k] is uo_out while word k is on
the pins, before the clock edge that takes it; the byte asked for by word k is samples[k + 2].

  test_hash_algos        SHA3-256/512, SHAKE128/256 at the rate edges, with and without SKIP, byte squeeze
  test_hash_bursts       the same algorithms with BURST squeeze (8n bytes from one word)
  test_cbd               ML-KEM PRF_eta (SHAKE256) through BURST_CBD, eta = 2 and 3, vs SamplePolyCBD
  test_quiet             uo_out is 0 whenever no byte is due (nothing leaks between commands)
  test_finish            FINISH: the chip pads and permutes, at every corner of the block
  test_absorb_n          ABSORB_N: packets of raw data bytes, whatever the uio pins say meanwhile
  test_mode              framing mode 1: no marker, uio[0] is the valid output; and back to mode 0
  test_sample_ntt        ML-KEM's XOF through SampleNTT, vs FIPS 203 SampleNTT + ByteEncode_12
  test_auto              one word per squeeze: the chip permutes between the blocks itself; the busy pin

In mode 1 a sample also carries the valid and busy pins (uio_out[1:0] while uio_oe is set) as hash_jobs.VALID
and hash_jobs.BUSY.

HASH_K=16 make runs the K = 16 core. Under GATES=yes a reduced set runs because gate-level simulation is slow.
"""
import os

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge

from hash_jobs import (BUSY, CLEAR, EXT, EXT_MODE, PERMUTE, VALID, HashJob, auto_jobs, finish_jobs, hash_only_jobs,
                       hw, ntt_jobs, raw_jobs, standard_jobs)

GL = os.environ.get("GATES") == "yes"


def core_k(dut):
    if GL:
        return int(os.environ.get("HASH_K") or 8)
    return int(dut.user_project.HASH_K.value)


async def reset(dut):
    cocotb.start_soon(Clock(dut.clk, 20, unit="ns").start())
    dut.ena.value = 1
    dut.ui_in.value = 0
    dut.uio_in.value = 0
    dut.rst_n.value = 0
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1


async def stream(dut, words):
    samples = []
    for w in words:
        await FallingEdge(dut.clk)
        v = dut.uo_out.value
        s = int(v) if v.is_resolvable else -1
        if s >= 0:                                      # mode 1: the chip's valid and busy pins
            s |= (int(dut.uio_oe.value) & int(dut.uio_out.value) & 3) << 8
        samples.append(s)
        dut.ui_in.value = w & 0xFF
        dut.uio_in.value = w >> 8
    return samples


async def run_jobs(dut, jobs, what):
    for job in jobs:
        words, sq = job.words()
        samples = await stream(dut, words)
        due = {k + 2 for k in sq}
        loud = [k for k, s in enumerate(samples) if k not in due and s & (VALID | 0xFF)]
        assert not loud, f"{what} {job.name} len {len(job.msg)}: uo_out nonzero at idle samples {loud[:5]}"
        if job.mode and not job.ntt:                    # the valid pin marks exactly the clocks with a byte
            flagged = {k for k, s in enumerate(samples) if s & VALID}
            want = {k for i, k in enumerate(sorted(due)) if i % 3} if job.packed else due    # packed: 2 of 3
            assert flagged == want, f"{what} {job.name} len {len(job.msg)}: valid pin differs from the due clocks"
        if job.ntt and job.auto:                        # busy from the word to the end, then free; all bytes in it
            k0 = sq[0]
            busy = [k for k, s in enumerate(samples) if k > k0 and s & BUSY]     # at k0: still the PERMUTE before
            assert busy and busy == list(range(k0 + 2, busy[-1] + 1)), f"{what}: busy is not one stretch"
            last = max(k for k, s in enumerate(samples) if s & VALID)
            assert last <= busy[-1] < len(samples) - 1, f"{what}: busy ends at {busy[-1]}, last byte at {last}"
        if job.ntt:                                     # a byte only with valid, and valid only in the windows
            bare = [k for k, s in enumerate(samples) if s & 0xFF and not s & VALID]
            assert not bare, f"{what} {job.name}: uo_out nonzero without valid at samples {bare[:5]}"
        got, exp = job.collect(samples, sq), job.expected()
        assert got == exp, f"{what} {job.name} len {len(job.msg)} eta {job.cbd_eta}: output mismatch"
    dut._log.info(f"{what}: {len(jobs)} jobs ok")


def pick(jobs):
    return jobs[::4] if GL else jobs


@cocotb.test()
async def test_hash_algos(dut):
    await reset(dut)
    await run_jobs(dut, pick(standard_jobs(core_k(dut))), "byte squeeze")


@cocotb.test()
async def test_hash_bursts(dut):
    await reset(dut)
    await run_jobs(dut, pick([j for j in hash_only_jobs(core_k(dut)) if not j.cbd_eta]), "burst squeeze")


@cocotb.test()
async def test_cbd(dut):
    await reset(dut)
    await run_jobs(dut, pick([j for j in hash_only_jobs(core_k(dut)) if j.cbd_eta]), "BURST_CBD")


@cocotb.test()
async def test_quiet(dut):
    """Hash words during a burst are ignored; after the burst the chip takes commands again."""
    await reset(dut)
    K = core_k(dut)
    job = HashJob("sha3_256", b"abc", K, burst=True)
    words, sq = job.words()
    # overwrite the idle clocks of the burst with CLEAR words: all must be ignored
    start = sq[0]
    for k in range(start + 1, start + len(sq)):
        words[k] = hw(CLEAR)
    samples = await stream(dut, words)
    assert job.collect(samples, sq) == job.expected(), "a hash word during a burst disturbed it"
    # the chip still works afterwards
    await run_jobs(dut, [HashJob("shake_128", b"x" * 40, K, burst=True)], "after burst")


@cocotb.test()
async def test_finish(dut):
    await reset(dut)
    await run_jobs(dut, pick(finish_jobs(core_k(dut))), "FINISH")


@cocotb.test()
async def test_absorb_n(dut):
    await reset(dut)
    await run_jobs(dut, pick(raw_jobs(core_k(dut))), "ABSORB_N")


async def set_mode(dut, m, now):
    """Send MODE m in the framing of mode `now`; the chip's uio[1:0] drivers follow."""
    await stream(dut, [hw(EXT, EXT_MODE | m, now), 0, 0, 0])
    assert int(dut.uio_oe.value) == 3 * m, f"uio_oe is {int(dut.uio_oe.value):#04x} in mode {m}"


@cocotb.test()
async def test_mode(dut):
    """Mode 1 drops the marker and drives the valid pin; mode 0 is the reset state and comes back on MODE 0."""
    await reset(dut)
    K = core_k(dut)
    assert int(dut.uio_oe.value) == 0, "every uio pin is an input after reset"
    # mode 0: a word without the marker is not a command (the CLEAR would wipe the absorbed message)
    job = HashJob("sha3_256", b"abc", K, burst=True)
    words, sq = job.words()
    at = words.index(hw(2))                             # the PERMUTE that ends the absorb
    words[at:at] = [hw(CLEAR, 0, mode=1)]
    samples = await stream(dut, words)
    assert job.collect(samples, [k + 1 for k in sq]) == job.expected(), "mode 0 took a word without the marker"
    await set_mode(dut, 1, now=0)
    # the busy pin: high for the PERMUTE time, from 2 clocks after the word
    samples = await stream(dut, [hw(PERMUTE, 0, 1)] + [0] * (job.perm_busy + 6))
    assert [k for k, s in enumerate(samples) if s & BUSY] == list(range(2, 2 + job.perm_busy)), "busy pin and PERMUTE"
    jobs = [HashJob("sha3_512", b"mode one", K, mode=1),
            HashJob("shake_128", bytes(range(170)), K, mode=1, burst=True, finish=True, raw=True),
            HashJob("shake_256", bytes(33), K, mode=1, cbd_eta=3)]
    await run_jobs(dut, jobs, "mode 1")
    await run_jobs(dut, [HashJob("sha3_256", b"marker", K)], "mode 1, words with the marker")
    await set_mode(dut, 0, now=1)
    await run_jobs(dut, pick(standard_jobs(K))[:2], "back in mode 0")


@cocotb.test()
async def test_sample_ntt(dut):
    await reset(dut)
    await set_mode(dut, 1, now=0)
    jobs = ntt_jobs(core_k(dut))
    await run_jobs(dut, jobs[:2] + jobs[-2:] if GL else jobs, "SampleNTT")


@cocotb.test()
async def test_auto(dut):
    """Blocks that follow by themselves, in both framing modes; in mode 1 with SampleNTT and the busy pin."""
    await reset(dut)
    K = core_k(dut)
    await run_jobs(dut, pick(auto_jobs(K)), "auto, mode 0")
    await set_mode(dut, 1, now=0)
    jobs = auto_jobs(K, mode=1)
    await run_jobs(dut, jobs[3:5] if GL else jobs, "auto, mode 1")
