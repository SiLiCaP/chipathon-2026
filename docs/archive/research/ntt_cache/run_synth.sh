#!/bin/bash
# run_synth.sh <top> <out-prefix> <verilog files...>
# Runs synth.sh (plain Yosys + ABC, sky130 hd tt corner) in the LibreLane image.
# Example, from the repo root:
#   research/ntt_cache/run_synth.sh est_halfpoly halfpoly research/ntt_cache/est_halfpoly.v
docker run --rm --user $(id -u):$(id -g) -e HOME=/tmp \
  -v $HOME/.cache/tt-local-ci/pdk:$HOME/.cache/tt-local-ci/pdk:ro -v "$PWD":"$PWD" -w "$PWD" \
  ghcr.io/librelane/librelane:3.0.14 /root/.nix-profile/bin/bash "$(dirname "$0")/synth.sh" "$@"
