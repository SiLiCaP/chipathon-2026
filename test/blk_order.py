# SPDX-License-Identifier: Apache-2.0
"""Block mode (NTT cache, src/ntt_blk.v): butterfly order inside a block.

A block holds 16 entries. Each pass runs 3 or 4 layers; every layer has 8 butterflies
on entry pairs (lo, lo | 1 << bit). The order of the 8 butterflies in each layer is
chosen so that no butterfly reads an entry less than DIST clocks after the butterfly
that writes it was read (DIST = 5: read, S1 register, 3 multiplier clocks): one
butterfly per clock, no stalls. The last layer's order is the order of the result
pairs at the pins. `python3 blk_order.py` prints the Verilog case table of ntt_blk.v.
"""

import itertools

DIST = 5
FA, FB, IA, IB = 0, 1, 2, 3                       # passes (header op)
PASS_NAMES = {FA: "forward A", FB: "forward B", IA: "inverse A", IB: "inverse B"}
PAIRED_BITS = {FA: (3, 2, 1, 0), FB: (3, 2, 1), IA: (1, 2, 3), IB: (0, 1, 2, 3)}
LENS = {FA: (128, 64, 32, 16), FB: (8, 4, 2), IA: (2, 4, 8), IB: (16, 32, 64, 128)}
STRIDED = {FA: True, FB: False, IA: False, IB: True}   # entry e = f[j + 16e] or f[16j + e]


def _layer(bit, seq):
    out = []
    for p in range(8):
        lo = sum(1 << b for i, b in enumerate(seq) if p >> i & 1)
        out.append((lo, lo | 1 << bit))
    return out


def _stall(orders):
    ready = {e: 0 for e in range(16)}
    worst = 0
    for layer, order in enumerate(orders):
        new = {}
        for p, (lo, hi) in enumerate(order):
            t = 8 * layer + p
            worst = max(worst, ready[lo] - t, ready[hi] - t)
            new[lo] = new[hi] = t + DIST
        ready.update(new)
    return worst


def _search(bits):
    opts = [[_layer(b, s) for s in itertools.permutations([x for x in range(4) if x != b])]
            for b in bits]
    for combo in itertools.product(*opts):
        if _stall(combo) == 0:
            return [list(o) for o in combo]
    raise AssertionError("no stall-free order")


ORDERS = {p: _search(bits) for p, bits in PAIRED_BITS.items()}


def layers(p):
    return len(PAIRED_BITS[p])


def entries(p, j):
    """Polynomial index of each of the 16 entries of block j in pass p."""
    return [j + 16 * e if STRIDED[p] else 16 * j + e for e in range(16)]


def out_pairs(p, j):
    """(index of r0, index of r1) for the 8 result pairs of block j, in pin order."""
    idx = entries(p, j)
    return [(idx[lo], idx[hi]) for lo, hi in ORDERS[p][-1]]


def verilog_table():
    rows = []
    for p in (FA, FB, IA, IB):
        for layer, order in enumerate(ORDERS[p]):
            for pos, (lo, _) in enumerate(order):
                rows.append(f"      7'b{p:02b}_{layer:02b}_{pos:03b}: lo = 4'd{lo};")
    return "\n".join(rows)


if __name__ == "__main__":
    print(verilog_table())
