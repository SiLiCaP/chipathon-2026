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

// What the chip drives on uio[1:0] in framing mode 1 (docs/info.md), as sampled next to uo_out:
#define TT_AUX_VALID 1u         // uio[0]: uo_out carries a byte this clock
#define TT_AUX_BUSY 2u          // uio[1]: the chip takes no command

// A transport plays words[0..n-1], one per chip clock, and stores uo_out of every clock in uo[0..n-1]
// (sampled just before the rising edge that captures the word). With aux != NULL it also stores the chip's
// uio[1:0] of every clock in aux[0..n-1] (TT_AUX_*; 0 where the chip does not drive them).
// Returns 0 or a negative error. The clock stops between calls; the chip keeps its state.
typedef struct tt_io tt_io_t;
struct tt_io {
    int (*run)(tt_io_t *io, const uint16_t *words, uint32_t n, uint8_t *uo, uint8_t *aux);
    int (*reset)(tt_io_t *io, uint32_t cycles);   // rst_n low for `cycles` clocks, inputs idle
    // Stop (1) or resume (0) driving uio[1:0] from the host side: the chip drives them in framing mode 1.
    // NULL: this transport cannot sample uio, so mode 1 is not available on it.
    int (*uio_release)(tt_io_t *io, int release);
    const char *name;
    void *ctx;
    uint32_t conv_cyc;          // set by run(): cycles spent reformatting words (0 if the pins take them as is)
};

#endif
