"""Hash jobs at the pins for the team's Keccak chips (chipathon-2026 branches sha3-4x2, sha3-latch, sha3-only),
emitting raw 16-bit pin words, so the same jobs run in cocotb, on the Tang Nano (USB) and through the Pico 2 W
rig. The K-general version of the team's test/test_hash.py HashJob, plus the hash-only chip's bursts and CBD.

Pin protocol: a hash word has valid = 1 and coeff[11:8] = 0xF; {sof, op} is the command, coeff[7:0] the data.
The host waits the fixed busy times; the byte asked for by word k appears on uo_out at sample k + 2.
  0 ABSORB, 1 SQUEEZE, 2 PERMUTE, 3 CLEAR, 4 SKIP (a lane)
  5 BURST n       (sha3-only) 8n bytes on consecutive clocks from one word
  6 BURST_CBD     (sha3-only) data {eta3, 2'b0, n}: the same bytes as SamplePolyCBD coefficients, 4-bit two's
                  complement; eta = 2: {f[2i+1], f[2i]} per byte; eta = 3: {0, f0}, {0, f1}, {f3, f2} per 3 bytes
  6 BURST_CBD, data bit 6 set (sha3-only): SampleNTT instead of CBD: the accepted 12-bit coefficients, packed
                  like ByteEncode_12, on the clocks where the valid pin (uio[0], framing mode 1) is high
  7 EXT           (sha3-only) data[7:6]: 01 ABSORB_N (data[5:0] = n - 1: the next n clocks' ui bytes are absorbed
                  as they are), 10 FINISH (data[1:0] = function: the chip pads and permutes), 11 MODE (data[0])
Framing mode 1 (after MODE 1): a hash word needs no 0xF marker, and uio[0] is the chip's valid output.
Cores: keccak_ring with K = 2 / 4 (K-bit words) or 8 / 16 (byte words: the hash-only chip pairs bytes for K = 16):
CLEAR M, PERMUTE M + 24 (2M - 1) clocks, M = 64 / K. keccak_lat: CLEAR 1, PERMUTE 1,464.
"""
import hashlib

ABSORB, SQUEEZE, PERMUTE, CLEAR, SKIP, BURST, BURST_CBD, EXT = 0, 1, 2, 3, 4, 5, 6, 7
EXT_ABSORB_N, EXT_FINISH, EXT_MODE = 0x40, 0x80, 0xC0     # EXT sub-commands (data[7:6])
BURST_NTT = 0x40                                          # BURST_CBD data bit 6: SampleNTT
RAW_MAX = 64                                              # longest ABSORB_N packet
NTT_TAIL = 3                                              # SampleNTT bytes leave up to 3 clocks after a burst
VALID = 0x100                                             # the valid pin in a sample (mode 1), next to uo_out
SQ_DELAY = 2
ALGOS = {"sha3_256": (136, 0x06, 32), "sha3_512": (72, 0x06, 64),
         "shake_128": (168, 0x1F, 504), "shake_256": (136, 0x1F, 192)}
FIN_ALG = {"sha3_256": 0, "sha3_512": 1, "shake_128": 2, "shake_256": 3}    # FINISH data[1:0]
Q = 3329


def busy(core, K):
    """(CLEAR, PERMUTE) busy clocks."""
    if core == "lat":
        return 1, 1464
    M = 64 // K
    return M, M + 24 * (2 * M - 1)


def hw(cmd, data=0, mode=0):
    """A hash command as a 16-bit pin word {uio, ui}: coeff = 0xF00 | data, valid, sof = cmd bit 2, op = cmd & 3.
    mode 1: no marker (uio[3:0] = 0; uio[0] is the chip's output there)."""
    return (0 if mode else 0xF00) | data | (1 << 12) | ((cmd >> 2) << 13) | ((cmd & 3) << 14)


def fin_steps(p, rate):
    """Padding steps of FINISH from byte pointer p in the block (hash_io.v). The host then idles
    fin_steps + 1 + the PERMUTE busy time."""
    if p // 8 == rate // 8 - 1:
        return 8 - p % 8
    return (8 - p % 8) + (rate // 8 - 2 - p // 8) + 8


def sample_ntt(stream):
    """FIPS 203 SampleNTT (Algorithm 7) on an XOF byte stream: (the 256 coefficients, bytes consumed)."""
    a, i = [], 0
    while len(a) < 256:
        c0, c1, c2 = stream[i:i + 3]
        i += 3
        d1, d2 = c0 + 256 * (c1 % 16), c1 // 16 + 16 * c2
        if d1 < Q:
            a.append(d1)
        if d2 < Q and len(a) < 256:
            a.append(d2)
    return a, i


def encode12(coeffs):
    """ByteEncode_12: two coefficients per three bytes."""
    out = bytearray()
    for x, y in zip(coeffs[::2], coeffs[1::2]):
        out += bytes([x & 0xFF, (x >> 8) | ((y & 15) << 4), y >> 4])
    return bytes(out)


def cbd(buf, eta):
    """SamplePolyCBD_eta (FIPS 203 Algorithm 8) as signed integers."""
    bits = [(buf[i // 8] >> (i % 8)) & 1 for i in range(len(buf) * 8)]
    return [sum(bits[2 * i * eta + j] for j in range(eta)) - sum(bits[2 * i * eta + eta + j] for j in range(eta))
            for i in range(256)]


def s4(v):
    return v - 16 if v & 8 else v


class HashJob:
    """One sponge: CLEAR, absorb, PERMUTE, squeeze. K: core step width; io: bits per word (min(K, 8)).
    burst: squeeze with BURST words; cbd_eta: 2 or 3 for a PRF (SHAKE256) whose output goes through BURST_CBD;
    outlen: bytes to squeeze (default: the digest size, or the ML-KEM XOF / PRF length in ALGOS).
    The hash-only chip's later commands (K = 8 or 16):
    finish: the chip pads (FINISH) instead of the host; raw: absorb with ABSORB_N packets (raw_junk: with
    this value on the uio pins during the data clocks, to show they are ignored); mode: the framing mode the
    chip is in (1: words without the marker, samples carry the valid pin as VALID);
    ntt: a SHAKE128 whose output goes through SampleNTT (mode 1 only): 384 bytes, ByteEncode_12 of the
    polynomial; ntt_extra: more blocks squeezed after the polynomial is complete (the chip must stay quiet)."""

    def __init__(self, name, msg, K=8, core="ring", skip=False, burst=False, cbd_eta=None, outlen=None,
                 finish=False, raw=False, raw_junk=0, mode=0, ntt=False, ntt_extra=0):
        self.name, self.msg, self.K, self.skip = name, bytes(msg), K, skip
        self.io = min(K, 8)
        self.burst = burst or cbd_eta is not None or ntt
        self.cbd_eta = cbd_eta
        self.rate, self.dom, default_len = ALGOS[name]
        self.outlen = 64 * cbd_eta if cbd_eta else (outlen or default_len)
        self.clear_busy, self.perm_busy = busy(core, K)
        self.finish, self.raw, self.raw_junk, self.mode, self.ntt = finish, raw, raw_junk, mode, ntt
        assert not (finish or raw or mode or ntt) or self.io == 8, "byte-wide chips only"
        if ntt:
            assert name == "shake_128" and mode == 1, "SampleNTT: SHAKE128, and the valid pin needs mode 1"
            coeffs, used = sample_ntt(hashlib.shake_128(self.msg).digest(self.rate * 40))
            self.ntt_blocks = -(-used // self.rate) + ntt_extra
            self.outlen = self.ntt_blocks * self.rate
            self.ntt_bytes = encode12(coeffs)

    def hw(self, cmd, data=0):
        return hw(cmd, data, self.mode)

    def _absorb(self, data):
        """Words that absorb `data`: ABSORB words (SKIP for all-zero lanes if asked), or ABSORB_N packets."""
        w = []
        if self.raw:
            for i in range(0, len(data), RAW_MAX):
                part = data[i:i + RAW_MAX]
                w += [self.hw(EXT, EXT_ABSORB_N | (len(part) - 1))] + [(self.raw_junk << 8) | b for b in part]
            return w
        for ln in range(0, len(data), 8):
            lane = data[ln:ln + 8]
            if self.skip and len(lane) == 8 and not any(lane):
                w.append(self.hw(SKIP))
            elif self.io == 8:
                w += [self.hw(ABSORB, b) for b in lane]
            else:
                w += self._lane_words(lane)
        return w

    def _lane_words(self, lane):
        io, words = self.io, []
        v = int.from_bytes(lane, "little")
        for i in range(64 // io):
            words.append(self.hw(ABSORB, (v >> (io * i)) & ((1 << io) - 1)))
        return words

    def words(self):
        """All clock words of the job and the clock indices whose byte (at index + 2) belongs to the output."""
        hw = self.hw
        w = [hw(CLEAR)] + [0] * self.clear_busy
        if self.finish:                                    # the chip pads: whole blocks, then the rest + FINISH
            full = len(self.msg) // self.rate * self.rate
            for i in range(0, full, self.rate):
                w += self._absorb(self.msg[i:i + self.rate]) + [hw(PERMUTE)] + [0] * self.perm_busy
            w += self._absorb(self.msg[full:]) + [hw(EXT, EXT_FINISH | FIN_ALG[self.name])]
            w += [0] * (fin_steps(len(self.msg) - full, self.rate) + 1 + self.perm_busy)
        else:
            m = bytearray(self.msg) + bytes([self.dom])
            m += bytes((-len(m)) % self.rate)
            m[-1] |= 0x80
            for i in range(0, len(m), self.rate):
                w += self._absorb(m[i:i + self.rate]) + [hw(PERMUTE)] + [0] * self.perm_busy
        sq, got = [], 0
        while got < self.outlen:
            if got:
                w += [hw(PERMUTE)] + [0] * self.perm_busy
            take = min(self.rate, (self.outlen - got + 7) // 8 * 8)
            if self.ntt:
                sq += [len(w) + j for j in range(take + NTT_TAIL)]     # the clocks whose sample may be valid
                w += [hw(BURST_CBD, BURST_NTT | take // 8)] + [0] * (take - 1 + NTT_TAIL)
            elif self.burst:
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
        """The output bytes (or, for CBD, the coefficients) from the per-clock uo_out samples.
        SampleNTT: the bytes of the clocks marked valid."""
        if self.ntt:
            return bytes(s & 0xFF for s in samples if s & VALID)
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
        if self.ntt:
            return self.ntt_bytes
        h = hashlib.new(self.name, self.msg)
        if self.name.startswith("sha3") and not self.cbd_eta:
            return h.digest()[:self.outlen]
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


def finish_jobs(K=8):
    """FINISH (the chip pads) at every corner of the block, half of them fed with ABSORB_N packets."""
    jobs = []
    for name, (rate, _, _) in ALGOS.items():
        for j, n in enumerate((0, 1, 7, 8, rate - 9, rate - 8, rate - 1, rate, rate + 1, 200)):
            msg = bytes((13 * i + n) & 0xFF for i in range(n))
            jobs.append(HashJob(name, msg, K, burst=True, finish=True, raw=bool(j % 2)))
    return jobs


def raw_jobs(K=8):
    """ABSORB_N packets carrying the host-padded message, with junk on the uio pins during the data clocks."""
    jobs = []
    for j, (name, (rate, _, _)) in enumerate(ALGOS.items()):
        for n in (rate - 1, 200):
            msg = bytes((17 * i + n) & 0xFF for i in range(n))
            jobs.append(HashJob(name, msg, K, burst=True, raw=True, raw_junk=(0xFF, 0x1F, 0xE0, 0x5A)[j]))
    return jobs


def ntt_seed(t, j=0, i=0):
    return bytes((t + 29 * k + (t >> 8) * k * k) & 0xFF for k in range(32)) + bytes([j, i])


# Seeds found to hit the sampler's corners: the 256th coefficient taken from a triple in which both candidates
# pass (the second one must be dropped), and a polynomial that needs a fourth block.
NTT_CORNER_SEEDS = (2, 265)


def ntt_jobs(K=8):
    """ML-KEM's XOF calls rho || j || i through SampleNTT (mode 1): each returns ByteEncode_12 of one
    polynomial of A. The first also squeezes a block too many: the chip must stay quiet after 256."""
    seeds = [ntt_seed(1, j, i) for i in (0, 1) for j in (0, 1)] + [ntt_seed(t) for t in NTT_CORNER_SEEDS]
    return [HashJob("shake_128", s, K, mode=1, ntt=True, finish=bool(n % 2), raw=bool(n % 2), ntt_extra=int(n == 0))
            for n, s in enumerate(seeds)]
