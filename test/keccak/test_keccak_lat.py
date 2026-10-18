"""cocotb test of keccak_lat against hashlib and the reference permutation."""
import hashlib
import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import FallingEdge, RisingEdge, ReadOnly


ABSORB, SQUEEZE, PERMUTE, CLEAR, SKIP = 0, 1, 2, 3, 4
PERM_CLOCKS = 24 * 61
CLEAR_CLOCKS = 1

R = [[0, 36, 3, 41, 18], [1, 44, 10, 45, 2], [62, 6, 43, 15, 61], [28, 55, 25, 21, 56], [27, 20, 39, 8, 14]]
RC = [0x0000000000000001, 0x0000000000008082, 0x800000000000808A, 0x8000000080008000,
      0x000000000000808B, 0x0000000080000001, 0x8000000080008081, 0x8000000000008009,
      0x000000000000008A, 0x0000000000000088, 0x0000000080008009, 0x000000008000000A,
      0x000000008000808B, 0x800000000000008B, 0x8000000000008089, 0x8000000000008003,
      0x8000000000008002, 0x8000000000000080, 0x000000000000800A, 0x800000008000000A,
      0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008]


def rot(v, n):
    n %= 64
    return ((v << n) | (v >> (64 - n))) & (2**64 - 1) if n else v


def keccak_f_ref(A):
    """FIPS 202 Keccak-f[1600] on 25 x 64-bit lanes, lane index x + 5*y."""
    A = list(A)
    for rd in range(24):
        C = [A[x] ^ A[x + 5] ^ A[x + 10] ^ A[x + 15] ^ A[x + 20] for x in range(5)]
        D = [C[(x - 1) % 5] ^ rot(C[(x + 1) % 5], 1) for x in range(5)]
        A = [A[L] ^ D[L % 5] for L in range(25)]
        B = [0] * 25
        for x in range(5):
            for y in range(5):
                B[y + 5 * ((2 * x + 3 * y) % 5)] = rot(A[x + 5 * y], R[x][y])
        A = [B[L] ^ (~B[(L % 5 + 1) % 5 + 5 * (L // 5)] & B[(L % 5 + 2) % 5 + 5 * (L // 5)])
             for L in range(25)]
        A[0] ^= RC[rd]
    return A


class Host:
    def __init__(self, dut):
        self.dut = dut
        self.clocks = 0
        self.k = int(dut.K.value)
        self.m = 64 // self.k
        self.perm_clocks = PERM_CLOCKS
        self.use_skip = True

    async def cmd(self, c, d=0):
        await FallingEdge(self.dut.clk)
        self.dut.cmd_valid.value = 1
        self.dut.cmd.value = c
        self.dut.din.value = d
        await RisingEdge(self.dut.clk)
        self.clocks += 1

    async def wait_idle(self, expect):
        await FallingEdge(self.dut.clk)
        self.dut.cmd_valid.value = 0
        n = 0
        while self.dut.busy.value:
            await FallingEdge(self.dut.clk)
            n += 1
        assert n == expect, f"busy for {n} clocks, expected {expect}"
        self.clocks += n

    async def permute(self):
        await self.cmd(PERMUTE)
        await self.wait_idle(self.perm_clocks)

    async def clear(self):
        await self.cmd(CLEAR)
        await self.wait_idle(CLEAR_CLOCKS)

    async def absorb(self, data):
        assert len(data) % 8 == 0
        k = self.k
        for b in data:
            for j in range(0, 8, k):
                await self.cmd(ABSORB, (b >> j) & ((1 << k) - 1))

    async def squeeze(self, n):
        assert n % 8 == 0
        out = []
        k = self.k
        for _ in range(n):
            v = 0
            for j in range(0, 8, k):
                await self.cmd(SQUEEZE)
                await ReadOnly()
                v |= int(self.dut.dout.value) << j
            out.append(v)
        return bytes(out)

    async def sponge(self, rate, dom, msg, outlen):
        await self.clear()
        m = bytearray(msg) + bytes([dom])
        m += bytes((-len(m)) % rate)
        m[-1] |= 0x80
        for i in range(0, len(m), rate):
            blk = m[i:i + rate]
            lanes = [blk[j:j + 8] for j in range(0, rate, 8)]
            for ln in lanes:
                if any(ln) or not self.use_skip:
                    await self.absorb(ln)
                else:
                    await self.cmd(SKIP)
            await self.permute()
        out = b""
        while len(out) < outlen:
            if out:
                await self.permute()
            take = min(rate, (outlen - len(out) + 7) // 8 * 8)
            out += await self.squeeze(take)
        return out[:outlen]


async def setup(dut):
    cocotb.start_soon(Clock(dut.clk, 20, unit="ns").start())
    dut.cmd_valid.value = 0
    dut.cmd.value = 0
    dut.din.value = 0
    dut.rst_n.value = 0
    for _ in range(3):
        await RisingEdge(dut.clk)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)
    return Host(dut)


@cocotb.test()
async def test_permutation(dut):
    h = await setup(dut)
    rng = random.Random(7)
    for _ in range(4):
        await h.clear()
        lanes = [rng.getrandbits(64) for _ in range(21)] + [0] * 4
        await h.absorb(b"".join(v.to_bytes(8, "little") for v in lanes[:21]))
        await h.permute()
        got = await h.squeeze(168)
        exp = b"".join(v.to_bytes(8, "little") for v in keccak_f_ref(lanes)[:21])
        assert got == exp, "permutation mismatch"


@cocotb.test()
async def test_hashes(dut):
    h = await setup(dut)
    rng = random.Random(3)
    cases = [
        ("sha3_256", 136, 0x06, 32), ("sha3_512", 72, 0x06, 64),
        ("shake_128", 168, 0x1F, 504), ("shake_256", 136, 0x1F, 192),
    ]
    for (name, rate, dom, outlen), skip in [(c, s) for c in cases for s in (True, False)]:
        h.use_skip = skip
        for n in (0, 1, 33, rate - 1, rate, rate + 1, 800):
            msg = bytes(rng.getrandbits(8) for _ in range(n))
            got = await h.sponge(rate, dom, msg, outlen)
            ref = hashlib.new(name, msg)
            exp = ref.digest(outlen) if name.startswith("shake") else ref.digest()
            assert got == exp, f"{name} len {n} mismatch"
        dut._log.info(f"{name} skip={skip}: ok")
