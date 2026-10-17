# SPDX-FileCopyrightText: © 2024 Tiny Tapeout
# SPDX-License-Identifier: Apache-2.0

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, RisingEdge


def pack(a_prime, b_prime):
    """The 3 bytes byte_serializer sends for the pair (a', b'): {a', b'} cut into bytes, lowest first, so b' goes first."""
    word = (a_prime << 12) | b_prime
    return [word & 0xFF, (word >> 8) & 0xFF, (word >> 16) & 0xFF]


def set_input(dut, value, sel_b):
    """Put a 12-bit value on the input pins and pick its register (uio_in[4]: 0 = a, 1 = b)."""
    dut.ui_in.value = value & 0xFF
    dut.uio_in.value = (sel_b << 4) | (value >> 8)


async def read_byte(dut):
    await RisingEdge(dut.clk)  # uo_out changes on the rising edge
    await FallingEdge(dut.clk)  # read it half a clock later, once it has settled
    return int(dut.uo_out.value)


async def read_frame(dut):
    return [await read_byte(dut) for _ in range(3)]


async def load_and_read_frame(dut, a, b):
    """Load a then b into the input registers while reading one frame.

    a' and b' follow a and b one clock later, so the pair is in place
    before the next frame starts and goes out in that frame.
    """
    set_input(dut, a, sel_b=0)
    frame = [await read_byte(dut)]
    set_input(dut, b, sel_b=1)
    frame += [await read_byte(dut) for _ in range(2)]
    return frame


@cocotb.test()
async def test_project(dut):
    dut._log.info("Start")

    # Set the clock period to 10 us (100 KHz)
    clock = Clock(dut.clk, 10, unit="us")
    cocotb.start_soon(clock.start())

    # Reset
    dut._log.info("Reset")
    dut.ena.value = 1
    dut.ui_in.value = 0
    dut.uio_in.value = 0
    dut.rst_n.value = 0
    await ClockCycles(dut.clk, 10)
    dut.rst_n.value = 1

    dut._log.info("Test project behavior")

    # Data_in comes from the input pins: ui_in = bits [7:0], uio_in[3:0] = bits [11:8].
    # uio_in[4] picks which input register it loads: 0 = a, 1 = b.
    # a' = a and b' = b (one clock later) until the NTT core computes them.
    # The first clock edge after reset sends byte 1, and a frame is 3 clocks.

    # The first frame is the reset value of a' and b'
    assert await load_and_read_frame(dut, 0x456, 0xABC) == [0x00, 0x00, 0x00]

    # Each pair goes out in the frame after it is loaded
    assert await load_and_read_frame(dut, 0xDEF, 0x123) == [0xBC, 0x6A, 0x45]

    # a' changes in the middle of this frame, but all 3 bytes still belong to one pair
    assert await load_and_read_frame(dut, 0x312, 0x789) == pack(0xDEF, 0x123)
    assert await read_frame(dut) == pack(0x312, 0x789)

    # With uio_in[4] = 1 only b loads, so a keeps its value
    set_input(dut, 0x5A5, sel_b=1)
    assert await read_frame(dut) == pack(0x312, 0x789)  # b' catches up during this frame
    assert await read_frame(dut) == pack(0x312, 0x5A5)

    # Keep testing the module by changing the input values, waiting for
    # one or more clock cycles, and asserting the expected output values.
