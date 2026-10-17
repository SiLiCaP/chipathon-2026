// SPDX-License-Identifier: Apache-2.0
// Host harness for the NTT chip (Spec 1): builds the per-clock input words of a job, runs them
// through a transport and picks the result bytes out at their fixed clock positions.
//
// The same code runs on the RP2350 (PIO + DMA transport), on a PC against the software chip, and on
// a PC against the Tang Nano FPGA emulator. Only the transport differs.
#ifndef NTT_CHIP_H
#define NTT_CHIP_H

#include <stdint.h>

#include "ntt_ref.h"

// One pin word per chip clock: {uio_in, ui_in}. ui_in = coeff[7:0]; uio[3:0] = coeff[11:8];
// uio[4] = valid; uio[5] = sof; uio[7:6] = op.
#define TT_WORD(c, valid, sof, op) \
    ((uint16_t)(((c) & 0xFFF) | ((valid) << 12) | ((sof) << 13) | ((op) << 14)))

enum { OP_FWD = 0, OP_INV = 1, OP_BM = 2, OP_SC = 3 };

// A transport plays words[0..n-1], one per chip clock, and stores uo_out of every clock in uo[0..n-1]
// (sampled just before the rising edge that captures the word). Returns 0 or a negative error.
typedef struct tt_io tt_io_t;
struct tt_io {
    int (*run)(tt_io_t *io, const uint16_t *words, uint32_t n, uint8_t *uo);
    int (*reset)(tt_io_t *io, uint32_t cycles);   // rst_n low for `cycles` clocks, inputs idle
    const char *name;
    void *ctx;
};

// Longest job: basemul = 128 x P_B + D_B + 3 clocks (655), plus margin for the sample skew.
#define NTT_WORDS_MAX 2048

enum { NTT_OK = 0, NTT_ERR_RANGE = -1, NTT_ERR_IO = -2 };

typedef struct {
    tt_io_t *io;
    int skew;                   // extra clocks between the chip and our sampling (0 unless calibrated)
    uint32_t clocks;            // chip clocks of the last call, layer waits included
    uint64_t clocks_total;
    uint32_t calls, faults;
    void (*on_layer)(void *arg, int op, int layer, const uint16_t f[NTT_N]);   // optional, NTT jobs
    void *on_layer_arg;
    uint16_t words[NTT_WORDS_MAX];
    uint8_t uo[NTT_WORDS_MAX];
} ntt_chip_t;

void ntt_chip_init(ntt_chip_t *c, tt_io_t *io);
int ntt_chip_reset(ntt_chip_t *c);

// The four calls of Spec 1. Each takes and returns 256 coefficients (out may alias an input), checks
// that every result is below 3329, and on a fault resets the chip and returns NTT_ERR_RANGE.
int ntt_chip_forward(ntt_chip_t *c, const uint16_t in[NTT_N], uint16_t out[NTT_N]);
int ntt_chip_inverse(ntt_chip_t *c, const uint16_t in[NTT_N], uint16_t out[NTT_N]);   // before the scale
int ntt_chip_basemul(ntt_chip_t *c, const uint16_t f[NTT_N], const uint16_t g[NTT_N], uint16_t out[NTT_N]);
int ntt_chip_scale(ntt_chip_t *c, const uint16_t in[NTT_N], uint16_t out[NTT_N]);

const char *ntt_err_str(int err);

#endif
