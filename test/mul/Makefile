# Spec 2 exhaustive test for src/modq_mul.v (iverilog, no cocotb).
#   make             quick: every a, every 37th b, a start every clock and every 3 clocks
#   make exhaustive  all 3329 x 3329 pairs, a start every clock (~11M products)
#   make LAT=4 ...   if the multiplier's latency is not 3

LAT ?= 3
SRC = ../../src/modq_mul.v

.PHONY: quick exhaustive clean
quick:
	$(MAKE) --no-print-directory run B_STEP=37 SPACING=1
	$(MAKE) --no-print-directory run B_STEP=37 SPACING=3
exhaustive:
	$(MAKE) --no-print-directory run B_STEP=1 SPACING=1

.PHONY: run
run:
	iverilog -g2012 -DLAT=$(LAT) -DB_STEP=$(B_STEP) -DSPACING=$(SPACING) -o mul.vvp tb_mul.v $(SRC)
	vvp -n mul.vvp

clean:
	rm -f mul.vvp
