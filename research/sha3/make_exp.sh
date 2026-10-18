#!/bin/bash
# make_exp.sh <name> <K> <tiles> <density> [extra json key=value ...]
# Clones the team repo next to it, drops in the hasher and the integrated top, sets tiles/density.
set -e
name=$1; K=$2; tiles=$3; dens=$4; shift 4
base=/home/papart/development/TT/chipathon-2026
d=/home/papart/development/TT/exp_$name
rm -rf "$d"
git clone -q "$base" "$d"
cp -r "$base/tt" "$d/tt"
cp "$base/research/sha3/keccak_ring.v" "$d/src/"
cp "$base/research/sha3/top/tt_um_NTT_Core.v" "$d/src/"
if [ -n "$SDC" ]; then cp "$base/research/sha3/$SDC" "$d/src/custom.sdc"; set -- "$@" "PNR_SDC_FILE=\"dir::custom.sdc\"" "SIGNOFF_SDC_FILE=\"dir::custom.sdc\""; fi
if [ -n "$EXCL" ]; then cp "$base/research/sha3/$EXCL" "$d/src/no_synth.cells"; set -- "$@" "SYNTH_EXCLUDED_CELL_FILE=\"dir::no_synth.cells\""; fi
cd "$d"
python3 - "$K" "$tiles" "$dens" "$@" <<'PY'
import sys, json, re
K, tiles, dens, extra = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4:]
y = open("info.yaml").read()
y = re.sub(r'tiles: "\w+"', f'tiles: "{tiles}"', y)
y = y.replace('    - "modq_acc.v"\n', '    - "modq_acc.v"\n    - "keccak_ring.v"\n')
open("info.yaml", "w").write(y)
v = open("src/tt_um_NTT_Core.v").read().replace("parameter HASH_K = 8", f"parameter HASH_K = {K}")
open("src/tt_um_NTT_Core.v", "w").write(v)
c = open("src/config.json").read()
import os
h = os.environ.get("HOLD")
if h:
    pl, grt = h.split(",")
    c0 = open("src/config.json").read()
c = c.replace('"PL_TARGET_DENSITY_PCT": 60', f'"PL_TARGET_DENSITY_PCT": {dens}')
if h:
    c = c.replace('"PL_RESIZER_HOLD_SLACK_MARGIN": 0.1', f'"PL_RESIZER_HOLD_SLACK_MARGIN": {pl}').replace('"GRT_RESIZER_HOLD_SLACK_MARGIN": 0.05', f'"GRT_RESIZER_HOLD_SLACK_MARGIN": {grt}')
for kv in extra:
    k, val = kv.split("=", 1)
    c = c.replace('  "CLOCK_PORT": "clk",', f'  "CLOCK_PORT": "clk",\n  "{k}": {val},')
open("src/config.json", "w").write(c)
m = open("test/Makefile").read()
open("test/Makefile", "w").write(m.replace("modq_acc.v", "modq_acc.v keccak_ring.v", 1))
PY
git -c user.name=x -c user.email=x@x commit -qam "exp $name" 
echo "$d ready"
