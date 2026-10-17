# SPDX-License-Identifier: Apache-2.0
"""Block mode (NTT cache) through the pins, against the reference model.

A forward NTT is pass forward A (len 128..16) and pass forward B (len 8..2), 16 blocks
each; an inverse NTT is inverse A (len 2..8) then inverse B (len 16..128). Under
GATES=yes only the forward NTT and the mixed-pass test run (gate-level sim is slow).
"""

import os
import random

import cocotb

import blk_order as B
import ntt_ref as ref
from ntt_host import (basemul_job, blk_ntt, blk_pass, blk_results, blk_stream, blk_tail,
                      ntt_job, rand_poly, scale_job)
from test import new_chip

GL = os.environ.get("GATES") == "yes"


def after_layers(f, layers, inverse=False):
    step = ref.intt_layer if inverse else ref.ntt_layer
    for layer in layers:
        f = step(f, layer)
    return f


@cocotb.test()
async def test_blk_forward(dut):
    """Forward NTT in block mode: pass A equals layers 0..3, pass B the rest."""
    chip = await new_chip(dut)
    rng = random.Random(31)
    f = rand_poly(rng)
    c0 = chip.clocks
    g = await blk_pass(chip, B.FA, f)
    assert g == after_layers(f, range(4)), "forward pass A differs from layers 0..3"
    h = await blk_pass(chip, B.FB, g)
    assert h == ref.ntt(f), "block-mode forward NTT differs"
    dut._log.info(f"block-mode forward NTT: {chip.clocks - c0} clocks (2 passes, host waits between them)")
    chip.check_asserts()


@cocotb.test(skip=GL)
async def test_blk_inverse(dut):
    chip = await new_chip(dut)
    rng = random.Random(32)
    f = rand_poly(rng)
    g = await blk_pass(chip, B.IA, f)
    assert g == after_layers(f, range(3), inverse=True), "inverse pass A differs from layers 0..2"
    assert await blk_pass(chip, B.IB, g) == ref.intt(f), "block-mode inverse NTT differs"
    chip.check_asserts()


@cocotb.test()
async def test_blk_mixed(dut):
    """Blocks of all four passes of different polynomials interleaved at the minimum
    spacing (any order of 3- and 4-layer blocks), then with random extra gaps."""
    chip = await new_chip(dut)
    rng = random.Random(33)
    polys = [rand_poly(rng) for _ in range(4)]
    n = 6 if GL else 24
    for gaps in (None, [rng.choice((0, 0, 1, 5, 30)) for _ in range(n)]):
        blocks = [(p, rng.randrange(16), polys[p]) for p in (rng.randrange(4) for _ in range(n))]
        words, heads = blk_stream(blocks, gaps)
        samples = await chip.run(words, tail=blk_tail(blocks[-1][0]))
        used = set()
        for (p, j, f), res in zip(blocks, blk_results(blocks, heads, samples, used)):
            inv = p in (B.IA, B.IB)
            first = {B.FA: range(4), B.FB: range(4, 7), B.IA: range(3), B.IB: range(3, 7)}[p]
            want = after_layers(f, first, inv)   # that pass's layers applied to f
            for i, v in res.items():
                assert v == want[i], f"{B.PASS_NAMES[p]} block {j}: f[{i}] = {v}, want {want[i]}"
        stray = [(k, v) for k, v in enumerate(samples) if v and k not in used]
        assert not stray, f"mixed blocks: stray bytes {stray[:8]}"
    chip.check_asserts()


@cocotb.test(skip=GL)
async def test_blk_full_flow(dut):
    """Block-mode forward NTT of f and g, streaming basemul, block-mode inverse, streaming
    scale: the schoolbook product. Then a streaming NTT job still works."""
    chip = await new_chip(dut)
    rng = random.Random(34)
    f, g = rand_poly(rng), rand_poly(rng)
    c0 = chip.clocks
    fh = await blk_ntt(chip, f)
    gh = await blk_ntt(chip, g)
    h = await basemul_job(chip, fh, gh)
    h = await blk_ntt(chip, h, inverse=True)
    h = await scale_job(chip, h)
    assert h == ref.schoolbook(f, g), "block-mode full flow differs from schoolbook"
    dut._log.info(f"block-mode polynomial multiply: {chip.clocks - c0} clocks")
    assert await ntt_job(chip, f, check_layers=False) == ref.ntt(f)
    chip.check_asserts()

