# keccak_lat: latch-array Keccak core (research prototype)

Oct 18, 2026 · branch `sha3-latch` · not part of the specs

## Why it exists

The ring core (`keccak_ring`, branch `sha3-4x2`) keeps the Keccak state in shift rings: about 1,600
flip-flop to flip-flop hops with no logic between them. A hop has about 0.18 ns of real hold slack at the
fast corner. With Tiny Tapeout's default 0.25 ns hold uncertainty the resizer puts a buffer on every hop
and the design no longer routes, so the ring core only hardens with a custom SDC (hold uncertainty
0.14 ns). A sweep showed no safer value works in 3x2: 0.14 ns gives 7 hold buffers, 0.16 ns gives 819,
0.18 ns gives 1,319 (routing fails).

`keccak_lat` removes the shift hops, so it should harden with the default hold constraints.

## Structure

- **State:** 25 words of 64 `dlxtp` latches. Each word is written through its own clock gate
  (`dlclkp`): the gate opens in the high phase of the clock after the write was decided, while the
  write register `wdata` holds the data. A latch closes half a clock after its data launched, so the
  write path has about half a period of hold slack.
- **Read port:** one 25:1 mux. A word written this clock reads back its new value (the latch is
  transparent), so ABSORB and SQUEEZE can follow each other on the same lane.
- **pi without moving data:** the output lane D of a round is written into the word that held its
  source lane pi_src(D). At the start of round r, logical lane L sits in word pi_src^r(L). The 24
  non-zero lanes form one pi cycle, so the mapping is a 5-bit add mod 24 plus two small tables, and after
  24 rounds it is the identity again: ABSORB and SQUEEZE see the state in natural order.
- **Datapath:** 64 bits wide. Column parities C (5 x 64 flip-flops), the five row registers B
  (5 x 64), a barrel rotator for rho, chi and iota into `wdata`.

Each round takes 61 clocks:

| Phase | Clocks | What it does |
| --- | --- | --- |
| PAR | 25 | Read every lane, accumulate the column parities C |
| ROW | 5 x 7 | Per output row: read the 5 source lanes, B[k] = rot(lane ^ D[col], rho); chi + iota for k = 0..4 into `wdata`, written one clock later into the source's word |
| TAIL | 1 | chi of the last lane |

PERMUTE takes 24 x 61 = **1,464 clocks** (ring: 368). CLEAR takes 1 clock (ring: 8).

The ports and the five commands (ABSORB, SQUEEZE, PERMUTE, CLEAR, SKIP) are the same as `keccak_ring`,
so the core drops into the same top. Only the host's busy times change.

## Results

| Check | Result |
| --- | --- |
| Core tests (`test/keccak`) | **Pass** (measured): 4 random permutations against FIPS 202; SHA3-256/512 and SHAKE128/256 for lengths 0, 1, 33, rate-1, rate, rate+1 and 800, with and without SKIP, bit-exact against hashlib |
| Project tests (`test/`, through the pins) | **16/16 pass** (measured), including NTT and hash interleaved and the hash state held across an NTT job. `test/core` passes in all 4 latency variants |
| Plain synthesis, all cells allowed | **74.4k µm²** (ring: 47.0k), of which 24.0k is the 1,600 latches |
| LibreLane synthesis, NTT + latch core, default cell list | 122.1k µm² |
| 4x2 harden, every TT default | **Fails** (measured, `exp_lat_4x2`): 95.2 % utilization after global placement, DPL-0036 in detailed placement |
| 4x2 harden, small-cell list + `clkdlybuf4s*` banned, default hold (0.25 ns, margins 0.1 / 0.05) | Running (`exp_lat_4x2s`) |
| ML-KEM-512 exchange, all hashing on chip (serial model) | 4.90 ms = **1.65x** vs RP2350 alone (ring: 3.17 ms = 2.55x; software only: 1.47x) |

## Assessment

The latch core works and removes the hold risk by construction, but it costs 1.6x the area of the ring
core and 4x the clocks per permutation. It does not fit 3x2 next to the NTT core. The 1,600 latches
and their read port alone take about 35k µm², so trimming the datapath (a 32-bit version, for example)
would not bring it near the ring core. Use it only if the custom hold SDC is ruled out, and then in 4x2.

## How to run

```
cd test/keccak && make          # core tests (cocotb + Icarus, behavioral latch and clock-gate models)
cd test && make                  # project tests through the pins
```

In the tt-local-ci image, put `/opt/venv-test/bin` first on `PATH`.
