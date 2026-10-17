# SPDX-License-Identifier: Apache-2.0
"""Host-side job building, shared by the pin-level and the core-level tests.

A job is sent as units (a butterfly, a basemul piece, a scale pair).
Each unit occupies one period of clocks, its coefficients on fixed offsets and
valid low in between (docs/schedule.md). A chip object runs the clock words
and turns what comes back into result pairs:

    chip.run(words, tail)            -> raw per-clock samples
    chip.pairs(op, starts, samples)  -> [(r0, r1), ...], checks the timing
"""

import blk_order as B
import ntt_ref as ref
import timing as T

OP_FWD, OP_INV, OP_BM, OP_SC = 0, 1, 2, 3
OP_NAMES = {OP_FWD: "forward", OP_INV: "inverse", OP_BM: "basemul", OP_SC: "scale"}

PERIOD = {OP_FWD: T.P_F, OP_INV: T.P_I, OP_BM: T.P_B, OP_SC: T.P_S}
OFFSETS = {OP_FWD: T.OFFS_F, OP_INV: T.OFFS_I, OP_BM: T.OFFS_B, OP_SC: T.OFFS_S}
DELAY = {OP_FWD: T.D_F, OP_INV: T.D_I, OP_BM: T.D_B, OP_SC: T.D_S}

IDLE = (0, 0, 0, 0)  # (coeff, valid, sof, op)


def stream(op, units, sof):
    """Clock words for a list of units. Returns (words, unit start indices)."""
    period, offs = PERIOD[op], OFFSETS[op]
    words, starts = [], []
    for u, unit in enumerate(units):
        starts.append(len(words))
        slot = dict(zip(offs, unit))
        for o in range(period):
            if o in slot:
                first = sof and u == 0 and o == 0
                words.append((slot[o], 1, int(first), op))
            else:
                words.append(IDLE)
    return words, starts


def pair_starts(op, starts):
    """Clock of the first input of each result pair (one pair per unit)."""
    return starts


def scale_units(cs):
    """Scale inputs as units: pairs (c, c')."""
    return [(cs[i], cs[i + 1]) for i in range(0, len(cs), 2)]


def rand_poly(rng):
    return [rng.randrange(ref.Q) for _ in range(256)]


def pin_word(coeff, valid, sof, op):
    """(ui_in, uio_in) for one clock."""
    return coeff & 0xFF, ((coeff >> 8) & 0xF) | (valid << 4) | (sof << 5) | (op << 6)


# ---- Jobs ---------------------------------------------------------------------

def layer_units(f, layer, inverse):
    idx = ref.ntt_layer_units(f, layer, inverse)
    return idx, [(f[jl], f[j]) for j, jl in idx]   # (b, a)


async def run_units(chip, op, units, sof):
    """Send units, wait until all their results are back, return the pairs."""
    words, starts = stream(op, units, sof)
    samples = await chip.run(words, tail=chip.tail(op))
    return chip.pairs(op, starts, samples)


async def ntt_job(chip, f, inverse=False, check_layers=True):
    """One NTT job: all 7 layers, layer by layer, sof on the first coefficient.
    The host waits for each layer's results before sending the next layer."""
    op = OP_INV if inverse else OP_FWD
    step = ref.intt_layer if inverse else ref.ntt_layer
    for layer in range(7):
        idx, units = layer_units(f, layer, inverse)
        pairs = await run_units(chip, op, units, sof=(layer == 0))
        expect = step(f, layer)
        g = list(f)
        for (j, jl), (r0, r1) in zip(idx, pairs):
            g[j], g[jl] = r0, r1
        if check_layers:
            assert g == expect, f"{OP_NAMES[op]} layer {layer} differs from the reference"
        f = g
    return f


async def basemul_job(chip, f, g):
    units = [tuple(ref.basemul_inputs(f, g)[4 * i:4 * i + 4]) for i in range(128)]
    pairs = await run_units(chip, OP_BM, units, sof=True)
    return [c for p in pairs for c in p]


async def scale_job(chip, f):
    pairs = await run_units(chip, OP_SC, scale_units(f), sof=True)
    return [c for p in pairs for c in p]


# ---- Block mode (NTT cache) ------------------------------------------------------



def blk_words(p, j, f):
    """Pin words of one block: the header (bits 11:8 = 0xE, op = pass, bits 3:0 = j), then 16 entries."""
    return [(0xE00 | j, 1, 0, p)] + [(f[i], 1, 0, 0) for i in B.entries(p, j)]


def blk_stream(blocks, gaps=None):
    """Words for a list of blocks (p, j, f), each header T.blk_spacing clocks after the one before
    (plus gaps[k] extra idle clocks before block k). Returns (words, header indices)."""
    words, heads = [], []
    for k, (p, j, f) in enumerate(blocks):
        start = len(words) + (gaps[k] if gaps else 0)
        if heads:
            start = max(start, heads[-1] + T.blk_spacing(blocks[k - 1][0]))
        words += [IDLE] * (start - len(words))
        heads.append(len(words))
        words += blk_words(p, j, f)
    return words, heads


def blk_tail(p):
    return T.blk_delay(p) + 3 * T.BLK_PAIRS


def blk_results(blocks, heads, samples, used=None):
    """{poly index: value} per block, read at the fixed delays."""
    out = []
    for (p, j, _), h in zip(blocks, heads):
        res = {}
        d = h + T.blk_delay(p)
        for m, (i0, i1) in enumerate(B.out_pairs(p, j)):
            b = samples[d + 3 * m:d + 3 * m + 3]
            if used is not None:
                used.update(range(d + 3 * m, d + 3 * m + 3))
            r0, r1 = ref.unpack_pair(*b)
            assert r0 < ref.Q and r1 < ref.Q, f"hardware fault: result {r0}, {r1} >= 3329"
            res[i0], res[i1] = r0, r1
        out.append(res)
    return out


async def blk_pass(chip, p, f, order=range(16)):
    """One pass (16 blocks) of polynomial f; returns the new polynomial."""
    blocks = [(p, j, f) for j in order]
    words, heads = blk_stream(blocks)
    samples = await chip.run(words, tail=blk_tail(p))
    used = set()
    g = list(f)
    for res in blk_results(blocks, heads, samples, used):
        for i, v in res.items():
            g[i] = v
    stray = [(k, v) for k, v in enumerate(samples) if v and k not in used]
    assert not stray, f"{B.PASS_NAMES[p]}: bytes outside the block results: {stray[:8]}"
    return g


async def blk_ntt(chip, f, inverse=False):
    """Forward (or inverse, before the scale) NTT in block mode: 2 passes of 16 blocks."""
    p1, p2 = (B.IA, B.IB) if inverse else (B.FA, B.FB)
    return await blk_pass(chip, p2, await blk_pass(chip, p1, f))
