# Handoff: SHA-3 (Keccak) on the ASIC

Status as of Oct 17, 2026. This is a research task, not an implementation task yet. Read the
whole note before touching anything.

## The ask

Find a way to put SHA-3 / SHAKE work on our Tiny Tapeout chip that actually makes ML-KEM-512
faster on the demo board, or show clearly that no option fits and say what it would take. End with
a recommendation the user can decide on: tile count, area, clocks per permutation, the expected
ML-KEM-512 speed-up, what stays secret, and what changes in the specs.

**Do not** change `specs/`, `src/` or `main` without the user's go-ahead. Spec changes come after
the analysis is accepted.

## Why this matters

Measured and cited numbers (sources in `docs/sha3_report.md` and `docs/architecture_options.md`):

- On the RP2350 alone, hashing is **67–76 %** of ML-KEM-512's time. The NTT and basemul work our chip
  takes over is only 8–14 %.
- So even an infinitely fast NTT unit gives at most 1.08–1.16×. The chip today gives about **0.91×**
  at 50 MHz (estimate from Viktor's model; it was 0.86× before the full-rate change).
- A hashing unit is the only place a real speed-up can come from: up to 3–4× in the limit.
- One ML-KEM-512 operation runs **26–27 Keccak-f[1600] permutations**. The RP2350 takes about
  **74 µs** per permutation (including absorb and squeeze).
- To beat the RP2350 per permutation at 50 MHz, a core needs fewer than about **3,700 clocks** per
  permutation. For about 1.7× overall, it needs about 1,300–1,500.

## What is already known (don't redo it)

Viktor explored this in [tt-viktor-chiptor](https://github.com/SiLiCaP/tt-viktor-chiptor). Read these
first:

- `docs/sha3_report.md` (in this repo): roles of Keccak in ML-KEM, time split, software-only options,
  the security catch.
- `docs/architecture_options.md` (in this repo): tile budgets, published compact cores, clock sweep,
  the SRAM question.
- `tt-viktor-chiptor/docs/spec-changes/02_hasher_engine.md`: a drafted Spec 6 (`keccak_lane`,
  `sponge_ctrl`), pin protocol for hash jobs (command words on the existing pins), and a fit check.
- `tt-viktor-chiptor/sim/arch_explore_rtl/`: verified Keccak RTL (`keccak_lane.v`, `keccak_lane_lat.v`,
  `keccak_core.v`), `keccak_ref.py`, cocotb tests against hashlib.
- `tt-viktor-chiptor/sim/sha3_options.py` and `arch_explore.py`: the ML-KEM timing model behind every
  "×" number.

His measured results (sky130 hd, Yosys + ABC, synthesized area):

| Keccak-f[1600] core | Area | Clocks / perm. | Tiles (est.) |
| --- | --- | --- | --- |
| Lane-serial, flip-flop state | 151k µm² | 1,346 | ~19 |
| Lane-serial, latch state | 99k µm² | 1,346 | ~13 |
| Latch state + clock gating | ~88k µm² (estimate) | 1,346 | ~11 |
| 1 round per clock | 145k µm² | 24 | ~18 |
| Published compact (Pessl & Hutter 2013 / Keccak team) | ~21k / ~35k µm² (GE conversion) | ~15,000 / 5,160 | fit, but slower than the RP2350 |

His conclusion: the 1,600-bit state and its access paths (write enables, a 25:1 lane read mux) set the
area, not the round logic. Every core small enough for 8 tiles is slower than the RP2350; every core
faster than it needs about 11+ tiles.

## Budget and constraints

- **Tiles.** Tiny Tapeout sizes: 1x1, 1x2, 2x2, 3x2, 4x2, 6x2, 8x2 (16 tiles max). One tile is about
  167 × 108 µm. We use 2x2 today (`info.yaml`). More tiles cost the team money and need agreement;
  ask the user before assuming a size.
- **Area rule of thumb (measured on our chip).** Placed cell area ≈ 1.6 × synthesized area, and placement
  tops out near 70 % density. Our current 2x2 build has 38.1k µm² of placed cells at 52.5 % utilization
  (72.6k µm² core). Viktor's estimates: about 32k µm² of synthesized logic fits 2x2, about 63k fits 4x2.
  Scale up the same way for 6x2 and 8x2, and check by hardening.
- **The NTT chip stays.** It is verified and meets 50 MHz at the slow corner (+1.41 ns post-PnR, after
  the Oct 17 changes). Any hash engine sits next to it and must not break it. Current NTT chip: about
  23–24k µm² synthesized, 38.1k µm² placed.
- **Clock.** 50 MHz is the guaranteed point (slow corner). 66 MHz is the pad limit and only works on
  typical silicon. Plan around 50 MHz.
- **Pins.** 8 inputs, 8 outputs, 8 bidirectional (today all used as inputs), no handshake, fixed
  delays (Spec 5). Hashing I/O is small (about 1.1 kB in and 2.7 kB out per operation), so the pins are
  not the problem for hashing.
- **Security.** Many hash calls take secrets: the seed d (keygen), σ and r (PRF), m (encaps), z (decaps).
  On the pins they leak in the clear, and Keccak is not linear, so the host can't mask them the way it
  masks the NTT. Public-input calls are safe: XOF from ρ (expanding Â) and H of the public key. That is
  18 of 27 permutations in keygen and 12 of 26 in decaps. A masked Keccak costs a multiple of the
  unmasked area.
- **Rules.** Synchronous logic, one clock (`clk`), synchronous active-low `rst_n`, no generated clocks
  (shared rules in every spec). Latches are against the current shared rules and complicate signoff in
  the Tiny Tapeout flow; any latch-based plan needs the user's explicit approval and a working STA story.

## Leads worth investigating

Unverified ideas. Measure before believing any of them.

1. **State as a plain shift register.** Much of the measured area is write-enabled flip-flops plus the
   25:1 read mux. If the state rotates through the datapath (slice- or lane-serial with the state
   shifting every clock), each bit can be a plain flip-flop with no enable and no mux. A sky130
   `dfxtp_1` is about 20 µm², so 1,600 bits ≈ 32k µm² before logic, against about 92k µm² for the
   enabled flip-flops alone in Viktor's core. Check the published slice-oriented designs (for example
   Jungk and Apfelbeck, ReConFig 2011, and similar slice-serial ASIC work) for their clock counts:
   about 64 slices × 24 rounds plus the θ column-parity handling. The target is a core under
   ~3,700 clocks that fits in ~35–40k µm² synthesized. If that works, NTT chip + hasher might fit in
   4x2 or 6x2.
2. **Public-input calls only.** Offload just XOF (SHAKE128 for Â) and H(ek). No secrets on the pins.
   Viktor estimates 1.34× with his core; redo the estimate with whatever core lead 1 gives. Doing
   SampleNTT's rejection sampling on the chip is also public and could save host time; check the cost.
3. **SRAM or a RAM macro for the state.** The state is only 200 bytes. Ask (or find out) whether a
   Tiny Tapeout project can instantiate a sky130 SRAM macro, or a DFFRAM / OpenRAM block, and at what
   size and tile cost. Recent shuttles carried standalone SRAM test projects (TTSKY-SPSRAM, 128 × 8),
   but there is no evidence yet that a user project can use one. This is a question for the organizers
   (Discord); draft the question for the user rather than posting it.
4. **Where a bigger tile budget gets us.** For 4x2, 6x2 and 8x2: which core fits next to the NTT chip,
   and the ML-KEM-512 number for each. One table the user can take into the tile negotiation.
5. **Software baseline.** Viktor's software-only estimate (keep Â from keygen, hash on both M33 cores)
   is about 1.47× with no silicon. Any hardware option has to be compared with that, not with 1.0×.

## How to measure here

- **Repo:** `/home/papart/development/TT/chipathon-2026` (team repo, branch `main`). Read
  `local-ci/README.md`.
- **Full flow, trustworthy numbers:** `local-ci/run gds --no-serve` runs LibreLane 3.0.14 like the
  GitHub workflow. Post-PnR slack is in `runs/wokwi/final/metrics.json`
  (`timing__setup__ws__corner:max_ss_100C_1v60`, `design__instance__area__stdcell`,
  `design__instance__utilization__stdcell`). This is the only number to trust for fit and timing.
- **Quick synthesis of a block:** run LibreLane in its image up to `Yosys.Synthesis` on a small config
  (`DESIGN_NAME`, `VERILOG_FILES`, `CLOCK_PORT`, `CLOCK_PERIOD` 20), then OpenSTA on the netlist with the
  sky130 hd ss/tt liberty files from `~/.cache/tt-local-ci/pdk/sky130A`. Wrap a combinational block in
  input/output registers, and subtract a pass-through wrapper's area.
- **Docker gotchas found the hard way:**
  - Nested containers can't see the session scratch directory under `/tmp`. Put experiment copies
    under `/home/papart/development/TT/`.
  - `git worktree` copies don't work with local-ci (Tiny Tapeout's tools can't resolve `HEAD`). Use
    `git clone` of the repo plus a copy of `tt/`.
  - Two `local-ci/run` invocations started at the same moment race on the shared build context.
    Start them about 90 s apart.
  - The LibreLane image has no `sed`/`awk`; use Python. There is no C++ compiler for Verilator builds;
    use iverilog for big simulations.
- **ABC noise.** Standalone results move ±5 % with trivial RTL rewrites. Compare candidates inside the
  full chip or by hardening, not by one standalone synthesis.
- **Exhaustive checks** of Keccak against `hashlib` (`sha3_256`, `shake_128`, …) are cheap in cocotb;
  Viktor's tests show the pattern.

## Team and process rules

- **Spec is king.** Specs in `specs/` are the source of truth. Changes go through the user.
- **Commits:** author `Artjoms Papšs <artjompapsh2003@gmail.com>`
  (`git -c user.name="Artjoms Papšs" -c user.email=artjompapsh2003@gmail.com commit …`). No co-author
  trailers or session links in commits, pushes or PRs. Commit or push
  only when the user asks.
- **Owners.** Spec 1 Pi harness (`rp2350/`, Viktor's area), Spec 2 multiplier (Maksims), Spec 3 Control
  and integration (the user), Spec 4 adders (Roberts), Spec 5 I/O (Nikita). A hash engine would be a new
  Spec 6 with an owner the team picks.
- **Future work already planned (not now):** on-chip storage of coefficients to get past the pin limit.
  If a RAM macro becomes possible (lead 3), it helps both that and the Keccak state.

## Deliverable

A report (`docs/sha3_asic_options.md` or similar, not committed until the user says so) with:

1. Candidate cores: area (measured in sky130), clocks per permutation, timing at 50 MHz slow corner.
2. Fit per tile size (2x2, 4x2, 6x2, 8x2) with the NTT chip alongside.
3. ML-KEM-512 exchange time and speed-up for each option, against both 1.0× (RP2350 alone) and the
   1.47× software-only baseline, using `sha3_options.py`'s model or an updated one.
4. What crosses the pins in each option and whether it is secret.
5. A recommendation, the spec changes it would need (Spec 6 plus changes to 1, 3 and 5), and the open
   questions for the organizers.
