# Whole-core tests for ntt_core (Spec 3), RTL only.
#   make                         default: src/modq_mul.v (latency 3), PRE_LAT=0, POST_LAT=0
#   make PRE_LAT=1 POST_LAT=1    other Pre/Post +/- register settings
#   make L_MUL=6                 behavioral multiplier (modq_mul_lat.v) with latency 6
#   make all-variants            all of the above that matter

SIM ?= icarus
TOPLEVEL_LANG ?= verilog
SRC_DIR = $(PWD)/../../src
PRE_LAT ?= 0
POST_LAT ?= 0
L_MUL ?=

CORE_SOURCES = ntt_core.v ntt_ctrl.v ntt_sched.v ntt_twiddle.v ntt_delay.v zeta_rom.v \
               modq_addsub.v modq_acc.v

ifeq ($(L_MUL),)
# the multiplier in src/ (placeholder or the owner's), latency 3
VERILOG_SOURCES += $(SRC_DIR)/modq_mul.v
L_MUL_P = 3
else
VERILOG_SOURCES += $(PWD)/modq_mul_lat.v
COMPILE_ARGS    += -DMUL_LAT=$(L_MUL)
L_MUL_P = $(L_MUL)
endif

SIM_BUILD        = sim_build/l$(L_MUL_P)$(if $(L_MUL),m)_pre$(PRE_LAT)_post$(POST_LAT)
VERILOG_SOURCES += $(addprefix $(SRC_DIR)/,$(CORE_SOURCES)) $(PWD)/tb_core.v
COMPILE_ARGS    += -DNTT_ASSERT -Ptb_core.L_MUL=$(L_MUL_P) -Ptb_core.PRE_LAT=$(PRE_LAT) -Ptb_core.POST_LAT=$(POST_LAT)
TOPLEVEL         = tb_core
COCOTB_TEST_MODULES = test_core
export PYTHONPATH := $(PWD)/..:$(PYTHONPATH)
export PRE_LAT POST_LAT
export CORE_L_MUL = $(L_MUL_P)

include $(shell cocotb-config --makefiles)/Makefile.sim

VARIANTS = "PRE_LAT=0 POST_LAT=0" "PRE_LAT=0 POST_LAT=1" "PRE_LAT=1 POST_LAT=0" "PRE_LAT=1 POST_LAT=1" \
           "L_MUL=2" "L_MUL=4" "L_MUL=5" "L_MUL=6" "L_MUL=6 PRE_LAT=1 POST_LAT=1"

.PHONY: all-variants
all-variants:
	@set -e; for v in $(VARIANTS); do \
	  r=results_$$(echo $$v | tr ' =' '_-').xml; \
	  echo "=== $$v"; \
	  $(MAKE) --no-print-directory $$v COCOTB_RESULTS_FILE=$$r; \
	  python -m cocotb_tools.check_results $$r; \
	done
