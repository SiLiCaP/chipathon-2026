# Spec 6: Keccak Core (draft)

Oct 18, 2026 · draft from the SHA-3 research (`research/sha3/`), not yet in `specs/`

## Your job

You own `keccak_ring`, the Keccak-f[1600] sponge core that sits next to the NTT core in the shell. It
runs every SHA-3 and SHAKE call of ML-KEM-512 (G, H, J, PRF, XOF). The host (Spec 1) pads and decides
what to absorb and squeeze. You never touch the NTT datapath, the pins or the output packer.

- **In scope:** the state, the permutation, and the five commands below.
- **Out of scope:** padding, the rate and the capacity (the host only absorbs lanes 0..rate/64−1), the
  pin decode and masking (Spec 5 shell), the output OR (Spec 5).
- **Reference:** FIPS 202. Bit-exact against Python `hashlib` for SHA3-256, SHA3-512, SHAKE128 and
  SHAKE256.

## Module and ports

| Port | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `clk`, `rst_n` | in | 1 each | Synchronous reset sets the FSM to idle and the pointer to 0. It does not clear the state; CLEAR does |
| `cmd_valid` | in | 1 | A command this clock (the shell's registered `hash_w`) |
| `cmd` | in | 3 | Command (table below) = `{sof_q, op_q}` |
| `din` | in | K | Data for ABSORB = `coeff_q[K-1:0]` |
| `dout` | out | K | Squeezed bits, registered, valid the clock after SQUEEZE. 0 when not squeezing |
| `busy` | out | 1 | High during PERMUTE and CLEAR. Not on a pin: the host uses fixed delays |

Parameters: `K` (bits per command: 2, 4 or 8; the chip uses 8) and `CLOCK_GATING` (1 on the chip; 0
is the enable-flop variant for comparison).

## Commands

A lane step moves K bits. `ptr` counts lane steps (M = 64/K per lane).

| cmd | Name | Effect | Busy |
| --- | --- | --- | --- |
| 000 | ABSORB | lane[ptr/M] bits K·(ptr%M).. ^= din, ptr++ (lanes 0..20 only) | – |
| 001 | SQUEEZE | dout ← the same bits, next clock; ptr++ | – |
| 010 | PERMUTE | Keccak-f[1600]; ptr ← 0 | 368 clocks (K = 8) |
| 011 | CLEAR | state ← 0; ptr ← 0 | M clocks (8) |
| 100 | SKIP | ptr jumps to the next lane, which stays untouched. Only at a lane boundary | – |
| 101–111 | – | no-op | – |

- A command while `busy` is high is undefined. The host must wait the fixed busy time.
- The state is random after power-up and is not cleared by reset. The host starts every hash call with
  CLEAR, which also wipes the previous call's (possibly secret) state.
- Byte order: ABSORB/SQUEEZE step through a lane from bit 0 upward, so byte j of the lane is the
  j-th command (little-endian, as in FIPS 202).
- At the pins (Spec 5) a squeezed byte appears on `uo_out` 2 clocks after its command word.

## Structure (how it reaches 368 clocks in about 47k µm²)

- State: 25 lanes × K sub-rings × M plain `dfxtp` shift flip-flops. Sub-ring b of lane L holds bits
  z = K·i + b. No enable, no reset, no mux on any state bit.
- ρ: each ring group has one clock gate (`sky130_fd_sc_hd__dlclkp_1`, 47 for K = 8). In the ρ phase
  (M − 1 clocks) each group shifts for its own number of clocks.
- π and the sub-ring part of ρ are fixed wiring.
- A pass (M clocks) reads K slices per clock and applies χ, ι and the next round's θ, writing back in
  the loop. A θ-only pass starts each permutation.
- Clocks: M + 24·(2M − 1) = 368 (K = 8), 760 (K = 4), 1,544 (K = 2).
- Golden model: `ring_model.py` (structure) and `keccak_f_ref` in it (FIPS 202).

## Rules that differ from the other blocks

These are allowed in this block only, each for a measured reason:

| Rule | Why |
| --- | --- |
| Clock gates (`dlclkp`) in the RTL | ρ without a 64:1 mux per bit. The enable-flop variant costs +15k µm² and still needs 1,860 hold buffers |
| Custom SDC: hold uncertainty 0.14 ns (setup stays 0.25 ns) | A ring hop is Q→D with no logic. With the default 0.25 ns, the resizer puts a hold buffer on every hop (1,400–1,700 buffers), and the design no longer routes in 3x2, 4x2 or 6x2 |
| Hold slack margins 0.03 / 0.02 ns | With the default margins the 0.14 SDC still gives about 1,380 buffers |
| Small cells allowed in synthesis (`dfxtp_1`, `xor2_1`, …) | 88.7k → 69.3k µm² for NTT + Keccak |
| `clkdlybuf4s*` banned from PnR buffering | The resizer used them as fanout buffers (1.0–1.6 ns each) and broke slow-corner setup |

## Constant time

Every command has a fixed clock count, independent of the data. The host's waits are fixed.

## Tests

- `test_keccak_ring.py`: 4 random states against `keccak_f_ref`; SHA3-256/512 and SHAKE128/256 for
  message lengths 0, 1, 33, rate−1, rate, rate+1 and 800, with and without SKIP.
- `test/test_hash.py` (shell level): the same hashes through the pins, plus NTT and hash words
  interleaved.
- Gate level: the 5-case GL set mixing SKIP and no-SKIP.
