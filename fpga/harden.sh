#!/bin/sh
# Harden a Tiny Tapeout design with the TT flow (LibreLane via tt-support-tools), for GL=1 FPGA builds.
#   sh fpga/harden.sh <design repo> <output dir>
# Only the committed state (git HEAD) of the design repo is hardened, in a copy, so the repo stays clean.
# Needs tt-support-tools (TT_TOOLS, default ~/tools/tt-support-tools) and docker or a podman shim on PATH.
set -e
src=$(cd "$1" && pwd)
out=$2
rm -rf "$out"
mkdir -p "$out"
git -C "$src" archive HEAD | tar -x -C "$out"
cd "$out"
git init -q
git add -A
git -c user.email=fpga@localhost -c user.name=fpga commit -qm "snapshot of $(git -C "$src" rev-parse --short HEAD)"
ln -sfn "${TT_TOOLS:-$HOME/tools/tt-support-tools}" tt
python ./tt/tt_tool.py --create-user-config
python ./tt/tt_tool.py --harden
echo "netlist: $out/runs/wokwi/final/nl/"
