# Tile sizes and routes: what fits where, and what is fastest (Oct 18, 2026)

For the team, before the tile decision. It evaluates every branch, hardens the routes at the tile sizes we
talk about (1×2, 2×2, 3×2, 4×2), asks whether a **SHA-only chip** (the NTT core cut out) fits 2×2 or 3×2, and
profiles the whole ML-KEM-512 chain for anything else worth turning into gates. Every number is tagged
**measured** (RTL simulation, LibreLane harden on Viktor's machine, emulator profile), **team** (measured by
the team, cited from the branch), **model** or **estimate**. Tools: tt-viktor-chiptor `tools/tile_exp.py`
(variants and hardens), `fpga/sim_variant` (pin-level hash test at any K), `sim/route_model.py` (speed),
`tools/mlkem_profile` (instruction profile).

## 1. Summary

- **The NTT-free chip exists: branch `sha3-only`** (section 7; 3×2 signoff detail in
  [sha3_only_3x2.md](sha3_only_3x2.md)). The NTT core is removed; the chip is the K = 8
  ring Keccak engine behind a new host adapter with burst squeeze and on-chip SamplePolyCBD (the "helper").
  Every hash, burst and CBD job is bit-exact in RTL simulation (K = 8 and K = 16), and the rig wiring is
  verified. Model: **3.18 ms per ML-KEM-512 exchange, 2.54×** the RP2350 alone (K = 16: 2.82 ms, 2.82×).
- **K = 8 needs 3×2, not 2×2** (measured). At 2×2 both the SHA-only K = 8 core and the hash-only chip are too
  congested (83.6 % after CTS; the router was still at 9,660 violations after an hour). **At 3×2 the hash-only
  chip signs off clean**: 55.5 %, +8.17 ns setup at the slow corner, 0 DRC / LVS / antenna. 2×2 holds K = 2
  and K = 4, both clean.
- **A SHA-only chip fits 2×2.** The ring Keccak core at K = 2 hardens cleanly in 2×2 (measured): 65 %
  utilization, 0 DRC / LVS / antenna errors, +9.9 ns setup slack at the slow corner. [K = 4 and K = 8: section 3.]
- **Cutting the NTT core makes ML-KEM faster, not slower.** The NTT core is pin-bound and slower than the
  RP2350's own NTT. With the same optimized host:
  - SHA-only ring K = 8: **3.47 ms per exchange, 2.33×** RP2350 alone;
  - NTT + ring K = 8: 4.27 ms, 1.89×.

  (model)
- **The host feed decides as much as the core.** With the RP2350 firmware as written (~23 CPU cycles per hash
  word), SHA-only K = 8 drops from 2.33× to 1.33×, and K = 2 from 1.22× to 0.55×. A PIO path that inserts the
  hash flag bits itself is needed for any of these numbers.
- **Nothing else in ML-KEM-512 is worth gates on its own.** Profiled on a Cortex-M33 (measured):
  - Keccak takes 57.5 % of all instructions and the NTT family 25.6 %;
  - every other step is 0.3–2.3 %, except polynomial add/reduce at 8 %;
  - those steps work on data that lives on the RP2350, so moving it over the pins costs more than the step.

  Only CBD sampling (1.2k µm², fixed rate) and rejection sampling (3.3k µm²) could ride on the hash engine's
  squeeze stream, for at most ~9 % together.
- **Every Keccak route that fits needs the team's custom hold setting** (0.14 ns hold uncertainty instead of
  TT's 0.25 ns), except the latch core, which is too big for 2×2 and slow. Tiny Tapeout's acceptance of that
  setting is the open question to settle first.

## 2. The branches

| Branch | What it is | Tile | Tests (re-run here) | Hardened |
|---|---|---|---|---|
| `main` (acff283) | NTT core, full-rate basemul / scale, 50 MHz timing closure | 2×2 | team CI green | **measured**: 52.5 %, +1.41 ns slow corner, clean |
| `sha3-4x2` | NTT core + `keccak_ring` K = 8 (clock-gated shift rings, 368 clocks / permutation), hash words on the existing pins, SKIP command | 4×2 | hash tests pass (K = 8, through the pins) | team: default constraints fail (1,590 hold buffers, routing overflow); with the 0.14 ns SDC it routes clean but misses 50 MHz by 1.55 ns (fix in progress on `sha3-asic-research`) |
| `sha3-4x2-cache` | + NTT block engine (`ntt_blk`): NTT in 1,010 clocks instead of 2,751 | 4×2 | team: block-mode tests pass | team: does not fit next to the K = 8 core in 4×2 (CTS 90.7 %), nor in 6×2 with default constraints |
| `sha3-latch` | NTT core + `keccak_lat` (1,600 latches, 1,464 clocks / permutation), default hold constraints | 4×2 | hash tests pass (latch, through the pins) | team: 4×2 with every TT default fails in placement (95 %); with small cells being hardened |
| `sha3-asic-research` | the study: ring core, harden experiments, models, Spec 6 draft, discord question | 3×2 | ring core tests pass K = 8 / 4 / 2 and with enable flops | team: 3×2 NTT + ring K = 8 with the 0.14 ns SDC in detailed routing |
| `feature/multiplier`, `roberts/mod_adder` | early 1×1 prototypes of the multiplier and the adders | 1×1 | – | superseded by `modq_mul` / `modq_addsub` in `main` |

## 3. The tile matrix (measured here)

Every variant was built from its branch, tested at the pins, and hardened with the TT flow (LibreLane, via
tt-support-tools). "relaxed" is the team's research recipe: 0.14 ns hold uncertainty, hold margins
0.03 / 0.02, small cells allowed, `clkdlybuf4s` delay cells banned, density 75. "default" is TT's config as
is. Core area: 2×2 = 72.6k µm², 3×2 = 110.9k µm².

| Variant | Tile | Flow | Pin tests | Synth (TT flow) | Result | Util. | Setup, slow corner |
|---|---|---|---|---|---|---|---|
| NTT only (`main`) | 2×2 | default | team CI | 31.8k | **fits, clean** | 52.5 % | +1.41 ns |
| SHA-only ring K = 2 | 2×2 | relaxed | 24/24 hash jobs | 38.3k | **fits, clean** | 65.1 % | +9.93 ns |
| SHA-only ring K = 4 | 2×2 | relaxed | 24/24 | 41.1k | **fits, clean** (0 DRC / LVS / antenna, hold +0.024 ns) | 71.4 % | +7.11 ns |
| SHA-only ring K = 8 | 2×2 | relaxed | 24/24 | 47.0k | **does not route**: 18,931 → 9,660 violations in 5 router iterations (60 min), stopped | | |
| SHA-only ring K = 8 | 2×2 | default | 24/24 | 57.9k | **does not fit**: 82.7 % after placement, detailed placement fails | | |
| SHA-only latch | 3×2 | small cells | 24/24 | | **stalls**: 75 min in post-CTS timing repair without progress, stopped | | |
| **Hash-only chip K = 8 + CBD** (`sha3-only`) | 2×2 | relaxed | 4/4 tests (43 jobs) | 48.6k | **does not route**: global routing needed overflow passes, detailed routing started at 23,058 violations; stopped | 83.6 % after CTS | |
| **Hash-only chip K = 8 + CBD** (`sha3-only`) | 3×2 | relaxed | 4/4 tests | 48.6k | **fits, clean** (0 DRC / LVS / antenna, hold +0.018 ns) | 55.5 % | +8.17 ns |
| Hash-only chip K = 16 + CBD | 3×2 | relaxed | 4/4 tests (K = 16) | 60.8k | **does not route**: 32,578 violations after 1 h 40 min, stopped | 69.2 % after CTS | |
| NTT + ring K = 8 | 3×2 | relaxed | team suite | | team run in progress | | |

Notes:
- **Pin tests:** the K-general hash test (`fpga/sim_variant`) runs all four SHA-3 / SHAKE functions at lengths
  0, 1, rate−1, rate, rate+1 and 200, with and without SKIP, through the pins. All 24 jobs are bit-exact
  against hashlib for every variant. The team's own `test_hash.py` assumes K = 8, so it can't check K = 4 / 2.
- **Slew:** every finished run reports max-slew warnings at the slow corner (`main` 1,242, SHA K = 2 598, SHA K = 4 1,008).
  They don't stop the flow, but they're worth a look before tapeout.
- **Memory:** a 15 GB machine runs at most three hardens at once. Two runs crashed in detailed routing when
  eight ran together and were repeated.

## 4. Speed of each route (model)

ML-KEM-512, one exchange (keygen + encaps + decaps), RP2350 at 150 MHz, chip at 50 MHz, host waits for the
chip. `sim/route_model.py` extends the team's `research/sha3/mlkem_model.py` (same hash calls, chip clocks and
setup) with:
- the K = 2 and latch cores;
- the NTT in software or on the chip;
- the RP2350's cost to feed the hash words.

| Route | Feed | Exchange | vs RP2350 alone |
|---|---|---|---|
| RP2350 alone | – | 8.08 ms | 1.00× |
| NTT chip (`main`) | firmware as written | 22.9 ms | 0.35× |
| NTT chip (`main`) | optimized | 8.88 ms | 0.91× |
| **SHA-only ring K = 8** | optimized | **3.47 ms** | **2.33×** |
| SHA-only ring K = 8 | as written | 6.06 ms | 1.33× |
| SHA-only ring K = 8, public inputs only | optimized | 5.30 ms | 1.52× |
| SHA-only ring K = 4 | optimized | 4.52 ms | 1.79× |
| SHA-only ring K = 2 | optimized | 6.61 ms | 1.22× |
| SHA-only latch | optimized | 5.20 ms | 1.55× |
| NTT + ring K = 8 | optimized | 4.27 ms | 1.89× |
| NTT + latch | optimized | 6.00 ms | 1.35× |
| software only (keep Â, both cores) | – | 5.50 ms | 1.47× |
| **Hash-only chip K = 8 + burst + CBD** (`sha3-only`) | optimized | **3.18 ms** | **2.54×** |
| Hash-only chip K = 8 + burst + CBD | as written | 4.57 ms | 1.77× |
| Hash-only chip K = 16 + burst + CBD | optimized | 2.87 ms | 2.82× |
| Hash-only chip K = 16 + burst + CBD | as written | 4.24 ms | 1.91× |

- **Feed costs.** "Optimized" means ~2 CPU cycles per hash word in and per byte out: the PIO inserts the flag
  bits and DMA moves raw bytes (estimate). "As written" means ~23 and ~15, from the firmware's compiled loops
  (`docs/ESTIMATES.md` in tt-viktor-chiptor). The team's model assumes the feed is free, which is why its
  K = 8 number is 3.17 ms.
- **Overlap.** The team's overlap model, where the host works while the chip runs, puts SHA-only K = 8 at
  2.82 ms with one core and 1.87 ms with two.
- **What bursts and CBD buy.** A burst squeezes a whole block with one host word instead of one word per
  byte, and CBD takes the PRF's sampling (2 % of the profile, its share of the non-hash time) off the RP2350.
  With the firmware as written that is 6.06 → 4.57 ms; with the optimized feed 3.47 → 3.18 ms.
- **Why the NTT core loses.** On the chip an NTT is pin-bound: 55 µs of streaming, plus the host's packing.
  The RP2350's own NTT takes 40 µs. Keeping the NTT core costs area and speed.

## 5. The ML-KEM-512 chain: what else could become gates (measured)

PQClean's clean ML-KEM-512, compiled for the Cortex-M33 (`-O2 -fno-inline`, so every instruction lands in the
function that does the work), one exchange run under an instruction-level emulator, keys matching
(`tools/mlkem_profile`):

| Part of the chain | Instructions | Share | As gates? |
|---|---|---|---|
| SHA-3 / SHAKE (Keccak) | 1,452,794 | 57.5 % | **yes: the hash engine** |
| NTT, inverse NTT, basemul | 648,333 | 25.6 % | on the chip it is pin-bound and slower than the RP2350's own (assembly) NTT: 55 µs vs 40 µs per call |
| polynomial add / sub / reduce | 201,240 | 8.0 % | no: one pass over host data, the pins cost more than the work |
| memcpy / memset | 56,971 | 2.3 % | no |
| CBD sampling | 49,746 | 2.0 % | **yes, done in `sha3-only`**: fused after the PRF squeeze, fixed rate (the burst plus CBD adapter costs 1.6k µm² in the TT flow) |
| rejection sampling (SampleNTT) | 42,095 | 1.7 % | **no**: fused after the XOF squeeze it would be 3.3k µm² (synthesized), but its 12-bit output has a variable rate; without a handshake pin the chip can't return it compacted, so the host would still unpack and check every candidate |
| compress / decompress | 35,947 | 1.4 % | no: host data |
| encode / decode | 29,569 | 1.2 % | no: host data |
| FO compare + select | 8,467 | 0.3 % | no |

- **What this says.** The clean C puts 57.5 % in Keccak. pqm4's optimized code puts 67–76 % there, because its
  NTT is assembly. Either way, after Keccak and the NTT, the rest is many small steps on data that lives on
  the RP2350. Any gate for them pays the pins: 1 byte in and 1 out per clock.
- **The two that don't pay the pins.** CBD and SampleNTT already sit on the hash engine's output, and they
  remove host work. Together they could save at most ~0.3 ms per exchange (about 9 % on the SHA-only K = 8
  route; estimate, scaling the profile to pqm4's non-hash time).
- **Done.** CBD is in the hash-only chip (section 7). SampleNTT stays in software (row above).

## 6. What it would take

1. **Ask Tiny Tapeout whether a custom SDC with 0.14 ns hold uncertainty is acceptable for a shuttle
   project.** Every ring-core route depends on it. Without it, only the latch core hardens, and the latch
   core doesn't fit 2×2.
2. **Pick the route.**
   - Fastest overall: the hash-only chip (`sha3-only`), K = 8 + CBD, in **3×2**, hardened clean
     ([sha3_only_3x2.md](sha3_only_3x2.md)). K = 16 would be faster still (2.87 ms) but doesn't route in 3×2.
   - In 2×2 only the slower cores fit: K = 2 (clean) and K = 4 [section 3].
   - Keeping the NTT core next to a hash engine needs 3×2 or 4×2, and it is slower.
3. **The firmware.** Hash calls in the RP2350 harness, a PIO program that inserts the hash flag bits, the
   per-call secret policy (all calls, or public-input calls only), a known-answer self-test at boot and a
   software fallback. Without the PIO feed the speed-up mostly disappears.
4. **The rig.** Bitstreams for the hash-only, SHA-only and NTT + hash variants build for the Tang Nano and
   the Pico 2 W rig, with the ring core in its enable-flop form, which is verified equivalent in the core
   test. The ring core's sky130 clock gates can't run as gates on the FPGA, so its gate-level netlist is
   checked in simulation only. Rig results: section 8.

## 7. The hash-only chip (branch `sha3-only`)

The NTT core and its tests are removed. What is left: `io_in_capture` → **`hash_io`** (new) → `keccak_ring`
(K = 8, 368 clocks per permutation). Commands are the `sha3-4x2` hash words, so existing host code still runs,
plus two new ones:

| cmd | Command | Data | What the host gains |
|---|---|---|---|
| 0–4 | Absorb, Squeeze, Permute, Clear, Skip | as on `sha3-4x2` | – |
| 5 | **Burst**: the next 8n state bytes on `uo_out`, one per clock | n (1..21) | one word per block instead of one per byte |
| 6 | **Burst CBD**: the same bytes as SamplePolyCBD coefficients (4-bit two's complement) | {η3, 2'b0, n} | PRF calls return ML-KEM's secret / noise coefficients; no CBD in software |

- **CBD output.** η = 2: one byte gives `{f[2i+1], f[2i]}`. η = 3: three bytes give `{0, f[4j]}`, `{0, f[4j+1]}`,
  `{f[4j+3], f[4j+2]}`. PRF_3 needs 192 bytes, more than SHAKE256's 136-byte rate, and 136 is not a multiple
  of 3. So the η = 3 grouping carries on across the Permute between the two bursts, and restarts only at Clear.
- **K = 16.** `hash_io` pairs bytes for a K = 16 core, so the host protocol stays the same and only the busy
  times change (PERMUTE 172, CLEAR 4). `keccak_ring`'s step counter was generalized for K = 16. The core test
  passes at K = 16 (permutation + every algorithm, measured).
- **Config.** `src/config.json` carries the team's relaxed recipe (0.14 ns hold SDC, small cells, no
  `clkdlybuf`). `info.yaml` is set to 3×2, where it signs off clean (section 3). The top module keeps the
  name `tt_um_NTT_Core` until the team picks a new one.
- **Tests (measured, RTL).** `test/test_hash_only.py` uses `test/hash_jobs.py` (the same file the FPGA host
  tools use):
  - **K = 8:** 4/4 tests pass:
    - `test_hash_algos`: 24 jobs, byte squeeze, every algorithm at the rate edges, with and without Skip;
    - `test_hash_bursts`: 12 jobs;
    - `test_cbd`: 6 PRF jobs, η = 2 and 3, against FIPS 203 SamplePolyCBD;
    - `test_quiet`: `uo_out` is 0 whenever no byte is due, and hash words during a burst are ignored.
  - **K = 16:** `HASH_K=16 make`, the same 4/4 tests pass. The same jobs take 648.6 µs of chip time at 50 MHz,
    against 1,020.6 µs at K = 8 (36 % fewer clocks).
  - **Lint:** Verilator `-Wall` is clean.
- **Area (measured, TT flow).** 48.6k µm² synthesized, against 47.0k for the plain SHA-only K = 8 top. The
  burst and CBD helper costs 1.6k µm².

## 8. The Pico 2 W + Tang Nano rig (measured)

- **Wiring check.** `tools/rig_pincheck.py` uses a pin-echo bitstream: each wire is driven both ways and the
  clock and reset are checked. **All 26 signal wires pass at 1, 4.5 and 13.5 MHz** (8 `ui_in`, 8 `uio`,
  8 `uo_out`, clk, rst_n).
- **Hash-only chip on the rig.** Pico build: 2,025 LUT4, 1,745 DFF, DUT logic up to 144 MHz on the GW2AR-18.
  `host/test_variant.py --hash 8 --hash-only` runs 42 jobs through the Pico (24 byte-squeeze, 12 burst,
  6 CBD): **42/42 bit-exact at 1.04, 3.38, 6.75 and 13.5 MHz**, the top of the rig's clock range, and `uo_out` is 0 on every clock where no byte is due. All 48,676 clocks take 1.9 s through the TTE1 bridge.
- **Tang Nano USB alone.** The same chip in the USB-UART build (Tang Nano without the Pico) also passes
  **42/42** (3.3 s). After the first re-plug the Tang's on-board debugger had wedged: the UART was silent and
  one JTAG flash hung. Unplugging and replugging the Tang fixed it.

## 9. How long a harden takes on this machine (measured)

LibreLane through tt-support-tools in a podman container, on a 15 GB laptop. The runs repeat the whole Tiny
Tapeout GDS flow with its signoff checks: STA at every corner, magic DRC, netgen LVS, antenna, KLayout
stream-out. Times come from LibreLane's per-step `runtime.txt` (`tools/harden_times.py`):

| Run | Synthesis | Floorplan | Placement | CTS | Routing | Signoff checks | Whole harden |
|---|---|---|---|---|---|---|---|
| NTT only (`main`), 2×2 | 0.3 min | 0.1 min | 0.5 min | 0.4 min | 2.6 min | 1.4 min | **5.4 min** |
| SHA-only K = 2, 2×2 | 0.2 min | 0.1 min | 0.3 min | 0.3 min | 3.8 min | 1.1 min | **5.8 min** |
| Hash-only K = 8 + CBD, 3×2 | 0.8 min | 0.1 min | 0.9 min | 0.7 min | 41.1 min | 1.1 min | **44.8 min** |
| SHA-only K = 4, 2×2 (long routing) | 0.2 min | 0.1 min | 0.4 min | 0.4 min | 82.9 min | 0.8 min | **84.9 min** |
| SHA-only K = 8, 2×2 (does not route) | 0.6 min | 0.1 min | 0.5 min | 0.5 min | > 60 min, stopped | – | – |
| Hash-only K = 8 + CBD, 2×2 (does not route) | 0.8 min | 0.2 min | 0.9 min | 0.8 min | > 40 min, stopped | – | – |
| Hash-only K = 16 + CBD, 3×2 (does not route) | 0.8 min | 0.1 min | 0.3 min | 0.3 min | > 1 h 40 min, stopped | – | – |

- **Uncongested designs finish in 5–6 minutes**, with routing and signoff taking most of it. A GitHub Actions GDS
  run of the same flow took about 12 hours per run in our experience.
- **Fuller designs spend most of their time in detailed routing**: 41 min for the hash-only chip at 3×2,
  83 min for K = 4 at 2×2. Too-congested ones never converge, and memory sets the limit: at most
  three runs at once on 15 GB, with `DRT_THREADS=6`.

## Reproduce (tt-viktor-chiptor)

```bash
python3 tools/tile_exp.py list | run <name> ... | status              # variants, tests, hardens (max 3 at once)
make -C fpga/sim_variant EXP=~/tools/work/exp/sha_k4_2x2 HASH_K=4     # pin-level hash test, any K
python3 sim/route_model.py                                           # section 4
make -C tools/mlkem_profile run                                      # section 5
python3 tools/tile_exp.py run hash_k8_3x2                            # section 7, harden of sha3-only
python3 tools/rig_pincheck.py --port /dev/ttyACM0                    # section 8, wiring
python3 host/test_variant.py --port /dev/ttyACM0 --hash 8 --hash-only  # section 8, chip on the rig
python3 tools/harden_times.py                                        # section 9
```
In `sha3-only`: `cd test && make` (K = 8) or `HASH_K=16 make`.
