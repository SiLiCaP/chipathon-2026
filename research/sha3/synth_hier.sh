#!/bin/bash
LIB=/home/papart/.cache/tt-local-ci/pdk/sky130A/libs.ref/sky130_fd_sc_hd/lib/sky130_fd_sc_hd__tt_025C_1v80.lib
cat > hier.ys <<YS
read_liberty -lib $LIB
read_verilog -sv $*
synth -top tt_um_NTT_Core
dfflibmap -liberty $LIB
abc -liberty $LIB
opt_clean
stat -liberty $LIB
YS
yosys -q -l hier.log hier.ys >/dev/null 2>&1
grep -E "^=== |Chip area|Number of cells" hier.log
