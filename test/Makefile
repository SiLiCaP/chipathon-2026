# Makefile
# See https://docs.cocotb.org/en/stable/quickstart.html for more info

# defaults
SIM ?= icarus
FST ?= -fst # Use more efficient FST format
TOPLEVEL_LANG ?= verilog
SRC_DIR = $(PWD)/../src
PROJECT_SOURCES = tt_um_NTT_Core.v io_in_capture.v hash_io.v keccak_ring.v

ifneq ($(GATES),yes)

# RTL simulation:
SIM_BUILD				= sim_build/rtl
VERILOG_SOURCES += $(addprefix $(SRC_DIR)/,$(PROJECT_SOURCES))
# HASH_K=16 make: the K = 16 core (172 clocks per permutation)
ifdef HASH_K
COMPILE_ARGS    += -DHASH_K=$(HASH_K)
endif
# keccak_ring clock gates: behavioral model in RTL sim (GL uses the sky130 dlclkp cell model)
COMPILE_ARGS    += -DKC_BEHAV_ICG

else

# Gate level simulation:
SIM_BUILD				= sim_build/gl
COMPILE_ARGS    += -DGL_TEST
COMPILE_ARGS    += -DFUNCTIONAL
COMPILE_ARGS    += -DUSE_POWER_PINS
COMPILE_ARGS    += -DSIM
COMPILE_ARGS    += -DUNIT_DELAY=\#1
VERILOG_SOURCES += $(PDK_ROOT)/sky130A/libs.ref/sky130_fd_sc_hd/verilog/primitives.v
VERILOG_SOURCES += $(PDK_ROOT)/sky130A/libs.ref/sky130_fd_sc_hd/verilog/sky130_fd_sc_hd.v

# this gets copied in by the GDS action workflow
VERILOG_SOURCES += $(PWD)/gate_level_netlist.v

endif

# Allow sharing configuration between design and testbench via `include`:
COMPILE_ARGS 		+= -I$(SRC_DIR)

# Include the testbench sources:
VERILOG_SOURCES += $(PWD)/tb.v
TOPLEVEL = tb

# List test modules to run, separated by commas and without the .py suffix:
COCOTB_TEST_MODULES = test_hash_only
export HASH_K

# include cocotb's make rules to take care of the simulator setup
include $(shell cocotb-config --makefiles)/Makefile.sim

# The RP2350 firmware's own pin stream (rp2350/core/hash_chip.c) through the RTL, against its software chip.
#   make rp2350              K = 8 RTL; records the PC harness's `all` and `ci` (golden) runs, replays them
RP2350_PC = $(abspath $(PWD)/../rp2350/pc)
.PHONY: rp2350
rp2350:
	$(MAKE) -C $(RP2350_PC) hash_host ci-golden
	cd $(RP2350_PC) && ./hash_host --sw --dump-words $(PWD)/stream_all.hwd all
	cd $(RP2350_PC) && ./hash_host_ci --sw --dump-words $(PWD)/stream_ci.hwd ci
	rm -f results.xml
	$(MAKE) COCOTB_TEST_MODULES=test_driver_stream STREAM=$(PWD)/stream_all.hwd,$(PWD)/stream_ci.hwd
