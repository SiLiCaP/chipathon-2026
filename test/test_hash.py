# SPDX-License-Identifier: Apache-2.0
"""Hash (Keccak sponge) tests through the Tiny Tapeout pins.

A hash word is a pin word with valid = 1 and coeff[11:8] = 0xF (never a coefficient):
op = command (0 absorb, 1 squeeze, 2 permute, 3 clear), coeff[7:0] = data byte.
The host has no busy flag on the pins, so it waits the fixed busy times:
PERMUTE 368 clocks, CLEAR 8 clocks (K = 8). The squeezed byte of word k is on
uo_out at sample k + 2 (1 clock io_in_capture, 1 clock keccak_ring dout register).
The host pads and absorbs / squeezes whole lanes (8 bytes) between permutations.

Under GATES=yes a reduced set of cases runs because gate-level simulation is slow.
"""

import hashlib
import os
import random

import cocotb

import ntt_ref as ref
from ntt_host import IDLE, OP_SC, scale_units, stream
from test import new_chip

GL = os.environ.get("GATES") == "yes"

ABSORB, SQUEEZE, PERMUTE, CLEAR = 0, 1, 2, 3
PERM_BUSY = 368            # 8 + 24 * 15 for K = 8
CLEAR_BUSY = 8
SQ_DELAY = 2               # squeeze word k -> byte at sample k + 2

ALGOS = {  # name: (rate, domain byte, output length)
    "sha3_256": (136, 0x06, 32),
    "sha3_512": (72, 0x06, 64),
    "shake_128": (168, 0x1F, 504),
    "shake_256": (136, 0x1F, 192),
}


def hw(cmd, data=0):
    """A hash command as a (coeff, valid, sof, op) pin word."""
    return (0xF00 | data, 1, 0, cmd)


class HashJob:
    """Builds the clock words of one sponge (clear, absorb, permute, squeeze) and
    finds the squeezed bytes in the samples. Words are lists so callers can
    splice them between other traffic."""

    def __init__(self, name, msg):
        self.name, self.msg = name, msg
        rate, dom, outlen = ALGOS[name]
        self.rate, self.outlen = rate, outlen

    def absorb_words(self):
        """CLEAR, then each padded block absorbed and permuted."""
        rate = self.rate
        m = bytearray(self.msg) + bytes([ALGOS[self.name][1]])
        m += bytes((-len(m)) % rate)
        m[-1] |= 0x80
        words = [hw(CLEAR)] + [IDLE] * CLEAR_BUSY
        for i in range(0, len(m), rate):
            words += [hw(ABSORB, b) for b in m[i:i + rate]]
            words += [hw(PERMUTE)] + [IDLE] * PERM_BUSY
        return words

    def squeeze_words(self):
        """Squeeze blocks (whole lanes) with a permutation between them.
        Returns (words, indices of the squeeze words)."""
        words, sq = [], []
        got = 0
        while got < self.outlen:
            if got:
                words += [hw(PERMUTE)] + [IDLE] * PERM_BUSY
            take = min(self.rate, (self.outlen - got + 7) // 8 * 8)
            for _ in range(take):
                sq.append(len(words))
                words.append(hw(SQUEEZE))
            got += take
        return words, sq

    def expected(self):
        h = hashlib.new(self.name, self.msg)
        return h.digest(self.outlen) if self.name.startswith("shake") else h.digest()


def check_quiet(samples, used, what):
    stray = [(k, v) for k, v in enumerate(samples) if v and k not in used]
    assert not stray, f"{what}: bytes outside the squeeze clocks: {stray[:8]}"


async def run_hash(chip, job):
    """Run one sponge on its own; check the digest and that nothing else comes out."""
    absorb = job.absorb_words()
    sq_words, sq = job.squeeze_words()
    samples = await chip.run(absorb + sq_words, tail=SQ_DELAY + 2)
    at = [len(absorb) + k + SQ_DELAY for k in sq]
    out = bytes(samples[k] for k in at)[:job.outlen]
    check_quiet(samples, set(at), f"{job.name} len {len(job.msg)}")
    assert out == job.expected(), f"{job.name} len {len(job.msg)} mismatch"
    return len(absorb) + len(sq_words)


@cocotb.test()
async def test_hash_algos(dut):
    """SHA3-256, SHA3-512, SHAKE128, SHAKE256 against hashlib, for message lengths
    0, 1, 33, rate - 1, rate, rate + 1, 800 (GL: a reduced set)."""
    chip = await new_chip(dut)
    rng = random.Random(21)
    if GL:
        cases = [("sha3_256", (0, 136)), ("sha3_512", (33,)),
                 ("shake_128", (1,)), ("shake_256", (135,))]
    else:
        cases = [(name, (0, 1, 33, rate - 1, rate, rate + 1, 800))
                 for name, (rate, _, _) in ALGOS.items()]
    for name, lengths in cases:
        for n in lengths:
            msg = bytes(rng.getrandbits(8) for _ in range(n))
            clocks = await run_hash(chip, HashJob(name, msg))
            dut._log.info(f"{name:9s} len {n:3d}: ok, {clocks} clocks")


@cocotb.test()
async def test_hash_ntt_interleaved(dut):
    """An NTT scale job and a SHA3-512 job share the pins: the absorb words go in
    the idle slots of the scale units, the permutation runs while the scale
    results stream out, and the squeeze comes after. Both results are checked,
    with no stray bytes on either side; then each runs alone again."""
    chip = await new_chip(dut)
    rng = random.Random(22)
    f = [rng.randrange(ref.Q) for _ in range(256)]
    job = HashJob("sha3_512", bytes(rng.getrandbits(8) for _ in range(50)))

    absorb = job.absorb_words()
    clear, rest = absorb[:1 + CLEAR_BUSY], absorb[1 + CLEAR_BUSY:]
    n_abs = job.rate                      # one block: 72 absorb words, then PERMUTE + busy
    abs_words, perm = rest[:n_abs], rest[n_abs:]
    assert len(perm) == 1 + PERM_BUSY

    ntt_words, starts = stream(OP_SC, scale_units(f), sof=True)
    idle_slots = [k for k, w in enumerate(ntt_words) if w == IDLE]
    assert len(idle_slots) >= n_abs + 1
    words = list(ntt_words)
    for k, w in zip(idle_slots, abs_words + perm[:1]):
        words[k] = w                       # absorb bytes, then PERMUTE, in NTT gaps
    # the permutation (368 clocks) overlaps the end of the NTT stream and its results
    perm_at = idle_slots[n_abs]
    words += [IDLE] * max(0, perm_at + 1 + PERM_BUSY - len(words))
    sq_words, sq = job.squeeze_words()
    base = len(clear) + len(words)
    samples = await chip.run(clear + words + sq_words, tail=SQ_DELAY + 2)

    at = [base + k + SQ_DELAY for k in sq]
    out = bytes(samples[k] for k in at)
    assert out == job.expected(), "interleaved hash mismatch"
    ntt_starts = [s + len(clear) for s in starts]
    pairs = chip.pairs(OP_SC, ntt_starts, samples, strict=False)
    assert [c for p in pairs for c in p] == ref.chip_scale(f), "interleaved scale mismatch"
    ntt_used = set()
    for p in ntt_starts:
        ntt_used.update(range(p + chip.tail(OP_SC) - 3, p + chip.tail(OP_SC)))
    check_quiet(samples, ntt_used | set(at), "interleaved")

    # a hash job, an NTT job, and the hash state held across the NTT job
    from ntt_host import scale_job
    await run_hash(chip, job)
    assert await scale_job(chip, f) == ref.chip_scale(f)
    held = job.absorb_words()
    await chip.run(held)
    assert await scale_job(chip, f[::-1]) == ref.chip_scale(f[::-1])
    sq_words, sq = job.squeeze_words()
    samples = await chip.run(sq_words, tail=SQ_DELAY + 2)
    assert bytes(samples[k + SQ_DELAY] for k in sq) == job.expected(), "hash state lost across NTT job"
    chip.check_asserts()
