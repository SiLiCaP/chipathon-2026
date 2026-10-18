#!/bin/bash
# make_exp2.sh <name> <tiles> <density> [KEY=JSONVALUE ...]
# Like make_exp.sh, but starts from the 4x2 agent branch (sha3-4x2: hash tests, specs) and drops in
# this folder's keccak_ring.v and integrated top. Env: SDC=<file>, EXCL=<cells file>, HOLD=<pl>,<grt>.
set -e
name=$1; tiles=$2; dens=$3; shift 3
here=/home/papart/development/TT/chipathon-2026/research/sha3
d=/home/papart/development/TT/exp_$name
rm -rf "$d"
git clone -q -b sha3-4x2 /home/papart/development/TT/chipathon-4x2 "$d"
cp -r /home/papart/development/TT/chipathon-4x2/tt "$d/tt" 2>/dev/null || cp -r /home/papart/development/TT/chipathon-2026/tt "$d/tt"
cp "$here/keccak_ring.v" "$here/top/tt_um_NTT_Core.v" "$d/src/"
if [ -n "$SDC" ]; then cp "$here/$SDC" "$d/src/custom.sdc"; set -- "$@" 'PNR_SDC_FILE="dir::custom.sdc"' 'SIGNOFF_SDC_FILE="dir::custom.sdc"'; fi
if [ -n "$EXCL" ]; then cp "$here/$EXCL" "$d/src/no_synth.cells"; set -- "$@" 'SYNTH_EXCLUDED_CELL_FILE="dir::no_synth.cells"'; fi
cd "$d"
python3 - "$tiles" "$dens" "$@" <<'PY'
import sys, re, os
tiles, dens, extra = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
y = open("info.yaml").read()
open("info.yaml", "w").write(re.sub(r'tiles: "\w+"', f'tiles: "{tiles}"', y))
c = open("src/config.json").read()
c = re.sub(r'"PL_TARGET_DENSITY_PCT": \d+', f'"PL_TARGET_DENSITY_PCT": {dens}', c)
h = os.environ.get("HOLD")
if h:
    pl, grt = h.split(",")
    c = re.sub(r'"PL_RESIZER_HOLD_SLACK_MARGIN": [\d.]+', f'"PL_RESIZER_HOLD_SLACK_MARGIN": {pl}', c)
    c = re.sub(r'"GRT_RESIZER_HOLD_SLACK_MARGIN": [\d.]+', f'"GRT_RESIZER_HOLD_SLACK_MARGIN": {grt}', c)
for kv in extra:
    k, val = kv.split("=", 1)
    c = c.replace('  "CLOCK_PORT": "clk",', f'  "CLOCK_PORT": "clk",\n  "{k}": {val},')
open("src/config.json", "w").write(c)
PY
git -c user.name=x -c user.email=x@x commit -qam "exp $name"
echo "$d ready"
