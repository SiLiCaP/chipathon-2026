# keccak_lat core tests, RTL only (behavioral latch and clock-gate models).
#   make
SIM ?= icarus
TOPLEVEL_LANG ?= verilog
SRC_DIR = $(PWD)/../../src
VERILOG_SOURCES = $(SRC_DIR)/keccak_lat.v
COMPILE_ARGS += -DKL_BEHAV
COCOTB_TOPLEVEL = keccak_lat
COCOTB_TEST_MODULES = test_keccak_lat

include $(shell cocotb-config --makefiles)/Makefile.sim
