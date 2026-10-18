# SHA-3 on the ASIC: options and recommendation

Status: draft, Oct 18 2026. Research branch `sha3-asic-research`. Nothing here is in `specs/` or `src/` yet.
Every number is tagged **measured** (RTL simulation, synthesis or LibreLane hardening on this machine),
**model** (`mlkem_model.py`, `overlap_model.py`) or **estimate**.

## 1. Result in one paragraph

A new Keccak-f[1600] core, `keccak_ring`, holds the whole 1,600-bit state in plain shift flip-flops
(200 rings of 8). It does ρ by clock gating, and π is wiring. A permutation takes **368 clocks**
(7.4 µs at 50 MHz, against about 74 µs on the RP2350). It is verified bit-exact against hashlib for
SHA3-256, SHA3-512, SHAKE128 and SHAKE256 (**measured**). With the NTT chip it synthesizes to about
69k µm² and places at 69 % of a **3x2** (6-tile) core (**measured**). With the chip doing all Keccak
and all NTT work, an ML-KEM-512 exchange is **2.3–3.2× faster** than the RP2350 alone (**model**).

## 2. The core: `keccak_ring.v`

- **State.** 25 lanes × K sub-rings × 64/K flip-flops. Sub-ring b of lane L holds bits z = K·i + b.
  Every state bit is a plain `dfxtp`: no enable, no reset, no mux.
- **Round.** A ρ phase (M−1 clocks) rotates each ring through its own clock gate (47 `dlclkp` gates
  for K = 8). A pass (M clocks) reads K slices per clock: π and ρ's sub-ring permutation are fixed
  wiring. The pass applies χ, ι and the next round's θ, and writes the result back into the rings.
  A θ-only pass starts each permutation.
- **Clocks per permutation (measured):** K = 8: 368; K = 4: 760; K = 2: 1,544.
- **Host interface:** one command word per clock:

  | Command | What it does |
  | --- | --- |
  | ABSORB | XOR K bits into the state |
  | SQUEEZE | read K bits out |
  | PERMUTE | run Keccak-f[1600] |
  | CLEAR | zero the state (M clocks) |
  | SKIP | pass over an all-zero lane in 1 clock |

  The host pads and works in whole lanes.
- **Why it is small.** Published compact cores and Viktor's lane-serial core spend their area on
  write-enabled state bits and a 25:1 lane mux (151k µm² for 1,346 clocks). Here a state bit costs
  one `dfxtp` (20 µm²), and the per-bit mux (+15k µm², **measured**: enable-flop variant) is replaced
  by 47 clock gates.

| Core (plain Yosys + ABC, sky130 hd) | Area | Clocks/perm. |
| --- | --- | --- |
| keccak_ring K = 8, clock gates | 47.0k µm² | 368 |
| keccak_ring K = 4, clock gates | 41.4k µm² | 760 |
| keccak_ring K = 2, clock gates | 38.6k µm² | 1,544 |
| keccak_ring K = 4, enable flops (no gates) | 56.7k µm² | 760 |
| Viktor's lane-serial, flip-flop state | 151k µm² | 1,346 |
| `keccak_lat`, latch-array state, 64-bit lane-serial (branch `sha3-latch`, §7) | 74.4k µm² | 1,464 |

## 3. Fitting 3x2 (6 tiles, free): what it took (measured)

1. **Small cells.** The Tiny Tapeout flow excludes all `_1` cells. Allowing `dfxtp_1`, `xor2_1`,
   `xnor2_1` and the simple `_1` gates through a custom `SYNTH_EXCLUDED_CELL_FILE` takes NTT + K = 8
   from 88.7k to **69.3k µm²** synthesized. With the default list, detailed placement fails.
2. **Hold on shift rings.** The default SDC applies 0.25 ns clock uncertainty to setup *and* hold.
   A ring hop (Q → D, no logic) has about 0.18 ns of real hold slack at the ff corner, with
   neighbouring sinks about 0.01 ns apart after CRPR. So the default puts a hold buffer on every hop:
   1,400–1,700 buffers, about 13k µm², and 117–174 % global-routing usage. That does not route in
   3x2 (4-slice with defaults: 155 %).
   - With a custom SDC (`set_clock_uncertainty -hold 0.14`, setup stays 0.25) the resizer inserts
     **0–2 hold buffers**.
   - Global routing then drops to **65.8 %** (K = 8) / **67.3 %** (K = 4).
3. **How close 0.14 ns is to the edge (3x2 sweep, Oct 18).** Same build as `exp_k8_t2`, only the hold
   uncertainty changed:

   | Hold uncertainty | Hold buffers | Global routing | Result |
   | --- | --- | --- | --- |
   | 0.14 ns | 7 | 69.6 % | in detailed routing |
   | 0.16 ns | 819 | 96.6 % | in detailed routing, unlikely to finish clean |
   | 0.18 ns | 1,319 | 142.5 % | fails (antenna repair) |
   | 0.20 ns | 1,373 | 150.2 % | stopped, worse than 0.18 |

   The ring core only routes in 3x2 at 0.14 ns or less. There is no less aggressive setting to fall
   back on. In the 4x2 signoff with the 0.14 SDC (`exp_4x2_hu14m`), the worst hold path at
   ff/−40 °C/1.95 V had +0.016 ns slack, so about 0.16 ns with no uncertainty. Under the default
   0.25 ns it would fail by about 0.09 ns. A hold failure can't be fixed by slowing the clock, so the
   firmware self-test and software fallback (§6) matter.
4. **Placement density doesn't help.** Utilization after global placement is already 69.7 %, so
   targets of 65 % and 70 % are both raised to the same floor (byte-identical placements). Global
   routing came out at 74.6 %, worse than the 69.6 % at 75 %.
5. **Status of signoff:** detailed routing, then DRC/LVS/STA, precheck and gl_test. *Pending, see §8.*

## 4. ML-KEM-512 speed (model)

RP2350 alone: 8.08 ms per exchange (keygen + encaps + decaps). pqm4 m4fspeed at 150 MHz (cited).

Host waits for the chip (`mlkem_model.py`):

| Option | Exchange | vs RP2350 |
| --- | --- | --- |
| K = 8, all hashing on chip | 3.17 ms | 2.55× |
| K = 8, public inputs only (XOF, H(ek)) | 5.10 ms | 1.58× |
| K = 4, all hashing on chip | 4.04 ms | 2.00× |
| software only (Viktor's estimate: Â reuse + 2 cores) | 5.50 ms | 1.47× |

Host overlaps work with the chip (`overlap_model.py`, list scheduling). **NTT always on the chip:**

| Keccak core | Pi cores | NTT packing | Exchange | vs RP2350 |
| --- | --- | --- | --- | --- |
| K = 8 | 1 | today (12 cyc/butterfly) | 3.49 ms | 2.31× |
| K = 8 | 1 | PIO inserts flags (3 cyc) | 3.09 ms | 2.62× |
| K = 8 | 2 | PIO inserts flags | 2.50 ms | 3.24× |
| K = 4 | 1 | today | 4.46 ms | 1.81× |
| K = 4 | 2 | PIO inserts flags | 3.49 ms | 2.32× |

The streaming NTT core alone is pin-bound (55 µs per NTT, against 40 µs in software), so full
offload costs 10–20 % against the fastest split (NTT in software). That price buys a chip that runs
all of ML-KEM's heavy math. The pin-traffic fix is an on-chip coefficient buffer (§7).

## 5. What crosses the pins

| Call | Input | Secret? |
| --- | --- | --- |
| XOF (Â) | ρ | public |
| H | ek | public |
| G | d (keygen), m ‖ h (encaps), m′ ‖ h (decaps) | **secret** |
| PRF | σ, r | **secret** |
| J | z ‖ c | **secret** |
| NTT, basemul | polynomials | maskable by the host (NTT is linear) |

Keccak cannot be masked on the host. The firmware chooses per call: everything on the chip, or
public inputs only. That is a deployment decision, not a silicon one.

## 6. For real use (the user's homeserver messenger)

- **Hybrid key exchange** (X25519 + ML-KEM), so a chip bug alone can't break security.
- A **known-answer self-test at boot**, a **pairwise-consistency check after keygen**, and
  **software fallback** if either fails. ML-KEM decaps re-encrypts and compares, so a fault gives
  an implicit rejection, not a leak, in normal operation.
- Both cores are **constant-time** (fixed clock counts). Randomness comes from the RP2350 TRNG.
- No power/EM side-channel hardening, on the chip or in RP2350 software.

## 7. Open options

- **4x2 with Tiny Tapeout's default constraints does not harden** (agent branch `sha3-4x2`,
  **measured**). The resizer adds 1,590 hold buffers, global routing reaches 108 %, and antenna repair
  fails (DPL-0036). With `CLOCK_GATING = 0` it is worse (1,860 buffers). So more tiles do not remove the
  need for the custom hold uncertainty. The ring hops need it at any size.
- **4x2 with the 0.14 ns SDC and hold margins 0.03 / 0.02** (**measured**): every job passes, including
  precheck and gl_test, with 0 DRC/LVS/antenna, 0 hold buffers and 59 % routing. But setup at the slow
  corner is −1.55 ns (max_ss). That is the NTT twiddle path `coeff_q[10] → zeta_r`. Two things make it
  slow:
  - the hash-word decode (and4 + nand3b, about 1.2 ns) sits at the front of the path;
  - the resizer used `clkdlybuf4s25_1` *delay cells* as fanout buffers (272 of them, 1.0–1.6 ns
    each, five on this path).

  The NTT alone in 2x2 has +1.41 ns at max_ss. Fix being hardened (`exp_k8_t2`, 3x2):
  - the hash flag and the masked NTT valid/sof are registered straight from the pins;
  - `EXTRA_EXCLUDED_CELLS` bans the clkdlybuf delay cells from synthesis and PnR.
- **NTT cache study** (agent branch `sha3-4x2-cache`, **measured** unless marked). A block engine
  (`ntt_blk`, LF variant, 564 bits of storage) cuts an NTT from 2,751 to 1,010 clocks (20.2 µs, 2×
  faster than RP2350 software). RTL tests pass. It does **not** fit next to the K = 8 Keccak core:
  - LF fails at CTS in 4x2 (90.7 %);
  - LF fails in 6x2 with the default constraints (2,337 hold buffers);
  - C-only (192 bits) does not route in 4x2.

  ML-KEM gains little. The exchange goes from 3.09 to 2.56 ms with 1 core, and from 2.50 to 1.98 ms
  with 2 cores (**model**). With 2 cores, the software NTT is already as fast as any chip NTT.
  Recommendation: no cache this shuttle. Keep `ntt_blk` on its branch for a later chip.
- **Latch-array Keccak, `keccak_lat`** (branch `sha3-latch`, **measured** unless marked; details in that
  branch's `docs/keccak_lat.md`). The state sits in 1,600 `dlxtp` latches written through one clock
  gate per lane, so there are no shift hops and no need for the custom hold SDC. pi is an address
  rename: the 24 non-zero lanes form one pi cycle, so the mapping is the identity again after 24 rounds.
  Same ports and commands as `keccak_ring`.
  - Core tests and all 16 project tests pass, bit-exact against hashlib.
  - 74.4k µm² plain synthesis (ring 47.0k); 1,464 clocks per permutation (ring 368).
  - ML-KEM-512: 4.90 ms = 1.65× vs the RP2350 (**model**; ring 2.55×, software only 1.47×).
  - Does not fit 3x2 next to the NTT. 4x2 with every TT default fails in placement (122.1k µm²
    synthesized, 95.2 % utilization). 4x2 with the small-cell list and default hold settings is being
    hardened (`exp_lat_4x2s`).
  - The latches and their read port alone take about 35k µm², so a narrower datapath would not get
    near the ring core. Worth it only if the custom hold SDC is ruled out, and then in 4x2 (paid).
- **Splitting into two TT projects** (NTT 1x2 + Keccak 2x2) does not work. The NTT alone fills about
  50 % of a 2x2 core (36.7k µm² placed), more than a 1x2 holds. Only one TT project is connected to the
  pins at a time, so the two cores could not overlap and the host would switch the mux many times per
  exchange. The hold issue is the same in a 2x2 Keccak project.
- **Area trims if needed:** the θ-init parity tree and the 21:1 squeeze mux (about 2.5k), CLEAR gating (about 1k).

## 8. Signoff results

*To be filled in from the finished runs: final metrics, DRC/LVS/antenna, setup/hold per corner,
precheck, gl_test.* State on Oct 18:

| Run | Setup | State |
| --- | --- | --- |
| `exp_k8_t2` (candidate) | 3x2, ring K = 8, small cells, 0.14 SDC, margins 0.03 / 0.02, new top | Detailed routing, violations 17,961 → 509 |
| `exp_k8_hu05` (comparison) | 3x2, 0.05 margins, old top | Detailed routing, violations 15,197 → 249 |
| `exp_k8_t2_d65` | as `exp_k8_t2`, density 65 | Detailed routing |
| `exp_k8_t2_h16` | as `exp_k8_t2`, hold 0.16 | Detailed routing |
| `exp_lat_4x2s` | 4x2, `keccak_lat`, small cells, default hold | Placement / CTS |

After signoff, a hold audit on the final layout: rerun STA with the default 0.25 ns and with 0, and
count the paths that depend on the custom value.

## 9. Spec changes needed (draft)

- **Spec 6 (new): Keccak sponge core.** The ports and commands above; fixed latencies (PERMUTE 368,
  CLEAR 8, squeeze byte 1 clock after the command); bit-exact against FIPS 202; clock gates allowed
  in this block only.
- **Spec 5 (I/O).** A hash word is valid = 1 with coeff[11:8] = 0xF (never a coefficient).
  {sof, op} = command, coeff[7:0] = data. uo_out = NTT bytes OR hash byte. Pins unchanged.
- **Spec 3 (Control / integration).** Hash words never reach `ntt_core` (valid and sof are masked).
  The shared rules gain the clock-gate exception, the custom hold uncertainty, and the small-cell
  exclusion list, each with its reason.
- **Spec 1 (Pi harness).** Host sponge calls (pad10*1, whole lanes, SKIP for zero lanes), the timing
  constants, the per-call secret policy, self-test and fallback, the PIO-inserted flags, and
  overlap scheduling.
- **info.yaml.** `tiles: "3x2"`.
