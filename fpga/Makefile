# Tang Nano 20K emulator build (open-source flow: OSS CAD Suite).
#   make -C fpga          build build/top.fs
#   make -C fpga prog     load into SRAM (lost on power cycle)
#   make -C fpga flash    write to the onboard flash (persists)
#   make -C fpga sim      cocotb simulation of the wrapper + UART protocol
# Which chip:        make -C fpga prog                 this repo's chip (info.yaml + src/, tt_um_NTT_Core)
# Gate level:        make -C fpga netlist              harden the design with the TT flow (once per design change)
#                    make -C fpga prog GL=1            the hardened sky130 gate netlist instead of the RTL: every
#                    standard cell is rebuilt from its logic function in the sky130 liberty file, so the FPGA
#                    runs the exact gates that go on the chip. Default for the team chip; GL=0 for its RTL.
#                    GL_NETLIST=path overrides the netlist (e.g. the gate-level netlist from the GDS action).
# Test rig:          make -C fpga prog TARGET=pico      the chip's pins on the headers, for a Pico 2 W running the
#                    RP2350 firmware (TT_TARGET=pico2w_tang): wiring in docs/PICO_TANG_RIG.md. Default TARGET=uart:
#                    the PC drives the chip over USB (fpga/PROTOCOL.md).
# Override the DUT:  make -C fpga TT_TOP=tt_um_other [EXTRA_SRCS=path/to/extra.v]
# Self-test DUT:     make -C fpga prog TT_TOP=tt_um_emu_selftest EXTRA_SRCS=$(pwd)/fpga/sim/tt_um_emu_selftest.v

FPGA_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT     := $(abspath $(FPGA_DIR)/..)
DESIGN_ROOT ?= $(ROOT)
# DESIGN_ROOT=<folder>: any design tree with info.yaml + src/, e.g. a tools/tile_exp.py variant
#   make -C fpga prog TARGET=pico GL=0 DESIGN_ROOT=~/tools/work/exp/sha_k8_2x2 BUILD_TAG=_sha_k8 \
#        CHPARAM='chparam -set CLOCK_GATING 0 keccak_ring;'      (the ring core's clock gates as enables)
BUILD_TAG ?=
CHPARAM ?=

# DUT sources and top come from the design's info.yaml, so the FPGA build follows the ASIC build.
SRC_FILES := $(shell awk '/^ *source_files:/{f=1;next} f&&/^ *- /{sub(/^ *- */,"");sub(/ *#.*/,"");gsub(/"/,"");print;next} f&&!/^ *#/{f=0}' $(DESIGN_ROOT)/info.yaml)
TT_TOP    ?= $(shell awk -F'"' '/^ *top_module:/{print $$2}' $(DESIGN_ROOT)/info.yaml)
GL        ?= 1
HARDEN_DIR := $(FPGA_DIR)build/harden/$(TT_TOP)
GL_NETLIST ?= $(firstword $(wildcard $(DESIGN_ROOT)/runs/wokwi/final/nl/$(TT_TOP).nl.v $(HARDEN_DIR)/runs/wokwi/final/nl/$(TT_TOP).nl.v))
SKY130_LIB ?= $(firstword $(wildcard $(HOME)/.ciel/ciel/sky130/versions/*/sky130A/libs.ref/sky130_fd_sc_hd/lib/sky130_fd_sc_hd__tt_025C_1v80.lib))
TARGET    ?= uart
ifeq ($(GL),1)
DUT_SRCS  := $(GL_NETLIST) $(FPGA_DIR)sky130_phys_cells.v $(EXTRA_SRCS)
READ_CELLS := read_liberty -ignore_miss_func $(SKY130_LIB);
BUILD     := $(FPGA_DIR)build/$(TT_TOP)$(BUILD_TAG)_gl
else
DUT_SRCS  := $(addprefix $(DESIGN_ROOT)/src/,$(SRC_FILES)) $(EXTRA_SRCS)
READ_CELLS :=
BUILD     := $(FPGA_DIR)build/$(TT_TOP)$(BUILD_TAG)
endif
ifeq ($(TARGET),pico)
WRAP_SRCS := $(FPGA_DIR)tangnano20k_pico_top.v
WRAP_TOP  := tangnano20k_pico_top
WRAP_PARAMS :=
CST       := $(FPGA_DIR)tangnano20k_pico.cst
BUILD     := $(BUILD)_pico
else
WRAP_SRCS := $(FPGA_DIR)uart.v $(FPGA_DIR)tangnano20k_top.v
WRAP_TOP  := tangnano20k_top
WRAP_PARAMS = chparam -set BAUD $(BAUD) tangnano20k_top;
CST       := $(FPGA_DIR)tangnano20k.cst
endif

BAUD   ?= 1000000    # measured: 1 Mbaud stable; 1.5 and 3 Mbaud lose bytes in long readbacks (BL616)
DEVICE := GW2AR-LV18QN88C8/I7
FAMILY := GW2A-18C

.PHONY: all prog flash sim clean info netlist check_netlist

all: $(BUILD)/top.fs

info:
	@echo "DESIGN_ROOT = $(DESIGN_ROOT)"
	@echo "TT_TOP   = $(TT_TOP)"
	@echo "GL       = $(GL)"
	@echo "TARGET   = $(TARGET) ($(WRAP_TOP), $(notdir $(CST)))"
	@echo "DUT_SRCS = $(DUT_SRCS)"

$(BUILD)/top.json: $(DUT_SRCS) $(WRAP_SRCS) $(DESIGN_ROOT)/info.yaml | check_netlist $(BUILD)
	yosys -q -l $(BUILD)/yosys.log -D TT_TOP=$(TT_TOP) \
	  -p "$(READ_CELLS) read_verilog $(DUT_SRCS) $(WRAP_SRCS); $(CHPARAM) $(WRAP_PARAMS) synth_gowin -top $(WRAP_TOP) -json $@"

$(BUILD)/pnr.json: $(BUILD)/top.json $(CST)
	nextpnr-himbaechel -q -l $(BUILD)/nextpnr.log --json $< --write $@ \
	  --device $(DEVICE) --vopt family=$(FAMILY) --vopt cst=$(CST) --freq 27

$(BUILD)/top.fs: $(BUILD)/pnr.json
	gowin_pack -d $(FAMILY) -o $@ $<

prog: $(BUILD)/top.fs
	openFPGALoader -b tangnano20k $<

flash: $(BUILD)/top.fs
	openFPGALoader -b tangnano20k -f $<

sim:   # the wrapper is checked with the sequential self-test DUT (random pin words, cycle accuracy)
	$(MAKE) -C $(FPGA_DIR)sim TT_TOP=tt_um_emu_selftest

netlist:
	PATH=$(HOME)/tools/bin:$(HOME)/tools/tt-venv/bin:$$PATH sh $(FPGA_DIR)harden.sh $(DESIGN_ROOT) $(HARDEN_DIR)

check_netlist:
ifeq ($(GL),1)
	@test -n "$(GL_NETLIST)" || { echo "no gate netlist for $(TT_TOP): run 'make -C fpga netlist' (or GL=0 for the RTL)"; exit 1; }
endif

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(filter-out $(FPGA_DIR)build/harden,$(wildcard $(FPGA_DIR)build/*))
	$(MAKE) -C $(FPGA_DIR)sim clean
