// SPDX-License-Identifier: Apache-2.0
// Software chip: a pin-level stand-in for tt_um_NTT_Core (the hash-only chip, K = 8). It follows the same
// protocol and fixed output delay as src/hash_io.v (docs/info.md), uses sha3_sw.c's Keccak-f[1600] for the
// permutation, and records protocol violations (a command while the chip is busy, SKIP off a lane boundary,
// a read past the state) instead of hiding them.
#ifndef HASH_SWCHIP_H
#define HASH_SWCHIP_H

#include <stdint.h>

#include "tt_io.h"

typedef struct {
    uint64_t st[25];            // the Keccak state
    unsigned ptr;               // byte pointer
    uint32_t clk;               // chip clocks since reset
    uint32_t busy;              // clocks still busy (PERMUTE, CLEAR)
    uint32_t burst_left;        // bytes still to stream after the current clock
    int cbd_on, eta3;           // mode of the running burst
    unsigned p3;                // eta = 3: byte position in its triple (survives PERMUTE, restarts at CLEAR)
    uint8_t left2, left4;
    uint8_t out[8];             // squeezed bytes by clock (ring)
    uint32_t out_clk[8];
    uint32_t violations;
    const char *last_violation;
} hash_swchip_t;

void hash_swchip_init(hash_swchip_t *s);
tt_io_t hash_swchip_io(hash_swchip_t *s);     // a transport that drives this software chip

#endif
