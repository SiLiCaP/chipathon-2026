# Pin-level hash test of a tile_exp variant:  make -C fpga/sim_variant EXP=~/tools/work/exp/sha_k4_2x2 HASH_K=4
SIM ?= icarus
TOPLEVEL_LANG ?= verilog
EXP ?= $(HOME)/tools/work/exp/sha_k8_2x2
HASH_K ?= 8
HASH_CORE ?= ring
SRC_FILES := $(shell awk '/^ *source_files:/{f=1;next} f&&/^ *- /{sub(/^ *- */,"");sub(/ *#.*/,"");gsub(/"/,"");print;next} f&&!/^ *#/{f=0}' $(EXP)/info.yaml)
VERILOG_SOURCES = $(addprefix $(EXP)/src/,$(SRC_FILES))
COMPILE_ARGS += -DKC_BEHAV_ICG -DKL_BEHAV
TOPLEVEL = tt_um_NTT_Core
COCOTB_TEST_MODULES = test_variant_hash
SIM_BUILD = sim_build/$(notdir $(EXP))
COCOTB_RESULTS_FILE = results_$(notdir $(EXP)).xml
export HASH_K HASH_CORE
include $(shell cocotb-config --makefiles)/Makefile.sim
