// SPDX-License-Identifier: Apache-2.0
// Test programs on top of the hash driver, shared by the RP2350 firmware and the PC build.
#ifndef HASH_APP_H
#define HASH_APP_H

#include <stdint.h>

#include "hash_chip.h"

// Each returns 0 on PASS, nonzero on FAIL, and prints a report.
int app_selftest(hash_chip_t *c);                 // known answers + byte/burst/skip/CBD sweeps vs the C reference
int app_fuzz(hash_chip_t *c, uint32_t n, uint32_t seed);   // n random jobs of every kind vs the C reference
int app_calibrate(hash_chip_t *c);                // find the sample skew at which the known answers pass

// The software reference of one job (sha3_sw.c): what the chip must return. outlen as in hash_chip_job.
void hash_ref(hash_kind_t kind, const uint8_t *msg, uint32_t len, uint8_t *out, uint32_t outlen);

#endif
