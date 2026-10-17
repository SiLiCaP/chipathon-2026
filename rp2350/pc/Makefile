# PC build of the RP2350 harness core (any C compiler):
#   make -C rp2350/pc            build ntt_host
#   make -C rp2350/pc test       all tests against the software chip
CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror
CORE := ../core
SRCS := pc_main.c $(CORE)/ntt_ref.c $(CORE)/ntt_chip.c $(CORE)/swchip.c $(CORE)/app.c $(CORE)/oplog.c

ntt_host: $(SRCS) $(wildcard $(CORE)/*.h)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

test: ntt_host
	./ntt_host --sw all

clean:
	rm -f ntt_host

.PHONY: test clean
