# Local CI

Runs the same jobs as the GitHub Actions workflows on your machine and shows the
results on a local web page: jobs, step logs (live while running), job summaries,
test results, artifacts, the GDS render and links to the 3D viewer.

```sh
local-ci/run              # test + docs + gds (incl. precheck, gl_test, viewer), then serve the results
local-ci/run test         # only the RTL cocotb test, takes seconds
local-ci/run gds          # only the gds workflow
local-ci/run serve        # browse earlier runs
local-ci/run --no-serve   # for scripts: exit code 0 only if every job passed
```

Results page: <http://localhost:8765/>. Runs are stored in `.ci-local/` (last 10 kept, `--keep N`).

## What runs

| GitHub workflow / job | Local equivalent |
|---|---|
| test / test | `make` in `test/`, `cocotb_tools.check_results`, JUnit summary, `test-results` artifact |
| docs / docs | `tt_tool --check-docs`, `--create-pdf`, `PDF` artifact |
| gds / gds | `tt_tool --create-user-config`, `--harden` (LibreLane 3.0.14), linter/warnings/stats/cell summaries, `GDS_logs`, `tt_submission`, `gds_render` artifacts |
| gds / precheck | `tt/precheck/precheck.py` with the same PDK version and Nix-pinned KLayout and Magic |
| gds / gl_test | gate-level `GATES=yes make` against the hardened netlist |
| gds / viewer | GDS/OAS + PNG, with links to gds-viewer.tinytapeout.com that load the layout from localhost |
| fpga | not supported (disabled on push in `fpga.yaml` as well) |

## How it works

- `run` (host): updates `tt/` (tt-support-tools `main`, like CI), builds the
  `tt-local-ci` image when its inputs change, starts `serve.py` and runs
  `pipeline.sh` in the container.
- `pipeline.sh` (container): the jobs and steps, step by step, as in the
  workflows and in `TinyTapeout/tt-gds-action`.
- LibreLane starts its own `ghcr.io/librelane/librelane` container on the host's
  Docker daemon, which is why the repo and cache are mounted at their host paths.
- `report.py` regenerates the static pages after every step.

Cache (PDKs, image build context): `~/.cache/tt-local-ci`. The first `gds` run
downloads the LibreLane image and the PDKs (several GB).

Differences from GitHub: Python 3.12 instead of 3.11, and the RTL test uses the
iverilog 13 build from the `gl_test` action instead of Ubuntu's iverilog 12.

## Host CI and the GitHub fallback

The owner's home server (the host, d0b0) builds every push with this local CI and posts the commit statuses
`host-ci/gds` and `host-ci/rig` (the Pico 2 W + Tang Nano rig stage). GitHub's `gds` workflow only builds if
the host does not acknowledge the commit with a `host-ci/gds` status within 10 minutes (job `wait-host`); a manual
`workflow_dispatch` run always builds on GitHub. `local-ci/summary.py <run-dir>` prints a run's jobs, durations,
failures and (best effort) utilisation and warning count as JSON; test it with `python3 local-ci/test_summary.py`.
