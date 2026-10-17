// SPDX-License-Identifier: Apache-2.0
// Software chip (Spec 1): a pin-level stand-in for tt_um_NTT_Core. It follows the same protocol and
// fixed output delays (timing.h) and the chip-level arithmetic (ntt_ref.h), counts butterflies and
// picks its own constants like the chip, and records protocol violations instead of hiding them.
#ifndef SWCHIP_H
#define SWCHIP_H

#include <stdint.h>

#include "ntt_chip.h"

typedef struct {
    int active, op;
    uint32_t clk;               // chip clocks since reset
    uint32_t unit_start;        // clock of the current unit's first input
    int n_got;                  // inputs collected for the current unit
    uint16_t in[4];
    uint32_t units_done;        // butterflies / pieces / coefficients of this job
    uint8_t out[64];            // result bytes by clock (ring)
    uint32_t out_clk[64];
    uint32_t violations;        // protocol errors seen (wrong offsets, coefficient >= 3329, ...)
    const char *last_violation;
} swchip_t;

void swchip_init(swchip_t *s);
tt_io_t swchip_io(swchip_t *s);     // a transport that drives this software chip

#endif
