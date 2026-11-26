# cocotb simulation of the Pico 2 W rig top (tangnano20k_pico_top) with the team chip's RTL:
# drives the header pins the way the Pico's PIO does and checks every uo_out byte against the cycle model.
#   make -C fpga/sim_pico            (this repo's chip)
SIM ?= icarus
TOPLEVEL_LANG ?= verilog
TEAM_SRC := $(abspath $(CURDIR)/../../src)
SRC_FILES := $(shell awk '/^ *source_files:/{f=1;next} f&&/^ *- /{sub(/^ *- */,"");sub(/ *#.*/,"");gsub(/"/,"");print;next} f&&!/^ *#/{f=0}' $(TEAM_SRC)/../info.yaml)
VERILOG_SOURCES = $(addprefix $(TEAM_SRC)/,$(SRC_FILES)) $(CURDIR)/../tangnano20k_pico_top.v
COMPILE_ARGS += -DTT_TOP=tt_um_NTT_Core
TOPLEVEL = tangnano20k_pico_top
COCOTB_TEST_MODULES = test_pico_top
export NTT_CHIP = team
include $(shell cocotb-config --makefiles)/Makefile.sim
