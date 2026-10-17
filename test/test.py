# SPDX-FileCopyrightText: © 2024 Tiny Tapeout
# SPDX-License-Identifier: Apache-2.0
"""Spec 5 checks of the I/O shell while ntt_core is a stub."""

import os
import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, RisingEdge

GL = os.environ.get("GATES") == "yes"


async def start(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="us").start())
    dut.ena.value = 1
    dut.ui_in.value = 0
    dut.uio_in.value = 0
    dut.rst_n.value = 0
    await ClockCycles(dut.clk, 5)
    await FallingEdge(dut.clk)
    dut.rst_n.value = 1


@cocotb.test()
async def test_shell(dut):
    """Every uio is an input; with the stub core uo_out stays 0x00."""
    await start(dut)
    rng = random.Random(1)
    for _ in range(200):
        await FallingEdge(dut.clk)
        dut.ui_in.value = rng.randrange(256)
        dut.uio_in.value = rng.randrange(256)
        await RisingEdge(dut.clk)
        assert int(dut.uio_oe.value) == 0 and int(dut.uio_out.value) == 0
        assert int(dut.uo_out.value) == 0


@cocotb.test(skip=GL)
async def test_capture(dut):
    """io_in_capture: each _q output equals the pins one clock earlier, and sof_q
    is high only when valid and sof were both high (Spec 5)."""
    await start(dut)
    cap = dut.user_project.u_in
    rng = random.Random(2)
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
