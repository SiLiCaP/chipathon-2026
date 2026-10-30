// SPDX-License-Identifier: Apache-2.0
// The pin transport: plays 16-bit pin words, one per chip clock, and samples uo_out on every clock.
// Implementations: PIO + DMA on the RP2350 (rp/tt_rp.c), a USB Tang Nano (pc/pc_main.c), the software chip.
#ifndef TT_IO_H
#define TT_IO_H

#include <stdint.h>

// One pin word per chip clock: {uio_in, ui_in}. ui_in = data byte; uio[3:0] = 0xF marks a command;
// uio[4] = valid; uio[5] = command bit 2; uio[7:6] = command bits 1:0.
#define TT_WORD(c, valid, sof, op) \
    ((uint16_t)(((c) & 0xFFF) | ((valid) << 12) | ((sof) << 13) | ((op) << 14)))

// A transport plays words[0..n-1], one per chip clock, and stores uo_out of every clock in uo[0..n-1]
// (sampled just before the rising edge that captures the word). Returns 0 or a negative error.
// The clock stops between calls; the chip keeps its state.
typedef struct tt_io tt_io_t;
struct tt_io {
    int (*run)(tt_io_t *io, const uint16_t *words, uint32_t n, uint8_t *uo);
    int (*reset)(tt_io_t *io, uint32_t cycles);   // rst_n low for `cycles` clocks, inputs idle
    const char *name;
    void *ctx;
    uint32_t conv_cyc;          // set by run(): cycles spent reformatting words (0 if the pins take them as is)
};

#endif
