# Spec 4 tests for src/modq_addsub.v and src/modq_acc.v (iverilog, no cocotb).
#   make             quick: every x against every 17th y (and y = 3328), 200k accumulator ops
#   make exhaustive  all 3329 x 3329 pairs, both REG_OUT settings (several minutes)

SRC = ../../src/modq_addsub.v ../../src/modq_acc.v

.PHONY: quick exhaustive run clean
quick:
	$(MAKE) --no-print-directory run Y_STEP=17
exhaustive:
	$(MAKE) --no-print-directory run Y_STEP=1

run:
	iverilog -g2012 -Wall -Wno-timescale -DY_STEP=$(Y_STEP) -o adders.vvp tb_adders.v $(SRC)
	vvp -n adders.vvp

clean:
	rm -f adders.vvp
