// SPDX-License-Identifier: Apache-2.0
// Software chip: a pin-level stand-in for tt_um_NTT_Core (the hash-only chip, K = 8). It follows the same
// protocol and output timing as src/hash_io.v + sample_ntt.v (docs/info.md), clock for clock, including the
// valid and busy pins of framing mode 1. It uses sha3_sw.c's Keccak-f[1600] for the permutation, and records
// protocol violations (a command while the chip is busy, a burst length outside 1..21, a word right after MODE,
// SKIP off a lane boundary, a read past the state) instead of hiding them.
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
    uint32_t raw_left;          // ABSORB_N: data bytes still to take
    int fin_on, fin_dom, fin_end;   // FINISH: running, domain byte done, PERMUTE is next
    unsigned fin_alg;
    int cbd_on, ntt_on, eta3;   // mode of the running burst
    unsigned p3;                // eta = 3: byte position in its triple (survives PERMUTE, restarts at CLEAR)
    uint8_t left2, left4, f0;
    // blocks that follow a burst by themselves
    unsigned ap_gap, ap_cnt, ap_n;
    int ap_wait, ap_inf;
    // SampleNTT (restarts at CLEAR)
    unsigned s_ph, s_cnt;
    int s_na;
    uint16_t s_acc;
    uint8_t s_c0, s_c1;
    // framing mode: the value now, and one clock ago (a word is framed by the mode of two clocks before)
    int mode, mode_prev;
    // what leaves the chip, by clock (rings)
    uint8_t out[8];             // a byte on uo_out
    uint32_t out_clk[8];
    uint8_t out_ntt[8];         // that byte comes from the sampler (it also counts as busy)
    uint8_t flag[8];            // bit 0: mode 1 (the chip drives uio[1:0]), bit 1: busy
    uint32_t flag_clk[8];
    uint32_t violations;
    const char *last_violation;
} hash_swchip_t;

void hash_swchip_init(hash_swchip_t *s);
tt_io_t hash_swchip_io(hash_swchip_t *s);     // a transport that drives this software chip

#endif
