# PC build of the RP2350 harness core (any C compiler):
#   make -C rp2350/pc            build ntt_host
#   make -C rp2350/pc test       all tests against the software chip
CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror
CORE := ../core
SRCS := pc_main.c $(CORE)/ntt_ref.c $(CORE)/ntt_chip.c $(CORE)/swchip.c $(CORE)/app.c $(CORE)/oplog.c $(CORE)/ci_app.c
CI_INC ?= -I../ci

ntt_host: $(SRCS) $(wildcard $(CORE)/*.h) $(wildcard ../ci/*.h)
	$(CC) $(CFLAGS) $(CI_INC) -o $@ $(SRCS)

test: ntt_host
	./ntt_host --sw all

# the committed golden vectors through the software chip (docs/ci-emulator.md)
ci-golden: $(SRCS) $(wildcard $(CORE)/*.h) $(wildcard ../ci/golden/*.h)
	$(CC) $(CFLAGS) -I../ci/golden -o ntt_host_ci $(SRCS)
	./ntt_host_ci --sw ci | tee ci_golden.out
	grep -qx 'ci: PASS' ci_golden.out

clean:
	rm -f ntt_host ntt_host_ci ci_golden.out

.PHONY: test ci-golden clean
