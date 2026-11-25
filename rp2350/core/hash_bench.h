// SPDX-License-Identifier: Apache-2.0
// Speed tests for the hash chip, shared by the RP2350 firmware and the PC build:
//   app_bench      ML-KEM-512's hash work (the 32 calls of one key exchange) and raw absorb / squeeze rates, timed
//                  with the host's cycle counter: on the chip (host + pins + chip, the firmware as written) and in
//                  software on the same host (sha3_sw.c). Every chip result is checked against the software one.
//   app_speedtest  first finds how fast the pins run (a known-answer set at every clock and sample skew: the wire
//                  speed of this board and chip), then runs app_bench at the fastest clock that passes with margin.
// Both print a readable report plus machine lines (BENCH / SWEEP / SPEEDTEST key=value) for
// rp2350/speedtest.py, which saves them as JSON and Markdown.
#ifndef HASH_BENCH_H
#define HASH_BENCH_H

#include <stdint.h>

#include "hash_chip.h"

typedef struct {
    uint32_t (*cycles)(void);           // free-running counter (RP2350: DWT cycle counter; wraps, deltas only)
    uint32_t cyc_hz;                    // its rate (RP2350: the system clock)
    uint32_t (*set_clock)(uint32_t hz); // chip clock; returns the clock it got. NULL: fixed (PC, software chip)
    uint32_t (*clock_hz)(void);         // the chip clock now; NULL: unknown
    const char *host;                   // "RP2350 150 MHz", "PC"
} bench_plat_t;

// How the bench drives the chip (same workload, same checks):
//   BENCH_NEW     every newer command: packets + on-chip padding + one burst word, CBD and SampleNTT on chip
//                 (framing mode 1, which samples 16 bits per clock for the valid / busy pins)
//   BENCH_MODE0   the same commands in framing mode 0 (8-bit samples), SampleNTT on the RP2350
//   BENCH_LEGACY  the October 2026 way: host padding, a word per byte (+ SKIP), a burst word per block, CBD on chip,
//                 SampleNTT on the RP2350
enum { BENCH_NEW = 0, BENCH_LEGACY = 1, BENCH_MODE0 = 2 };
int app_bench(hash_chip_t *c, const bench_plat_t *p);       // BENCH_NEW
int app_bench_style(hash_chip_t *c, const bench_plat_t *p, int style);
// max_hz: the fastest clock to try (0: up to what the host can make, 75 MHz at a 150 MHz RP2350)
int app_speedtest(hash_chip_t *c, const bench_plat_t *p, uint32_t max_hz);

#endif
