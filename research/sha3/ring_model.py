"""Structural model of the ring Keccak core (k = 8 interleave, 8-bit rings).

State: 25 lanes x 8 sub-rings x 8 flip-flops. Sub-ring p of lane L holds the lane bits z = 8*i + p.
Position 0 of a ring is its exit; a clocked ring drops position 0 into the closure logic and takes the
closure's output at position 7. Every flip-flop is a plain shift stage (no enable, no mux); rings are
only clocked or not (one clock gate per group).

The model mirrors the RTL step by step, so it doubles as the generator's source of truth.
"""
import hashlib

K = 8           # sub-rings per lane (interleave), = bits per I/O byte
M = 64 // K     # flip-flops per sub-ring

# rho offsets R[x][y], lane index L = x + 5*y
R = [[0, 36, 3, 41, 18], [1, 44, 10, 45, 2], [62, 6, 43, 15, 61], [28, 55, 25, 21, 56], [27, 20, 39, 8, 14]]
RC = [0x0000000000000001, 0x0000000000008082, 0x800000000000808A, 0x8000000080008000,
      0x000000000000808B, 0x0000000080000001, 0x8000000080008081, 0x8000000000008009,
      0x000000000000008A, 0x0000000000000088, 0x0000000080008009, 0x000000008000000A,
      0x000000008000808B, 0x800000000000008B, 0x8000000000008089, 0x8000000000008003,
      0x8000000000008002, 0x8000000000000080, 0x000000000000800A, 0x800000008000000A,
      0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008]


def lane(x, y):
    return (x % 5) + 5 * (y % 5)


def rho(L):
    return R[L % 5][L // 5]


# pi: B[x'][y'] = A[x][y] with x' = y, y' = 2x + 3y.  PI_SRC[dest] = source lane
PI_SRC = [0] * 25
for x in range(5):
    for y in range(5):
        PI_SRC[lane(y, 2 * x + 3 * y)] = lane(x, y)


def sigma(L, q):
    """physical sub-ring of lane L that holds post-rho logical sub-ring q during a round pass"""
    return (q - rho(L)) % K


def rho_steps(L, b):
    """extra clocks physical sub-ring b of lane L takes in the rho phase"""
    q = (b + rho(L)) % K
    return ((q - rho(L) - b) // K) % M


# ---------- reference Keccak-f[1600] on 25 x 64-bit lanes ----------
def rot(v, n):
    n %= 64
    return ((v << n) | (v >> (64 - n))) & (2**64 - 1) if n else v


def keccak_f_ref(A):
    A = list(A)
    for rd in range(24):
        C = [A[x] ^ A[x + 5] ^ A[x + 10] ^ A[x + 15] ^ A[x + 20] for x in range(5)]
        D = [C[(x - 1) % 5] ^ rot(C[(x + 1) % 5], 1) for x in range(5)]
        A = [A[L] ^ D[L % 5] for L in range(25)]
        B = [0] * 25
        for L in range(25):
            B[[d for d in range(25) if PI_SRC[d] == L][0]] = rot(A[L], rho(L))
        A = [B[L] ^ (~B[lane(L % 5 + 1, L // 5)] & B[lane(L % 5 + 2, L // 5)]) for L in range(25)]
        A[0] ^= RC[rd]
    return A


# ---------- structural ring model ----------
class Rings:
    def __init__(self):
        self.r = [[[0] * M for _ in range(K)] for _ in range(25)]
        self.clocks = 0

    def load(self, lanes):
        for L in range(25):
            for p in range(K):
                self.r[L][p] = [(lanes[L] >> (K * i + p)) & 1 for i in range(M)]

    def lanes(self):
        return [sum(self.r[L][p][i] << (K * i + p) for p in range(K) for i in range(M)) for L in range(25)]

    def exits(self):
        return [[self.r[L][p][0] for p in range(K)] for L in range(25)]

    def clock(self, closure, enable=None):
        """closure[L][p] = bit entering each ring; enable[L][p] = ring clocked (default all)"""
        for L in range(25):
            for p in range(K):
                if enable is None or enable[L][p]:
                    self.r[L][p] = self.r[L][p][1:] + [closure[L][p]]
        self.clocks += 1


def theta_d(C, Cprev):
    """D[x][p] for one row of K slices; C[x][p] parities of this clock, Cprev[x] parity of slice z-1 of p=0"""
    D = [[0] * K for _ in range(5)]
    for x in range(5):
        for p in range(K):
            cm1 = C[(x + 1) % 5][p - 1] if p else Cprev[(x + 1) % 5]
            D[x][p] = C[(x - 1) % 5][p] ^ cm1
    return D


def parity(S):
    """S[L][p] -> C[x][p]"""
    return [[S[x][p] ^ S[x + 5][p] ^ S[x + 10][p] ^ S[x + 15][p] ^ S[x + 20][p] for p in range(K)] for x in range(5)]


def chi_iota(B, rc_bits):
    """B[L][p] post-pi bits of K slices -> E; rc_bits[p] the iota bit for lane 0"""
    E = [[0] * K for _ in range(25)]
    for L in range(25):
        x, y = L % 5, L // 5
        for p in range(K):
            E[L][p] = B[L][p] ^ ((1 - B[lane(x + 1, y)][p]) & B[lane(x + 2, y)][p])
    for p in range(K):
        E[0][p] ^= rc_bits[p]
    return E


def post_pi(view):
    """view(L, q) -> post-rho bit of lane L logical sub-ring q; returns post-pi B[dest][q]"""
    return [[view(PI_SRC[D], q) for q in range(K)] for D in range(25)]


def permute(rg: Rings):
    # theta-only pass for round 0 (canonical frame)
    wrap = [[rg.r[L][p][M - 1] for p in range(K)] for L in range(25)]      # slice 63 lives at position M-1
    cprev = [c[K - 1] for c in parity(wrap)]
    for i in range(M):
        ex = rg.exits()
        C = parity(ex)
        D = theta_d(C, cprev)
        cprev = [c[K - 1] for c in C]
        rg.clock([[ex[L][p] ^ D[L % 5][p] for p in range(K)] for L in range(25)])
    for rd in range(24):
        # rho phase: gated rotation, E clocks
        steps = [[rho_steps(L, b) for b in range(K)] for L in range(25)]
        for t in range(max(max(s) for s in steps)):
            ex = rg.exits()
            rg.clock(ex, [[steps[L][b] > t for b in range(K)] for L in range(25)])
        last = rd == 23
        # wrap: post-rho slice 63 = logical q = K-1 at position M-1 of its physical ring
        if not last:
            Bw = post_pi(lambda L, q: rg.r[L][sigma(L, q)][M - 1])
            Ew = chi_iota(Bw, [(RC[rd] >> (K * (M - 1) + p)) & 1 for p in range(K)])
            cprev = [c[K - 1] for c in parity(Ew)]
        for i in range(M):
            ex = rg.exits()
            B = post_pi(lambda L, q: ex[L][sigma(L, q)])
            E = chi_iota(B, [(RC[rd] >> (K * i + p)) & 1 for p in range(K)])
            if not last:
                C = parity(E)
                D = theta_d(C, cprev)
                cprev = [c[K - 1] for c in C]
                E = [[E[L][p] ^ D[L % 5][p] for p in range(K)] for L in range(25)]
            rg.clock(E)


if __name__ == "__main__":
    import random
    random.seed(1)
    for _ in range(5):
        A = [random.getrandbits(64) for _ in range(25)]
        rg = Rings()
        rg.load(A)
        permute(rg)
        assert rg.lanes() == keccak_f_ref(A), "mismatch"
    print("ring model == reference, clocks per permutation:", rg.clocks)
    # reference sanity vs hashlib: SHA3-256 of empty string through keccak_f_ref
    st = [0] * 25
    st[0] ^= 0x06
    st[16] ^= 0x80 << 56
    st = keccak_f_ref(st)
    dig = b"".join(st[i].to_bytes(8, "little") for i in range(4))
    assert dig == hashlib.sha3_256(b"").digest()
    print("reference == hashlib sha3_256")
