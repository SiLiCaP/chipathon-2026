#!/bin/bash
# run_synth.sh <top> <out> <chparam|""> files...  (runs synth.sh in the LibreLane image)
docker run --rm --user $(id -u):$(id -g) -e HOME=/tmp -v $HOME/.cache/tt-local-ci/pdk:$HOME/.cache/tt-local-ci/pdk:ro \
  -v /home/papart/development/TT:/home/papart/development/TT -w "$PWD" ghcr.io/librelane/librelane:3.0.14 \
  /root/.nix-profile/bin/bash ./synth.sh "$@"
