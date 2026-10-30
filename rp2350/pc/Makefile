# PC build of the RP2350 harness core (any C compiler):
#   make -C rp2350/pc            build hash_host
#   make -C rp2350/pc test       self test + fuzz against the software chip
#   make -C rp2350/pc ci-golden  the committed golden ML-KEM hash jobs (docs/ci-emulator.md) on the software chip
CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror
CORE := ../core
SRCS := pc_main.c $(CORE)/hash_chip.c $(CORE)/hash_swchip.c $(CORE)/hash_app.c $(CORE)/ci_app.c $(CORE)/sha3_sw.c
CI_INC ?= -I../ci

hash_host: $(SRCS) $(wildcard $(CORE)/*.h) $(wildcard ../ci/*.h)
	$(CC) $(CFLAGS) $(CI_INC) -o $@ $(SRCS)

test: hash_host
	./hash_host --sw all

# the committed golden vectors through the software chip
ci-golden: $(SRCS) $(wildcard $(CORE)/*.h) $(wildcard ../ci/golden/*.h)
	$(CC) $(CFLAGS) -I../ci/golden -o hash_host_ci $(SRCS)
	./hash_host_ci --sw ci | tee ci_golden.out
	grep -qx 'ci: PASS' ci_golden.out

clean:
	rm -f hash_host hash_host_ci ci_golden.out

.PHONY: test ci-golden clean
