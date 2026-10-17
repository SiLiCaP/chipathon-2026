# SPDX-License-Identifier: Apache-2.0
"""Reference model for the NTT core: FIPS 203 Algorithms 9, 10, 11 and 12, plus
the final multiply by 3303, ByteEncode12 packing and the ROM / gamma tables.

Two levels (Spec 1):
- the plain model (ntt, intt, basemul, scale): FIPS 203 maths, the truth;
- the chip-level model (chip_*): what the pins show. The multiplier uses three
  K-RED rounds, so every product carries a factor 13^3 = 2197. The chip stores
  ROM' = ROM * 2197^-1 and uses the scale constant 304 = 3303 * 2197^-2, so the
  NTT modes are plain, basemul carries 2197 and scale removes it.

This is a stand-in for the Pi owner's reference model. It is kept small and
readable on purpose: every hardware test compares against it.
"""

Q = 3329
SCALE = 3303      # 128^-1 mod 3329
KM = 2197         # factor every chip product carries (13^3, three K-RED rounds)
KM_INV = 944      # 2197^-1 mod 3329
SCALE_C = 304     # chip scale constant, 3303 * 2197^-2 mod 3329


def bitrev7(k):
    return int(f"{k:07b}"[::-1], 2)


# ROM[k] = 17^BitRev7(k) mod q, and gamma_i = 17^(2*BitRev7(i)+1) mod q
ZETA = [pow(17, bitrev7(k), Q) for k in range(128)]
GAMMA = [pow(17, 2 * bitrev7(i) + 1, Q) for i in range(128)]


# The scaled tables the chip stores / derives
ZETA_S = [z * KM_INV % Q for z in ZETA]
GAMMA_S = [g * KM_INV % Q for g in GAMMA]


def gamma_from_rom(i):
    """How the chip derives gamma: ROM[64 + (i >> 1)], negated for odd i."""
    z = ZETA[64 + (i >> 1)]
    return Q - z if i & 1 else z


def ntt_layer(f, layer):
    """One forward layer (len = 128 >> layer) of Algorithm 9. Returns a new list."""
    f = list(f)
    length = 128 >> layer
    k = 1 << layer
    for start in range(0, 256, 2 * length):
        zeta = ZETA[k]
        k += 1
        for j in range(start, start + length):
            t = zeta * f[j + length] % Q
            f[j + length] = (f[j] - t) % Q
            f[j] = (f[j] + t) % Q
    return f


def intt_layer(f, layer):
    """One inverse layer of Algorithm 10, without the final scale.
    layer 0 is len = 2, layer 6 is len = 128."""
    f = list(f)
    length = 2 << layer
    k = (256 >> (layer + 1)) - 1  # 127 for len 2, ... , 1 for len 128
    for start in range(0, 256, 2 * length):
        zeta = ZETA[k]
        k -= 1
        for j in range(start, start + length):
            t = f[j]
            f[j] = (t + f[j + length]) % Q
            f[j + length] = zeta * (f[j + length] - t) % Q
    return f


def ntt(f):
    for layer in range(7):
        f = ntt_layer(f, layer)
    return f


def intt(f):
    """Inverse NTT before the 3303 scale (the chip does the scale as its own job)."""
    for layer in range(7):
        f = intt_layer(f, layer)
    return f


def basemul(f, g):
    """Algorithm 11 (MultiplyNTTs) with Algorithm 12 (BaseCaseMultiply)."""
    h = [0] * 256
    for i in range(128):
        f0, f1, g0, g1 = f[2 * i], f[2 * i + 1], g[2 * i], g[2 * i + 1]
        h[2 * i] = (f0 * g0 + f1 * g1 % Q * GAMMA[i]) % Q
        h[2 * i + 1] = (f0 * g1 + f1 * g0) % Q
    return h


def scale(f):
    return [c * SCALE % Q for c in f]


def schoolbook(f, g):
    """Negacyclic product mod X^256 + 1, mod q."""
    h = [0] * 256
    for i in range(256):
        for j in range(256):
            k = i + j
            if k < 256:
                h[k] += f[i] * g[j]
            else:
                h[k - 256] -= f[i] * g[j]
    return [c % Q for c in h]


# ---- Chip-level model ---------------------------------------------------------

def kred(x):
    """K-RED: 13 * (x & 255) - (x >> 8), congruent to 13 * x (3329 = 13 * 2^8 + 1).
    Python's & and >> on negative ints match two's complement hardware."""
    return 13 * (x & 255) - (x >> 8)


def chip_mul(a, b):
    """modq_mul: three K-RED rounds and one correction -> 2197 * a * b mod q.
    Rounds 1 and 2 are merged on the bytes of C = a * b:
    t = 169 * C0 - 13 * C1 + C2 (= kred(kred(C))), then u = kred(t) lands in
    -169..3328, so only a negative result needs +q."""
    c = a * b
    t = 169 * (c & 255) - 13 * ((c >> 8) & 255) + (c >> 16)
    u = kred(t)
    return u + Q if u < 0 else u


def chip_ntt_layer(f, layer, inverse=False):
    """One layer as the chip computes it (zeta' from the scaled ROM)."""
    f = list(f)
    if inverse:
        length, k, step = 2 << layer, (256 >> (layer + 1)) - 1, -1
    else:
        length, k, step = 128 >> layer, 1 << layer, 1
    for start in range(0, 256, 2 * length):
        z = ZETA_S[k]
        k += step
        for j in range(start, start + length):
            b, a = f[j + length], f[j]
            if inverse:
                f[j], f[j + length] = (a + b) % Q, chip_mul((b - a) % Q, z)
            else:
                t = chip_mul(b, z)
                f[j], f[j + length] = (a + t) % Q, (a - t) % Q
    return f


def chip_basemul_piece(f0, f1, g0, g1, gamma_s):
    m1 = chip_mul(f1, g1)
    m2 = chip_mul(m1, gamma_s)
    m3 = chip_mul(f0, g0)
    m4 = chip_mul(f0, g1)
    m5 = chip_mul(f1, g0)
    return (m3 + m2) % Q, (m4 + m5) % Q


def chip_basemul(f, g):
    h = []
    for i in range(128):
        h += chip_basemul_piece(f[2 * i], f[2 * i + 1], g[2 * i], g[2 * i + 1], GAMMA_S[i])
    return h


def chip_scale(f):
    return [chip_mul(c, SCALE_C) for c in f]


def pack_pair(r0, r1):
    """ByteEncode12 of two coefficients: three bytes, little-endian bit order."""
    v = r0 | (r1 << 12)
    return [v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF]


def unpack_pair(b0, b1, b2):
    return b0 | ((b1 & 0x0F) << 8), (b1 >> 4) | (b2 << 4)


# ---- Job input/output order at the chip boundary -----------------------------
# Each job is a list of units. A unit is the coefficients the host sends for one
# butterfly / basemul piece / scale coefficient, and the results it gets back.

def ntt_layer_units(f, layer, inverse=False):
    """Butterfly order for one layer: (b, a) = (f[j+len], f[j]) with the indices.
    Returns [(j, j+len), ...] in send order."""
    length = (2 << layer) if inverse else (128 >> layer)
    out = []
    for start in range(0, 256, 2 * length):
        for j in range(start, start + length):
            out.append((j, j + length))
    return out


def basemul_inputs(f, g):
    seq = []
    for i in range(128):
        seq += [g[2 * i + 1], f[2 * i + 1], g[2 * i], f[2 * i]]
    return seq


def self_check():
    """Known answers from the specs. Raises AssertionError on mismatch."""
    assert (ZETA[0], ZETA[1], ZETA[2], ZETA[3], ZETA[64], ZETA[127]) == (1, 1729, 2580, 3289, 17, 2154)
    assert [GAMMA[i] for i in (0, 1, 2, 3, 126, 127)] == [17, 3312, 2761, 568, 2154, 1175]
    assert all(GAMMA[i] == gamma_from_rom(i) for i in range(128))
    assert all(z != 0 for z in ZETA)
    assert 128 * SCALE % Q == 1
    ramp = list(range(256))
    fr = ntt(ramp)
    assert fr[:8] == [2429, 2845, 425, 795, 1865, 1356, 624, 31]
    assert fr[248:] == [2883, 149, 2594, 3105, 2502, 2134, 2717, 2303]
    ir = intt(fr)
    assert ir[:4] == [0, 128, 256, 384]
    assert scale(ir) == ramp
    assert pack_pair(0x123, 0x456) == [0x23, 0x61, 0x45]
    assert pack_pair(3328, 3328) == [0x00, 0x0D, 0xD0]
    assert unpack_pair(0x23, 0x61, 0x45) == (0x123, 0x456)
    import random
    rng = random.Random(1)
    f = [rng.randrange(Q) for _ in range(256)]
    g = [rng.randrange(Q) for _ in range(256)]
    assert scale(intt(basemul(ntt(f), ntt(g)))) == schoolbook(f, g)

    # Chip-level model (Specs 1, 2, 3 after the K-RED change)
    assert KM == 13 ** 3 and KM * KM_INV % Q == 1 and SCALE_C == SCALE * KM_INV * KM_INV % Q
    assert (ZETA_S[0], ZETA_S[1], ZETA_S[2], ZETA_S[3], ZETA_S[64], ZETA_S[127]) == (944, 966, 2021, 2188, 2732, 2686)
    assert [GAMMA_S[i] for i in (0, 1, 2, 3, 126, 127)] == [2732, 597, 3106, 223, 2686, 643]
    assert all(GAMMA_S[i] == (Q - ZETA_S[64 + (i >> 1)] if i & 1 else ZETA_S[64 + (i >> 1)]) for i in range(128))
    for a, b, p in ((1, 1, 2197), (966, 2000, 2498), (3328, 3328, 2197), (3328, 2, 2264),
                    (1234, 2345, 350), (0, 3328, 0), (500, 304, 2023), (3328, 304, 1241)):
        assert chip_mul(a, b) == p, (a, b)
    assert all(chip_mul(a, b) == KM * a * b % Q for a in range(0, Q, 97) for b in range(Q))
    assert chip_basemul_piece(5, 7, 11, 13, 17 * KM_INV % Q) == (841, 2377)
    assert basemul([5, 7] + [0] * 254, [11, 13] + [0] * 254)[:2] == [1602, 142]
    assert chip_scale([2197, 500 * KM % Q, 3328 * KM % Q]) == [3303, 316, 26]
    t = chip_mul(2000, ZETA_S[1])
    assert (t, (1000 + t) % Q, (1000 - t) % Q) == (2498, 169, 1831)
    fh = f
    for layer in range(7):
        fh = chip_ntt_layer(fh, layer)
    assert fh == ntt(f)
    assert chip_basemul(f, g) == [KM * c % Q for c in basemul(f, g)]
    assert chip_scale(f) == [c * SCALE * KM_INV % Q for c in f]
    h = chip_basemul(ntt(f), ntt(g))
    for layer in range(7):
        h = chip_ntt_layer(h, layer, inverse=True)
    assert chip_scale(h) == schoolbook(f, g)


if __name__ == "__main__":
    self_check()
    print("reference model: all known answers match")
