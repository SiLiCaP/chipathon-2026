"""Hash jobs at the pins for the team's Keccak chips (chipathon-2026 branches sha3-4x2, sha3-latch, sha3-only),
emitting raw 16-bit pin words, so the same jobs run in cocotb, on the Tang Nano (USB) and through the Pico 2 W
rig. The K-general version of the team's test/test_hash.py HashJob, plus the hash-only chip's bursts and CBD.

Pin protocol: a hash word has valid = 1 and coeff[11:8] = 0xF; {sof, op} is the command, coeff[7:0] the data.
The host waits the fixed busy times; the byte asked for by word k appears on uo_out at sample k + 2.
  0 ABSORB, 1 SQUEEZE, 2 PERMUTE, 3 CLEAR, 4 SKIP (a lane)
  5 BURST n       (sha3-only) 8n bytes on consecutive clocks from one word
  6 BURST_CBD     (sha3-only) data {eta3, 2'b0, n}: the same bytes as SamplePolyCBD coefficients, 4-bit two's
                  complement; eta = 2: {f[2i+1], f[2i]} per byte; eta = 3: {0, f0}, {0, f1}, {f3, f2} per 3 bytes
Cores: keccak_ring with K = 2 / 4 (K-bit words) or 8 / 16 (byte words: the hash-only chip pairs bytes for K = 16):
CLEAR M, PERMUTE M + 24 (2M - 1) clocks, M = 64 / K. keccak_lat: CLEAR 1, PERMUTE 1,464.
"""
import hashlib

ABSORB, SQUEEZE, PERMUTE, CLEAR, SKIP, BURST, BURST_CBD = 0, 1, 2, 3, 4, 5, 6
SQ_DELAY = 2
ALGOS = {"sha3_256": (136, 0x06, 32), "sha3_512": (72, 0x06, 64),
         "shake_128": (168, 0x1F, 504), "shake_256": (136, 0x1F, 192)}
Q = 3329


def busy(core, K):
    """(CLEAR, PERMUTE) busy clocks."""
    if core == "lat":
        return 1, 1464
    M = 64 // K
    return M, M + 24 * (2 * M - 1)


def hw(cmd, data=0):
    """A hash command as a 16-bit pin word {uio, ui}: coeff = 0xF00 | data, valid, sof = cmd bit 2, op = cmd & 3."""
    return (0xF00 | data) | (1 << 12) | ((cmd >> 2) << 13) | ((cmd & 3) << 14)


def cbd(buf, eta):
    """SamplePolyCBD_eta (FIPS 203 Algorithm 8) as signed integers."""
    bits = [(buf[i // 8] >> (i % 8)) & 1 for i in range(len(buf) * 8)]
    return [sum(bits[2 * i * eta + j] for j in range(eta)) - sum(bits[2 * i * eta + eta + j] for j in range(eta))
            for i in range(256)]


def s4(v):
    return v - 16 if v & 8 else v


class HashJob:
    """One sponge: CLEAR, absorb, PERMUTE, squeeze. K: core step width; io: bits per word (min(K, 8)).
    burst: squeeze with BURST words; cbd_eta: 2 or 3 for a PRF (SHAKE256) whose output goes through BURST_CBD."""

    def __init__(self, name, msg, K=8, core="ring", skip=False, burst=False, cbd_eta=None):
        self.name, self.msg, self.K, self.skip = name, bytes(msg), K, skip
        self.io = min(K, 8)
        self.burst = burst or cbd_eta is not None
        self.cbd_eta = cbd_eta
        self.rate, self.dom, outlen = ALGOS[name]
        self.outlen = 64 * cbd_eta if cbd_eta else outlen
        self.clear_busy, self.perm_busy = busy(core, K)

    def _lane_words(self, lane):
        io, words = self.io, []
        v = int.from_bytes(lane, "little")
        for i in range(64 // io):
            words.append(hw(ABSORB, (v >> (io * i)) & ((1 << io) - 1)))
        return words

    def words(self):
        """All clock words of the job and the clock indices whose byte (at index + 2) belongs to the output."""
        m = bytearray(self.msg) + bytes([self.dom])
        m += bytes((-len(m)) % self.rate)
        m[-1] |= 0x80
        w = [hw(CLEAR)] + [0] * self.clear_busy
        for i in range(0, len(m), self.rate):
            for ln in range(i, i + self.rate, 8):
                lane = m[ln:ln + 8]
                w += [hw(SKIP)] if self.skip and not any(lane) else self._lane_words(lane)
            w += [hw(PERMUTE)] + [0] * self.perm_busy
        sq, got = [], 0
        while got < self.outlen:
            if got:
                w += [hw(PERMUTE)] + [0] * self.perm_busy
            take = min(self.rate, (self.outlen - got + 7) // 8 * 8)
            if self.burst:
                n = take // 8
                cmd, data = (BURST_CBD, n | (0x80 if self.cbd_eta == 3 else 0)) if self.cbd_eta else (BURST, n)
                sq += [len(w) + j for j in range(take)]
                w += [hw(cmd, data)] + [0] * (take - 1)
            else:
                for _ in range(take * 8 // self.io):
                    sq.append(len(w))
                    w.append(hw(SQUEEZE))
            got += take
        return w + [0] * (SQ_DELAY + 2), sq

    def collect(self, samples, sq):
        """The output bytes (or, for CBD, the coefficients) from the per-clock uo_out samples."""
        io, mask, v, n = self.io, (1 << self.io) - 1, 0, 0
        out = bytearray()
        for k in sq:
            v |= (samples[k + SQ_DELAY] & mask) << n
            n += io
            if n == 8:
                out.append(v)
                v = n = 0
        out = bytes(out[:self.outlen])
        if not self.cbd_eta:
            return out
        coeffs = []
        if self.cbd_eta == 2:
            for b in out:
                coeffs += [s4(b & 15), s4(b >> 4)]
        else:
            for j in range(0, len(out), 3):
                coeffs += [s4(out[j] & 15), s4(out[j + 1] & 15), s4(out[j + 2] & 15), s4(out[j + 2] >> 4)]
        return coeffs

    digest = collect

    def expected(self):
        h = hashlib.new(self.name, self.msg)
        if self.cbd_eta:
            return cbd(h.digest(self.outlen), self.cbd_eta)
        return h.digest(self.outlen) if self.name.startswith("shake") else h.digest()


def standard_jobs(K=8, core="ring"):
    """The algorithm set of the team's test_hash_algos, at core width K."""
    jobs = []
    for name, (rate, _, _) in ALGOS.items():
        for n in (0, 1, rate - 1, rate, rate + 1, 200):
            msg = bytes((7 * i + n) & 0xFF for i in range(n))
            jobs.append(HashJob(name, msg, K, core, skip=bool(n % 2)))
    return jobs


def hash_only_jobs(K=8):
    """The hash-only chip's own features: bursts for every algorithm, and CBD for ML-KEM's PRF calls."""
    jobs = []
    for name, (rate, _, _) in ALGOS.items():
        for n in (0, rate - 1, 200):
            msg = bytes((11 * i + n) & 0xFF for i in range(n))
            jobs.append(HashJob(name, msg, K, burst=True, skip=bool(n % 2)))
    for eta in (2, 3):                                 # PRF_eta(s, N) = SHAKE256(s || N, 64 eta)
        for nonce in (0, 1, 7):
            seed = bytes((5 * i + nonce) & 0xFF for i in range(32)) + bytes([nonce])
            jobs.append(HashJob("shake_256", seed, K, cbd_eta=eta))
    return jobs
