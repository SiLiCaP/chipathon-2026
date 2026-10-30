# SPDX-License-Identifier: Apache-2.0
"""The RP2350 firmware's own pin stream through the hash-only chip's RTL, clock for clock.

The firmware's C driver (rp2350/core/hash_chip.c) builds the pin words; the PC build can record them together with
what its software chip (rp2350/core/hash_swchip.c) answered (`hash_host --sw --dump-words FILE selftest`). This test
plays that very stream into the RTL and requires uo_out to equal the software chip's on every clock, so the C
driver and its software model are proven against the real logic (the cocotb tests in test_hash_only.py check the
Python driver, hash_jobs.py, the same way).

  make -C test rp2350        builds rp2350/pc/hash_host, records `all` and `ci` (golden vectors) and replays them
  STREAM=file.hwd make ...   replay a recorded stream instead

K = 8 only (the C driver waits the K = 8 busy times). Skipped for HASH_K=16.
"""
import os
import struct

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge

from test_hash_only import core_k


def load(path):
    b = open(path, "rb").read()
    assert b[:4] == b"HWD1", "not a HWD1 stream"
    n = struct.unpack("<I", b[4:8])[0]
    words = struct.unpack("<%dH" % n, b[8:8 + 2 * n])
    return words, b[8 + 2 * n:8 + 3 * n]


@cocotb.test()
async def test_rp2350_stream(dut):
    if core_k(dut) != 8:
        dut._log.info("K != 8: the C driver is K = 8 only, skipped")
        return
    streams = [s for s in os.environ.get("STREAM", "").split(",") if s]
    assert streams, "set STREAM=file.hwd[,file2.hwd] (make -C test rp2350 does)"
    for path in streams:
        words, want = load(path)
        cocotb.start_soon(Clock(dut.clk, 20, unit="ns").start())
        dut.ena.value = 1
        dut.ui_in.value = 0
        dut.uio_in.value = 0
        dut.rst_n.value = 0
        await ClockCycles(dut.clk, 10)               # the C harness resets for 10 clocks, then streams
        dut.rst_n.value = 1
        bad = 0
        for k, w in enumerate(words):
            await FallingEdge(dut.clk)
            v = dut.uo_out.value
            got = int(v) if v.is_resolvable else -1
            if got != want[k]:
                bad += 1
                if bad <= 5:
                    dut._log.error(f"{path}: clock {k}: RTL uo_out {got:#04x}, software chip {want[k]:#04x}")
            dut.ui_in.value = w & 0xFF
            dut.uio_in.value = w >> 8
        assert not bad, f"{path}: {bad} of {len(words)} clocks differ between the RTL and the software chip"
        dut._log.info(f"{path}: {len(words)} clocks identical, {sum(1 for x in want if x)} nonzero output bytes")
