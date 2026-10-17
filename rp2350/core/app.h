// SPDX-License-Identifier: Apache-2.0
// Test and demo programs on top of the harness, shared by the RP2350 firmware and the PC build.
#ifndef APP_H
#define APP_H

#include <stdint.h>

#include "ntt_chip.h"

// Supplied by the platform (rp/main.c, pc/pc_main.c)
uint64_t plat_time_us(void);     // monotonic microseconds
uint32_t plat_chip_hz(void);     // the chip clock, 0 if unknown
uint32_t plat_rand32(void);

// Each returns 0 on PASS, nonzero on FAIL, and prints a report.
int app_selftest(ntt_chip_t *c);                 // reference known answers + chip known answers
int app_demo(ntt_chip_t *c, uint32_t seed);      // Spec 1 demo: f * g on the chip vs schoolbook
int app_fuzz(ntt_chip_t *c, uint32_t n, uint32_t seed);   // every mode vs the chip-level model, edge vectors
int app_matvec(ntt_chip_t *c, uint32_t seed);    // stretch goal: k = 2 matrix-vector product
int app_bench(ntt_chip_t *c, uint32_t n);        // timing of the four calls and of a whole multiply
int app_calibrate(ntt_chip_t *c);                // find the sample skew at which the known answers pass

#endif
