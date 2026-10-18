# Handoff: SHA-3 on the ASIC, round 2

Status as of Oct 18, 2026. This follows `docs/handoff_sha3.md` (the original ask). Read this first,
then `sha3_asic_options.md` (the report) in this folder. Check the live runs with `./status.sh`.

## Where we are in one paragraph

A new Keccak core, `keccak_ring.v` (K = 8, clock-gated shift rings, 368 clocks per permutation), is
verified bit-exact against hashlib and sits next to the NTT core in the integrated top
(`top/tt_um_NTT_Core.v`). It fits **3x2** (6 tiles, free) only with three non-default flow settings:
small cells, a custom SDC with hold uncertainty 0.14 ns, and hold margins 0.03 / 0.02. With the
defaults it does not harden in 3x2 or 4x2 (one hold buffer per ring hop, routing fails). Two 3x2
hardens are in detailed routing; neither has signoff numbers yet. Model speed-up for ML-KEM-512, with
the NTT core used: 2.3–3.2× against the RP2350 alone (8.08 ms per exchange).

## The user's goals and rules (keep applying)

- **Real use:** the chip will secure the user's homeserver messenger. It must actually work, not just
  pass a demo. Stability beats peak speed.
- **The NTT core must be used.** The result only has to be clearly faster than the RP2350 without the
  chip. Don't recommend dropping the NTT, even where software is faster.
- **Tiles:** up to 6 (3x2) are free; 8 and more cost money, unless the organisers waive it for a working
  design that is meaningfully faster than the Pi.
- **Clock gating is approved** by the user. The user worries most about the hold uncertainty: report its
  margins honestly.
- **Git:** commit only when asked. Push only when asked (the research branches are on GitHub since Oct 18), never merge into `main`. Commit as
  `git -c user.name="Artjoms Papšs" -c user.email=artjompapsh2003@gmail.com commit …`.
  No co-author trailers or session links in commits, pushes or PRs.
- **Don't change** `specs/`, `src/` or `main` of the team repo without a go-ahead. Spec drafts live here.
- **Don't touch** another session's or agent's `exp_*` runs. Stop runs with exact patterns
  (`pkill -f "[e]xp_<name>/local-ci/pipeline.sh"`), then `docker stop <id>` of that run's container
  (find it with `docker inspect -f '{{range .Mounts}}{{.Source}} {{end}}' <id>`).
- The user was away (napping) and gave full autonomy toward the goal: "everything is allowed, as long
  as it is moving towards the goal". Report what you did when they are back.

## Repos and branches (all local)

| Path | Branch | What |
| --- | --- | --- |
| `/home/papart/development/TT/chipathon-2026` | `sha3-asic-research` | This folder (untracked) and `docs/handoff_sha3.md` |
| `/home/papart/development/TT/chipathon-4x2` | `sha3-4x2` | Agent build: Keccak in the top, hash tests (`test/test_hash.py`), spec edits for Specs 1/3/5, `docs/ntt_cache_study.md` |
| `/home/papart/development/TT/chipathon-4x2-cache` | `sha3-4x2-cache` | NTT block engine `ntt_blk` (works in RTL, does not fit next to Keccak) |
| `/home/papart/development/TT/exp_<name>` | throwaway | One clone per harden experiment, log in `exp_<name>.log` |

## Files in this folder

| File | Purpose |
| --- | --- |
| `sha3_asic_options.md` | The report. §8 (signoff) still to fill |
| `Spec 6 Keccak Core.md` | Draft Spec 6 (ports, commands, flow-rule exceptions) |
| `keccak_ring.v` | The core. Commands ABSORB 0, SQUEEZE 1, PERMUTE 2, CLEAR 3, SKIP 4 |
| `top/tt_um_NTT_Core.v` | Integrated top. Hash word = valid with coeff[11:8] = 0xF. **Newest change:** `hash_w`, `ntt_valid`, `ntt_sof` are registered straight from the pins (slow-corner setup fix). Not yet copied into `chipathon-4x2` |
| `ring_model.py` | Python structural model plus FIPS 202 reference (source of truth) |
| `test_keccak_ring.py`, `Makefile` | Core tests: `make K=8` (also K=4, `CLOCK_GATING=0`) |
| `mlkem_model.py`, `overlap_model.py` | Speed models (serial, list-scheduled) |
| `make_exp.sh` | Old experiment maker (from chipathon-2026, no hash tests) |
| `make_exp2.sh` | **Use this one.** Clones `chipathon-4x2` `sha3-4x2`, drops in this folder's core and top. `SDC=… EXCL=… HOLD=pl,grt ./make_exp2.sh <name> <tiles> <density> [KEY=JSON …]` |
| `hold05/10/14.sdc`, `base.sdc` | Default SDC plus a hold-only uncertainty line |
| `no_synth_small.cells` | Synthesis exclusion list that allows small `_1` cells |
| `status.sh` | Read-only run status, signoff metrics once `final/metrics.json` exists |
| `hold.tcl`, `hpwl.py`, `synth*.sh` | STA and area helpers |

Launch a harden: `cd /home/papart/development/TT/exp_<name> && local-ci/run --no-serve test gds > ../exp_<name>.log 2>&1`
(in the background; detailed routing takes 1–3 h).

## Live runs at handoff

| Run | Setup | State |
| --- | --- | --- |
| `exp_k8_hu05` | 3x2, K = 8, small cells, hold 0.05 SDC, margins 0.05 / 0.03, density 75, old top | Detailed routing pass ~19, 896 → ~590 violations, converging slowly. GRT 65.8 % |
| `exp_k8_t2` | 3x2, K = 8, small cells, **hold 0.14 SDC**, margins 0.03 / 0.02, density 75, **new top**, `EXTRA_EXCLUDED_CELLS` bans `clkdlybuf4s*` | RTL tests 16/16 pass. 7 hold buffers, GRT 69.6 %. Detailed routing early passes (3.8k–5.7k violations) |
| `exp_k8_t2_d65` | As `exp_k8_t2`, density 65 (`gds` only) | Placement raised the target to the 69.7 % floor. 7 hold buffers, GRT 74.6 % (worse than 75). Backup only |
| `exp_k8_t2_d70` | As `exp_k8_t2`, density 70 | **Stopped**: byte-identical placement to `d65` |
| `exp_k8_t2_h16` | As `exp_k8_t2`, hold uncertainty 0.16 | **Stopped** at detailed routing (user's call): 819 hold buffers, GRT 96.6 %, not a usable fallback |
| `exp_k8_t2_h18` | hold 0.18 | **Failed** at antenna repair: 1,319 buffers, GRT 142.5 % |
| `exp_k8_t2_h20` | hold 0.20 | **Stopped**: 1,373 buffers, GRT 150.2 % |
| `exp_lat_4x2` | 4x2, `keccak_lat` (branch `sha3-latch`), every TT default | **Failed**: RTL tests 16/16 pass, then 122.1k µm² synthesized, 95.2 % utilization, DPL-0036 |
| `exp_lat_4x2s` | 4x2, `keccak_lat`, small-cell list + `clkdlybuf4s*` ban, **default hold** (no custom SDC, margins 0.1 / 0.05) | Running (`gds` only). The test: does the latch core harden without the custom hold SDC? |

**Hold cliff (measured):** 0.14 ns → 7 buffers, 0.16 → 819, 0.18 → 1,319, 0.25 → 1,400+. The ring only
routes in 3x2 at 0.14 or below; there is no less aggressive setting to fall back on.

`exp_k8_t2` is the candidate: it has the safer hold setting and the setup fix. Whichever of the
`t2` runs (`t2`, `t2_d65`) signs off clean first wins.

**TT organisers' answer (Discord, Oct 18):** `sky130_fd_sc_hd__dlclkp` cells are allowed; any SDC is
allowed; submissions are taken from the GDS and are almost never modified (only rare analog DRC fixes,
with contact first), so there is no later re-timing with default constraints. They know of no silicon
measurement that argues for more or less hold uncertainty. So both flow exceptions are within the rules;
the 0.14 ns margin is our own engineering risk and our signoff is the final word on timing.

## Measured findings to keep in mind

- **Hold:** a ring hop (Q → D, no logic) has about 0.18 ns of real hold slack at the ff corner, and
  about 0.01 ns of skew after CRPR. The default 0.25 ns uncertainty plus 0.1 margin puts a buffer on
  every hop (1,400–1,860 buffers), and routing fails. This holds at 3x2, 4x2 and 6x2 (the cache
  design). The 0.14 SDC with default margins still gives about 1,380 buffers. It needs margins
  0.03 / 0.02.
- **Setup (agent, 4x2, hold 0.14):** every job passed (precheck, gl_test, 0 DRC/LVS/antenna), but
  max_ss setup was −1.55 ns. The path was `coeff_q[10] → hash decode → NTT control → u_core.zeta_r`.
  The resizer used `clkdlybuf4s25_1` delay cells as fanout buffers (1.0–1.6 ns each), and 2,867 max
  slew violations followed. The NTT alone in 2x2 has +1.41 ns. **Mid-PnR STA is not predictive:**
  4x2 showed +6.2 ns at step 43 and −1.04 ns (nom_ss) at signoff. Wait for signoff.
- **NTT cache:** the LF block engine gives 1,010 clocks per NTT instead of 2,751. It doesn't fit with
  Keccak in 4x2 or 6x2 (defaults), and ML-KEM improves only from 3.09 to 2.56 ms. Recommendation: no
  cache this shuttle.
- **ML-KEM needs Keccak** (FIPS 203). Kyber-90s (AES + SHA-2) was withdrawn: not recommended.

## Next steps, in order

1. **Wait for `exp_k8_t2` signoff.** Then read `runs/wokwi/final/metrics.json` (`./status.sh k8_t2`)
   and the summary at `runs/wokwi/*-openroad-stapostpnr/summary.rpt`. Check:
   - 0 DRC/LVS/antenna;
   - setup ≥ 0 at max_ss. If it is not, find the path in `max_ss_100C_1v60/max.rpt`. Fallbacks:
     relax the clock to 40 MHz (`CLOCK_PERIOD` 25), or more register slicing;
   - hold at all corners.
2. Precheck and gl_test run automatically after a successful gds job (same `local-ci/run … gds`).
   Check their results in `exp_k8_t2.log` and in `.ci-local/runs/<latest>/jobs/{precheck,gl_test}/status`.
3. **Hold audit on the final layout:** rerun STA with the default 0.25 ns hold uncertainty and with 0.
   Report how many paths rely on the custom value and the real slack at the ff corner. `hold.tcl`
   is a starting point (it reads the post-CTS odb; point it at `final/odb`).
4. When `exp_k8_hu05` ends, record it as the comparison point, or stop it if it stalls (it uses the old
   top and the smaller hold margin, so it is not the candidate).
5. Fill §8 of `sha3_asic_options.md`. Copy the new top into `chipathon-4x2` (branch `sha3-4x2`) and
   rerun its tests. Update the spec edits there to say 3x2, the registered hash flag and the flow
   settings.
6. **Latch-based Keccak (started Oct 18, branch `sha3-latch`, worktree `/home/papart/development/TT/chipathon-latch`, uncommitted):**
   `src/keccak_lat.v`, tests `test/keccak/` (`make` with `/opt/venv-test/bin` on PATH in the tt-local-ci image).
   State in 1,600 `dlxtp` latches, 64-bit lane-serial datapath; pi is an address rename (the 24 non-zero
   lanes form one pi cycle, so the mapping is identity again after 24 rounds). Same ports and commands as
   `keccak_ring`. RTL passes the ring's test set bit-exact (hashlib). **But:** plain synthesis 74.4k µm²
   (ring 47.0k), 1,464 clocks per permutation, ML-KEM model 1.65× vs RP2350 (ring 2.55×). Does not fit 3x2
   with the NTT; floor of the approach is about 35k (latches + read port). 4x2 hardens: see the live-runs table.
   Write-up: `docs/keccak_lat.md` on that branch. All four branches are on GitHub (`SiLiCaP/chipathon-2026`).
7. Things the user may ask for next (offered, not started):
   - a **6x2 run with default settings** (12 tiles, paid), as proof of what the defaults cost;
   - a **latch-based lane-serial Keccak** prototype: estimated 50–60k µm², about 1,300–1,400
     clocks, no shift hops, so it may pass with default hold. Plain synthesis first;
8. Real-use firmware notes for Spec 1: hybrid X25519 + ML-KEM, a known-answer self-test at boot, a
   pairwise-consistency check after keygen, software fallback, and CLEAR at the start of every hash
   call (the state is not reset and holds the previous call's secrets).
