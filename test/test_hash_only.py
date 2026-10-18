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

HASH_K=16 make runs the K = 16 core. Under GATES=yes a reduced set runs because gate-level simulation is slow.
"""
import os

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge

from hash_jobs import CLEAR, HashJob, hash_only_jobs, hw, standard_jobs

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
        samples.append(int(v) if v.is_resolvable else -1)
        dut.ui_in.value = w & 0xFF
        dut.uio_in.value = w >> 8
    return samples


async def run_jobs(dut, jobs, what):
    for job in jobs:
        words, sq = job.words()
        samples = await stream(dut, words)
        due = {k + 2 for k in sq}
        loud = [k for k, s in enumerate(samples) if k not in due and s != 0]
        assert not loud, f"{what} {job.name} len {len(job.msg)}: uo_out nonzero at idle samples {loud[:5]}"
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
