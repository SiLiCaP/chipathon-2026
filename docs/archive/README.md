# Archive: historical design notes

These pages are kept for the record. Each describes the design as it was on its date; figures, file names and
branches in them may no longer exist. For the chip on `main`, use [info.md](../info.md) (the datasheet),
[getting_started.md](../getting_started.md) and [speed_results.md](../speed_results.md).

## The hash chip: how it was chosen (Oct 17 to 31, 2026)

| Page | What it is |
|---|---|
| [architecture_options.md](architecture_options.md) | Architecture options for 4 and for 8 tiles, Oct 17 |
| [sha3_report.md](sha3_report.md) | Why hashing decides ML-KEM-512 speed, Oct 17 |
| [tile_routes.md](tile_routes.md) | Tile sizes and routes: what fits where, Oct 18 |
| [sha3_only_3x2.md](sha3_only_3x2.md) | The first 3x2 harden of the hash-only chip, Oct 18 (custom hold SDC, since removed) |
| [handoff_sha3_only.md](handoff_sha3_only.md) | Handoff of the `sha3-only` branch, Oct 18 |
| [host_ci_findings.md](host_ci_findings.md) | Host CI findings: tile sizes, pin placement, the rig, Oct 30 to 31 |
| [onchip_offload.md](onchip_offload.md) | Moving more ML-KEM steps into the chip, Oct 31 |
| [harden_hash_io_fix.md](harden_hash_io_fix.md) | The `hash_io` busy fix that was not merged, and why the layout is hard to re-route, Nov 25 to 26 |
| [speed_history.md](speed_history.md) | The firmware's speed steps of Nov 25, measured against portable C |

## The earlier NTT chip (to Oct 18, 2026)

The RTL and tests these pages describe (`src/ntt_core.v`, `src/modq_*.v`, `test/timing.py`, ...) are no longer in
the tree. The specs they implement are in [ntt_chip/specs/](ntt_chip/).

| Page | What it is |
|---|---|
| [interface.md](interface.md) | Interface table of the NTT core's blocks |
| [control.md](control.md) | Control loop and integration (Spec 3) |
| [schedule.md](schedule.md) | Per-mode schedule |
| [host_timing.md](host_timing.md) | Host timing: output delay, periods, job lengths |
| [adders.md](adders.md) | Modular adders (Spec 4) |
| [ntt_cache_study.md](ntt_cache_study.md) | NTT cache study for the 4x2 build, Oct 18 |

## Files that moved here with their pages

Paths inside the archived pages are the ones of their date. Where those files are now:

| Then | Now | What |
|---|---|---|
| `specs/`, `schematics/`, `tables/` | [ntt_chip/](ntt_chip/) | The NTT chip's five specs, its schematics and its ROM tables |
| `scripts/gen_tables.py` | [ntt_chip/gen_tables.py](ntt_chip/gen_tables.py) | Generated those tables; needs the removed `test/ntt_ref.py`, so it no longer runs |
| `research/` | [research/](research/) | Models and synthesis blocks of the NTT cache and on-chip offload studies |
| `scripts/offload_budget.py` | [research/offload/offload_budget.py](research/offload/offload_budget.py) | Pin and clock budget of the October command set; still runs |
| `fpga/sim_pico/`, `fpga/sim_variant/` | [fpga_sims/](fpga_sims/) | Simulations that need files from tt-viktor-chiptor; they do not run from this repo |
| `rp2350/results/` (runs before `213448`) | [speedtest_runs/](speedtest_runs/README.md) | Speed test runs of earlier firmware, against portable C |
