"""cocotb test of keccak_ring against hashlib and the reference permutation."""
import hashlib
import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import FallingEdge, RisingEdge, ReadOnly

from ring_model import keccak_f_ref

ABSORB, SQUEEZE, PERMUTE, CLEAR, SKIP = 0, 1, 2, 3, 4


class Host:
    def __init__(self, dut):
        self.dut = dut
        self.clocks = 0
        self.k = int(dut.K.value)
        self.m = 64 // self.k
        self.perm_clocks = self.m + 24 * (2 * self.m - 1)
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
        await self.wait_idle(self.m)

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
