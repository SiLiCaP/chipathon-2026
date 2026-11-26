#!/bin/bash
# synth.sh <top> <out-prefix> <verilog files...>   plain yosys + ABC, sky130 hd tt corner
top=$1; out=$2; shift 2
LIB=/home/papart/.cache/tt-local-ci/pdk/sky130A/libs.ref/sky130_fd_sc_hd/lib/sky130_fd_sc_hd__tt_025C_1v80.lib
cat > $out.ys <<YS
read_liberty -lib $LIB
read_verilog -sv $*
synth -flatten -top $top
dfflibmap -liberty $LIB
abc -liberty $LIB
opt_clean
stat -liberty $LIB
YS
yosys -q -l $out.log $out.ys >/dev/null 2>&1
echo "$out: $(grep -E 'Chip area' $out.log | tail -1)"
