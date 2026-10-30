"""ML-KEM-512 on the RP2350 + chip with the ring Keccak core: time per operation and speed-up.

    python3 research/ntt_cache/mlkem_model.py   (copied from research/sha3 in the team repo)

Inputs:
  RP2350 split per operation (pqm4 m4fspeed at 150 MHz, cited in tt-viktor-chiptor sim/config.py):
      total time, hashing share, 26-27 Keccak-f[1600] permutations per operation
  hash calls of ML-KEM-512 (FIPS 203): listed in CALLS below, inputs/outputs in bytes
  ring core: clocks per permutation measured in RTL simulation (K = 8: 368, K = 4: 760, K = 2: 1544)
  chip clock 50 MHz; one command word per clock; a K-bit step per absorb/squeeze word
  ASSUMPTIONS: 2 us host setup per hash call (300 cycles, same as Viktor's model); the host's PIO + DMA
  keep the chip fed at one word per clock; one "skip lane" word per untouched lane in a padded block.
"""

F_CHIP = 50e6
F_RP = 150e6
SETUP_S = 300 / F_RP

# pqm4 m4fspeed, ML-KEM-512 (total cycles at 150 MHz, hashing share), from tt-viktor-chiptor sim/config.py
RP = {"keygen": (2.62e-3, 1.98e-3), "encaps": (2.61e-3, 1.92e-3), "decaps": (2.85e-3, 1.92e-3)}
RP_PER_PERM = {"keygen": 1.98e-3 / 27, "encaps": 1.92e-3 / 26, "decaps": 1.92e-3 / 26}

SHA3_512, SHA3_256, SHAKE128, SHAKE256 = 72, 136, 168, 136  # rates in bytes
# (name, rate, bytes in, bytes out, secret input?)
G = lambda n: ("G", SHA3_512, n, 64, True)
H_EK = ("H", SHA3_256, 800, 32, False)
J = ("J", SHAKE256, 800, 32, True)
XOF = ("XOF", SHAKE128, 34, 504, False)          # 3 blocks per matrix entry on average (12 perms / 4)
PRF3 = ("PRF", SHAKE256, 33, 192, True)          # eta1 = 3
PRF2 = ("PRF", SHAKE256, 33, 128, True)          # eta2 = 2
CALLS = {
    "keygen": [G(33)] + [XOF] * 4 + [PRF3] * 4 + [H_EK],
    "encaps": [H_EK, G(64)] + [XOF] * 4 + [PRF3] * 2 + [PRF2] * 3,
    "decaps": [G(64), J] + [XOF] * 4 + [PRF3] * 2 + [PRF2] * 3,
}
CORES = {  # K: (clocks per permutation, synthesized um^2 standalone)
    8: (368, 46981), 4: (760, 41428), 2: (1544, 38614),
}


def perms(call):
    _, rate, n_in, n_out, _ = call
    absorb_blocks = (n_in + 1 + rate - 1) // rate          # pad10*1 always adds at least one byte
    squeeze_blocks = (n_out + rate - 1) // rate
    return absorb_blocks + squeeze_blocks - 1


def chip_clocks(call, K):
    """clocks the chip spends on one hash call: clear, absorb (+ skipped lanes), permutations, squeeze"""
    _, rate, n_in, n_out, _ = call
    clk_perm = CORES[K][0]
    steps_per_lane = 64 // K
    lanes_rate = rate // 8
    padded = n_in + 1
    full_blocks, rem = divmod(padded, rate)
    clocks = steps_per_lane                                                 # CLEAR
    clocks += full_blocks * rate * 8 // K                                   # whole blocks
    if rem:
        data_lanes = (rem + 7) // 8
        clocks += data_lanes * steps_per_lane                               # lanes with data/pad byte
        clocks += max(0, lanes_rate - data_lanes - 1)                       # skip words
        clocks += steps_per_lane                                            # last lane (0x80)
    clocks += perms(call) * clk_perm
    out_lanes = (n_out + 7) // 8
    clocks += out_lanes * steps_per_lane                                    # squeeze, whole lanes
    return clocks


def op_time(op, K, which):
    """which: 'all' (every hash call on the chip) or 'public' (XOF and H(ek) only)"""
    total, hashing = RP[op]
    t = total - hashing
    for c in CALLS[op]:
        if which == "all" or not c[4]:
            t += chip_clocks(c, K) / F_CHIP + SETUP_S
        else:
            t += perms(c) * RP_PER_PERM[op]
    return t


def main():
    for op in CALLS:
        n = sum(perms(c) for c in CALLS[op])
        pub = sum(perms(c) for c in CALLS[op] if not c[4])
        print(f"{op}: {n} permutations ({pub} public)")
    base = sum(v[0] for v in RP.values())
    sw = 5.50e-3                                   # Viktor's software-only estimate (A-hat reuse + 2 cores)
    print(f"\nRP2350 alone: {base * 1e3:.2f} ms per exchange (keygen + encaps + decaps)")
    print(f"software only (Viktor's estimate): {sw * 1e3:.2f} ms = {base / sw:.2f}x\n")
    print(f"{'core':28} {'per perm':>9} {'exchange':>9} {'vs RP':>6} {'vs SW':>6}")
    for K in (8, 4, 2):
        for which in ("all", "public"):
            ex = sum(op_time(op, K, which) for op in CALLS)
            per = CORES[K][0] / F_CHIP * 1e6
            print(f"K={K} ({CORES[K][0]} clk), {which:6} hashing  {per:6.1f} us {ex * 1e3:7.2f}ms "
                  f"{base / ex:5.2f}x {sw / ex:5.2f}x")


if __name__ == "__main__":
    main()
