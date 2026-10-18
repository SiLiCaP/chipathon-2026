SIM ?= icarus
TOPLEVEL_LANG = verilog
VERILOG_SOURCES = $(PWD)/keccak_ring.v
COCOTB_TOPLEVEL = keccak_ring
COCOTB_TEST_MODULES = test_keccak_ring
CLOCK_GATING ?= 1
K ?= 8
COMPILE_ARGS += -DKC_BEHAV_ICG -Pkeccak_ring.CLOCK_GATING=$(CLOCK_GATING) -Pkeccak_ring.K=$(K)
include $(shell cocotb-config --makefiles)/Makefile.sim
