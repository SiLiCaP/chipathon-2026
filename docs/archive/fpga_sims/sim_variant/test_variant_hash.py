"""Pin-level hash test for any chip variant with a Keccak core (tools/tile_exp.py experiments), at any K:
every algorithm through the pins against hashlib, with host/hash_jobs.py building the words."""
import os
import sys

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "host"))
from hash_jobs import standard_jobs  # noqa: E402

K = int(os.environ.get("HASH_K", "8"))
CORE = os.environ.get("HASH_CORE", "ring")


@cocotb.test()
async def test_hash_all_algorithms(dut):
    cocotb.start_soon(Clock(dut.clk, 10, "ns").start())
    dut.ena.value = 1
    dut.ui_in.value = 0
    dut.uio_in.value = 0
    dut.rst_n.value = 0
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1
    for job in standard_jobs(K, CORE):
        words, sq = job.words()
        samples = []
        for w in words:
            await FallingEdge(dut.clk)
            v = dut.uo_out.value
            samples.append(int(v) if v.is_resolvable else 0)
            dut.ui_in.value = w & 0xFF
            dut.uio_in.value = w >> 8
        # samples[k]: uo_out while word k is on the pins, before the edge that takes it (the PIO convention)
        got = job.digest(samples, sq)
        assert got == job.expected(), f"{job.name} len {len(job.msg)} K={K}: {got.hex()[:16]} != {job.expected().hex()[:16]}"
    dut._log.info(f"K={K} {CORE}: {len(standard_jobs(K, CORE))} jobs, every digest equals hashlib")
