# Whole-core tests for ntt_core (Spec 3), RTL only.
#   make                         default: src/modq_mul.v (latency 3), PRE_LAT=1, POST_LAT=0
#   make PRE_LAT=0 POST_LAT=1    other Pre/Post +/- register settings
#   make all-variants            every Pre/Post combination
# The multiplier latency is fixed at 3 (the basemul schedule starts m2 on the
# clock m1 comes out), so there are no latency variants.

SIM ?= icarus
TOPLEVEL_LANG ?= verilog
SRC_DIR = $(PWD)/../../src
PRE_LAT ?= 1
POST_LAT ?= 0

CORE_SOURCES = ntt_core.v ntt_ctrl.v ntt_sched.v ntt_twiddle.v ntt_delay.v zeta_rom.v \
               modq_addsub.v modq_acc.v ntt_blk.v

SIM_BUILD        = sim_build/pre$(PRE_LAT)_post$(POST_LAT)
VERILOG_SOURCES += $(addprefix $(SRC_DIR)/,modq_mul.v $(CORE_SOURCES)) $(PWD)/tb_core.v
COMPILE_ARGS    += -DNTT_ASSERT -Ptb_core.PRE_LAT=$(PRE_LAT) -Ptb_core.POST_LAT=$(POST_LAT)
TOPLEVEL         = tb_core
COCOTB_TEST_MODULES = test_core
export PYTHONPATH := $(PWD)/..:$(PYTHONPATH)
export PRE_LAT POST_LAT

include $(shell cocotb-config --makefiles)/Makefile.sim

VARIANTS = "PRE_LAT=0 POST_LAT=0" "PRE_LAT=0 POST_LAT=1" "PRE_LAT=1 POST_LAT=0" "PRE_LAT=1 POST_LAT=1"

.PHONY: all-variants
all-variants:
	@set -e; for v in $(VARIANTS); do \
	  r=results_$$(echo $$v | tr ' =' '_-').xml; \
	  echo "=== $$v"; \
	  $(MAKE) --no-print-directory $$v COCOTB_RESULTS_FILE=$$r; \
	  python -m cocotb_tools.check_results $$r; \
	done
