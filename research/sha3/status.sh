#!/bin/bash
# status.sh [run names...]: where each LibreLane experiment is, and its signoff numbers once done.
#   research/sha3/status.sh                 all exp_* runs
#   research/sha3/status.sh k8_t2 k8_hu05   just these
# Read-only: never stops or changes a run.
TT=/home/papart/development/TT
names=("$@")
[ ${#names[@]} -eq 0 ] && names=($(ls -d $TT/exp_*/ 2>/dev/null | xargs -n1 basename | sed 's/^exp_//'))
for e in "${names[@]}"; do
  d=$TT/exp_$e/runs/wokwi
  [ -d "$d" ] || { echo "== $e: no run dir"; continue; }
  if pgrep -f "[e]xp_$e/local-ci/pipeline.sh" >/dev/null; then state=running; else state=stopped; fi
  step=$(ls "$d" | grep -E '^[0-9]+-' | sort -n | tail -1)
  echo "== $e: $state, last step $step"
  hb=$(grep -h "Inserted .* hold buffers" "$d"/*/*.log 2>/dev/null | tail -1)
  [ -n "$hb" ] && echo "   $hb"
  grt=$(grep -h "^Total  " "$d"/*-openroad-globalrouting/*.log 2>/dev/null | tail -1)
  [ -n "$grt" ] && echo "   GRT: $grt"
  drv=$(grep -h "Number of violations =" "$d"/*-openroad-detailedrouting/*.log 2>/dev/null | tail -1)
  [ -n "$drv" ] && echo "   DRT: $drv"
  if [ -f "$d/final/metrics.json" ]; then
    python3 - "$d/final/metrics.json" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
keys = ["design__instance__utilization", "route__drc_errors", "design__lvs_error__count",
        "antenna__violating__nets", "timing__setup__ws", "timing__hold__ws",
        "timing__setup__ws__corner:max_ss_100C_1v60", "timing__hold__ws__corner:max_ff_n40C_1v95"]
for k in keys:
    if k in m: print(f"   {k} = {m[k]}")
PY
  fi
  log=$TT/exp_$e.log
  [ -f "$log" ] && grep -E "TESTS=[0-9]+ PASS" "$log" | head -1 | sed 's/^ */   test: /'
done
