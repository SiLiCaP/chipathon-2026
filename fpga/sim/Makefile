# cocotb simulation of the Tang Nano wrapper + UART protocol.
#   make                              sequential self-test DUT (checks the wrapper's cycle accuracy)
#   make TT_TOP=tt_um_example DUT_SRCS=...   any DUT with a model in host/dut_models.py

SIM ?= icarus
TOPLEVEL_LANG ?= verilog
FST ?= -fst

TT_TOP   ?= tt_um_emu_selftest
DUT_SRCS ?=
export TT_TOP

VERILOG_SOURCES = $(DUT_SRCS) $(CURDIR)/tt_um_emu_selftest.v $(CURDIR)/../uart.v $(CURDIR)/../tangnano20k_top.v
COMPILE_ARGS += -DTT_TOP=$(TT_TOP) -Ptangnano20k_top.BAUD=3375000
TOPLEVEL = tangnano20k_top
COCOTB_TEST_MODULES = test_wrapper
SIM_BUILD = sim_build/$(TT_TOP)
COCOTB_RESULTS_FILE = results_$(TT_TOP).xml

include $(shell cocotb-config --makefiles)/Makefile.sim
