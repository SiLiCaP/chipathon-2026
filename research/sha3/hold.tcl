read_db /home/papart/development/TT/exp_k8_h03/runs/wokwi/35-openroad-cts/tt_um_NTT_Core.odb
read_liberty /home/papart/.cache/tt-local-ci/pdk/sky130A/libs.ref/sky130_fd_sc_hd/lib/sky130_fd_sc_hd__ff_n40C_1v95.lib
read_sdc /home/papart/development/TT/exp_k8_h03/runs/wokwi/35-openroad-cts/tt_um_NTT_Core.sdc
set_propagated_clock [all_clocks]
estimate_parasitics -placement
report_checks -path_delay min -group_path_count 3 -fields {cap slew} -digits 3
report_clock_skew -hold
