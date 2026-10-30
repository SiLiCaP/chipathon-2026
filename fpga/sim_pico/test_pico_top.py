"""The rig top as the Pico drives it: inputs change while clk is low, uo_out is sampled just before the
rising edge (the PIO program tt_lockstep.pio and docs/host_timing.md). A polynomial multiply recorded on
the cycle model (sim/chip_a.py, team timing) must come back byte for byte."""
import os
import random
import sys

import cocotb
from cocotb.triggers import Timer

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path[:0] = [os.path.join(ROOT, "sim"), os.path.join(ROOT, "model")]
from chip_a import ChipA  # noqa: E402
from host import Host  # noqa: E402
from mlkem_ntt_model import polymul_schoolbook  # noqa: E402


async def clock(dut, word):
    dut.tt_clk.value = 0
    dut.tt_ui.value = word & 0xFF
    dut.tt_uio.value = word >> 8
    await Timer(10, "ns")
    v = dut.tt_uo.value
    got = int(v) if v.is_resolvable else -1      # X only before the first edge in reset
    dut.tt_clk.value = 1
    await Timer(10, "ns")
    return got


@cocotb.test()
async def test_polymul_through_the_pins(dut):
    rng = random.Random(3)
    f = [rng.randrange(3329) for _ in range(256)]
    g = [rng.randrange(3329) for _ in range(256)]
    host = Host(ChipA(), record=True)
    h = host.scale(host.invntt(host.basemul(host.ntt(f), host.ntt(g))))
    assert h == polymul_schoolbook(f, g)
    stim, exp = host.trace.stim, host.trace.expect
    dut.tt_clk.value = 0
    dut.tt_rst_n.value = 0
    for _ in range(10):
        await clock(dut, 0)
    dut.tt_rst_n.value = 1
    bad = []
    for k, (w, e) in enumerate(zip(stim, exp)):
        got = await clock(dut, w)
        if got != e:
            bad.append((k, got, e))
    assert not bad, f"{len(bad)} of {len(stim)} bytes differ, first {bad[:3]}"
    assert int(dut.led.value) >> 5 & 1, "LED5 (a uio pin driven) must stay off"
    dut._log.info(f"{len(stim)} clocks, every uo_out byte as modelled")
