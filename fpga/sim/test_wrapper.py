"""cocotb test of fpga/tangnano20k_top.v through its UART, using host/tt_emu.py encoders and
host/dut_models.py expectations. The baud rate is raised to CLK_HZ/8 so the sim runs fast."""
import copy
import os
import random
import sys

import cocotb
from cocotb.clock import Clock
from cocotb.queue import Queue
from cocotb.triggers import ClockCycles, FallingEdge, with_timeout

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))
import tt_emu as P            # noqa: E402
from dut_models import MODELS, run_model  # noqa: E402

CPB = 8                       # clocks per UART bit (BAUD = 27 MHz / 8, set in the Makefile)
TT_TOP = os.environ.get("TT_TOP", "tt_um_emu_selftest")


class Link:
    def __init__(self, dut):
        self.dut = dut
        self.rxq = Queue()
        dut.uart_rx.value = 1
        cocotb.start_soon(Clock(dut.clk, 37, unit="ns").start())
        cocotb.start_soon(self._receiver())

    async def _receiver(self):
        d = self.dut
        while True:
            await FallingEdge(d.uart_tx)
            await ClockCycles(d.clk, CPB // 2)        # middle of the start bit
            b = 0
            for i in range(8):
                await ClockCycles(d.clk, CPB)
                b |= int(d.uart_tx.value) << i
            await ClockCycles(d.clk, CPB)
            assert int(d.uart_tx.value) == 1, "framing error on uart_tx"
            self.rxq.put_nowait(b)

    async def send(self, data):
        d = self.dut
        for byte in data:
            for bit in [0] + [(byte >> i) & 1 for i in range(8)] + [1]:
                d.uart_rx.value = bit
                await ClockCycles(d.clk, CPB)

    async def xfer(self, cmd, op, n=0, raw=False):
        await self.send(cmd)
        resp = bytearray()
        for _ in range(P.resp_len(op, n)):
            resp.append(await with_timeout(self.rxq.get(), 50, "ms"))
        if raw:
            return bytes(resp)
        P.check_header(resp)
        return bytes(resp[2:])


async def setup(dut):
    link = Link(dut)
    await ClockCycles(dut.clk, 300)     # power-on reset
    return link


def peek(model, ui, uio):
    return copy.deepcopy(model).cycle(ui, uio)


@cocotb.test()
async def test_ping_and_errors(dut):
    link = await setup(dut)
    assert await link.xfer(P.enc_ping(), P.OP_PING) == P.MAGIC
    resp = await link.xfer(bytes([0x7F]), P.OP_RESET, raw=True)      # unknown opcode -> A5 01
    assert resp == bytes([0xA5, 0x01]), resp.hex()
    assert await link.xfer(P.enc_ping(), P.OP_PING) == P.MAGIC


@cocotb.test()
async def test_step_mode(dut):
    link = await setup(dut)
    rng = random.Random(1)
    model = MODELS[TT_TOP]()
    await link.xfer(P.enc_set_in(0, 0), P.OP_SET_IN)
    await link.xfer(P.enc_reset(5), P.OP_RESET)
    model.reset()
    for _ in range(200):
        ui, uio = rng.randrange(256), rng.randrange(256)
        await link.xfer(P.enc_set_in(ui, uio), P.OP_SET_IN)
        got0 = tuple(await link.xfer(P.enc_step(0), P.OP_STEP))       # sample, no clock
        assert got0 == peek(model, ui, uio), (got0, peek(model, ui, uio))
        n = rng.choice((1, 1, 3))
        for _ in range(n):
            model.cycle(ui, uio)
        got = tuple(await link.xfer(P.enc_step(n), P.OP_STEP))       # sample after n clocks
        assert got == peek(model, ui, uio), (got, peek(model, ui, uio))


async def run_check(link, words, div):
    model = MODELS[TT_TOP]()
    await link.xfer(P.enc_set_div(div), P.OP_SET_DIV)
    await link.xfer(P.enc_reset(4), P.OP_RESET)
    model.reset()
    await link.xfer(P.enc_load(0, words), P.OP_LOAD)
    await link.xfer(P.enc_run(len(words)), P.OP_RUN)
    caps = P.decode_caps(await link.xfer(P.enc_read_cap(0, len(words)), P.OP_READ_CAP, len(words)))
    exp = run_model(model, words)
    for i, (g, e) in enumerate(zip(caps, exp)):
        assert g == e, f"div {div}: first mismatch at cycle {i}: got {g}, expected {e}"
    assert len(caps) == len(exp)


@cocotb.test()
async def test_run_mode(dut):
    link = await setup(dut)
    rng = random.Random(2)
    for div in (1, 2, 5):
        await run_check(link, [rng.randrange(1 << 16) for _ in range(300)], div)


@cocotb.test()
async def test_run_full_ram(dut):
    """Acceptance: an 8192-word run matches the cycle-accurate model."""
    link = await setup(dut)
    rng = random.Random(3)
    await run_check(link, [rng.randrange(1 << 16) for _ in range(P.DEPTH)], 1)
