"""ML-KEM-512 exchange time per NTT-cache candidate (docs/ntt_cache_study.md).

    python3 research/ntt_cache/overlap_cache.py

Uses overlap_model.py (the list scheduler from research/sha3, copied here unchanged) and
swaps in each candidate's chip clocks and CPU packing cost. NTT, inverse NTT and basemul
always run on the chip (arith policy "chip"); hashing runs on the Keccak core.

Chip clocks per job (pin I/O included):
  streaming (today)  NTT 2,751, inverse 2,765 + scale 394, basemul 655 (docs/host_timing.md)
  block engines      2 passes x 16 blocks, measured at the pins in RTL simulation (test_blk.py on
                     branch sha3-4x2-cache, one NTT with the host turnaround between the passes)
  half polynomial    layer 0 streamed (2 clocks / butterfly, one half stays on chip), 6 layers
                     per half on chip (384 clocks each), halves swapped through the pins
  Keccak state       half polynomial in a 128-deep circulating ring, one butterfly per 2 clocks
                     (ESTIMATE, not designed)
CPU packing: today's numbers are 12 cycles per streamed butterfly (2 coefficients in, 2 out),
"PIO flags" 3; that is 3.0 / 0.75 cycles per coefficient moved, used for every candidate.
Basemul and scale packing are unchanged (14 / 8 today, 4 / 2 PIO flags).
"""
import overlap_model as om

F_CHIP, F_RP = om.F_CHIP, om.F_RP


# name: (NTT clocks, inverse NTT clocks without scale, coefficient moves per (i)NTT)
VARIANTS = {
    "streaming (today)": (2751, 2765, 896 * 4),
    "(a) B3 LF: L + C + F": (1010, 1010, 1024),      # block-mode variants: measured in RTL simulation
    "(a) B3 L: L + C": (1428, 1428, 1024),           # (test_blk.py on branch sha3-4x2-cache, NTT_BLK=..)
    "(a) B3 F: C + F": (1490, 1490, 1024),
    "(a) B3 C: C only": (1908, 1908, 1024),
    "(b) half polynomial": (1420, 1472, 640),        # model
    "(c) Keccak state": (2000, 2050, 640),           # ESTIMATE
}
SCALE, BM = 394, 655
PER_MOVE = {"today": 3.0, "PIO flags": 0.75}


def use(name):
    ntt, intt, moves = VARIANTS[name]
    om.CHIP = {"ntt": ntt / F_CHIP, "intt": (intt + SCALE) / F_CHIP, "bm": BM / F_CHIP}

    def pack_s(kind, mode):
        p = om.PACK[mode]
        if kind == "ntt":
            return moves * PER_MOVE[mode] / F_RP
        if kind == "intt":
            return (moves * PER_MOVE[mode] + 128 * p["scale"]) / F_RP
        return 128 * p["piece"] / F_RP
    om.pack_s = pack_s


def exchange(name, mode, cores, split=False):
    use(name)
    return sum(om.schedule(op, mode, "chip", split, cores) for op in om.GRAPHS)


def main():
    base = sum(om.RP[op][0] for op in om.RP)
    print(f"RP2350 alone: {base * 1e3:.2f} ms; NTT jobs always on the chip, hashing on the Keccak core\n")
    print("columns: CPU packing today / PIO flags, 1 or 2 RP2350 cores; 'split': NTT jobs may use the pins")
    print("while the Keccak core permutes (both engines busy at once, optimistic)\n")
    print(f"{'candidate':26} {'NTT':>6} {'us':>5} | {'1c today':>9} {'1c PIO':>8} {'2c PIO':>8} {'2c split':>9}")
    for name, (ntt, intt, moves) in VARIANTS.items():
        t1 = exchange(name, "today", 1)
        t2 = exchange(name, "PIO flags", 1)
        t3 = exchange(name, "PIO flags", 2)
        # (c) holds the coefficients in the Keccak state: no hashing during an NTT, so no split
        t4 = exchange(name, "PIO flags", 2, split=not name.startswith("(c)"))
        print(f"{name:26} {ntt:6d} {ntt / F_CHIP * 1e6:5.1f} | " +
              " ".join(f"{t * 1e3:6.2f}ms" for t in (t1, t2, t3, t4)))
    use("streaming (today)")
    sw1 = sum(om.schedule(op, "today", "sw", False, 1) for op in om.GRAPHS)
    sw2 = sum(om.schedule(op, "today", "sw", False, 2) for op in om.GRAPHS)
    print(f"\nfor reference, hash on chip and NTT in software: 1 core {sw1 * 1e3:.2f} ms, 2 cores {sw2 * 1e3:.2f} ms")


if __name__ == "__main__":
    main()
