# SPDX-FileCopyrightText: © 2024 Tiny Tapeout
# SPDX-License-Identifier: Apache-2.0

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, RisingEdge


def pack(a_prime, b_prime):
    """The 3 bytes byte_serializer sends for the pair (a', b'): {a', b'} cut into bytes, lowest first, so b' goes first."""
    word = (a_prime << 12) | b_prime
    return [word & 0xFF, (word >> 8) & 0xFF, (word >> 16) & 0xFF]


def set_input(dut, value):
    """Put a 12-bit value on the input pins: ui_in = bits [7:0], uio_in[3:0] = bits [11:8]."""
    dut.ui_in.value = value & 0xFF
    dut.uio_in.value = value >> 8


async def read_byte(dut):
    await RisingEdge(dut.clk)  # uo_out changes on the rising edge
    await FallingEdge(dut.clk)  # read it half a clock later, once it has settled
    return int(dut.uo_out.value)


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

    # One 12-bit word goes in per clock while BUSY (uio_out[4]) is low.
    # The words go into b, a, b, a, ... so words 1 and 2 are the pair (b, a),
    # words 3 and 4 the next pair, and so on. Once a pair is in, BUSY goes high
    # and the inputs are ignored until byte 3 of that pair is out on uo_out.
    # a' = a and b' = b until the NTT core computes them.
    # uo_out is 0 while no bytes are being sent.
    assert int(dut.uio_oe.value) == 0b0001_0000

    pairs = [(0x123, 0xDEF), (0x789, 0x312), (0x5A5, 0xA5A), (0xFED, 0x0C1)]  # (b, a)
    words = [w for pair in pairs for w in pair]
    out = []
    busy = []
    sent = 0
    for _ in range(6 * len(pairs) + 3):
        busy.append((int(dut.uio_out.value) >> 4) & 1)
        if busy[-1] or sent == len(words):
            set_input(dut, 0xBAD)  # must be ignored
        else:
            set_input(dut, words[sent])
            sent += 1
        out.append(await read_byte(dut))

    # Every word was taken
    assert sent == len(words)

    # Each pair takes 6 clocks: load b, load a, a' and b' load, then bytes 1, 2, 3.
    # BUSY is high from loading a until byte 3 goes out, so the next b loads on the
    # clock edge right after byte 3.
    assert busy[:12] == [0, 0, 1, 1, 1, 1] * 2
    assert out[:6] == [0x00, 0x00, 0x00, 0x23, 0xF1, 0xDE]  # pack(a'=0xDEF, b'=0x123)

    # Every pair goes out once, in order, with 0 in between
    for n, (b, a) in enumerate(pairs):
        assert out[6 * n : 6 * n + 6] == [0, 0, 0] + pack(a_prime=a, b_prime=b)
    assert out[6 * len(pairs) :] == [0, 0, 0]
