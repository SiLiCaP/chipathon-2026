// SPDX-License-Identifier: Apache-2.0
// Host driver for the hash-only chip (Keccak ring K = 8 + hash_io adapter + on-chip SamplePolyCBD).
// It builds the per-clock pin words of a sponge job exactly like test/hash_jobs.py (HashJob.words), runs them
// through a transport (tt_io.h) and picks the squeezed bytes out at their fixed clock positions.
//
// The same code runs on the RP2350 (PIO + DMA transport), on a PC against the software chip
// (hash_swchip.c) and on a PC against the Tang Nano FPGA emulator. Only the transport differs.
// Pin protocol: docs/info.md. Anything that changes it must change this file, hash_swchip.c and
// docs/ci-emulator.md in the same push (the host CI runs this firmware against the chip's gate netlist).
#ifndef HASH_CHIP_H
#define HASH_CHIP_H

#include <stdint.h>

#include "tt_io.h"

// chip commands (uio[5], uio[7:6] = cmd bits 2, 1:0; data byte on ui[7:0])
enum { HC_ABSORB = 0, HC_SQUEEZE = 1, HC_PERMUTE = 2, HC_CLEAR = 3, HC_SKIP = 4, HC_BURST = 5, HC_BURST_CBD = 6 };
#define HC_WORD(cmd, data) TT_WORD(0xF00 | ((data) & 0xFF), 1, (cmd) >> 2, (cmd) & 3)

// K = 8 core: CLEAR 8 busy clocks, PERMUTE 368 (M + 24 (2M - 1), M = 8); a squeezed byte is on uo_out 2 clocks
// after the word that asked for it.
#define HC_CLEAR_BUSY 8
#define HC_PERM_BUSY 368
#define HC_SQ_DELAY 2

// What a job computes. The first four are the contract-2 kinds (docs/ci-emulator.md); the PRF kinds are SHAKE256
// whose output goes through BURST_CBD on the way out: ML-KEM's PRF_eta + SamplePolyCBD_eta in one chip command.
typedef enum {
    HASH_SHA3_256 = 0, HASH_SHA3_512 = 1, HASH_SHAKE128 = 2, HASH_SHAKE256 = 3,
    HASH_PRF_CBD2 = 4,          // out: 128 bytes, {f[2i+1], f[2i]} per byte (4-bit two's complement)
    HASH_PRF_CBD3 = 5,          // out: 192 bytes, {0,f0} {0,f1} {f3,f2} per three bytes
    HASH_NKIND = 6
} hash_kind_t;

enum { HASH_OK = 0, HASH_ERR_ARG = -1, HASH_ERR_IO = -2, HASH_ERR_QUIET = -3 };

// job options
#define HASH_BYTEWISE 1u        // squeeze with one SQUEEZE word per byte instead of BURST
#define HASH_SKIP 2u            // absorb all-zero lanes with SKIP

#define HASH_WORDS_MAX 1024     // longest transport call: one block (a rate of absorb words + PERMUTE + 368 busy)

typedef struct {
    tt_io_t *io;
    int skew;                   // extra clocks between the chip and our sampling (0 unless calibrated)
    uint64_t clocks_total;      // chip clocks played
    uint32_t calls, faults;
    uint16_t words[HASH_WORDS_MAX];
    uint8_t uo[HASH_WORDS_MAX];
} hash_chip_t;

void hash_chip_init(hash_chip_t *c, tt_io_t *io);
int hash_chip_reset(hash_chip_t *c);

// Output length of a kind with a fixed output (SHA3: the digest, PRF kinds: 64 * eta), else 0.
uint32_t hash_kind_fixed_len(hash_kind_t kind);
const char *hash_kind_name(hash_kind_t kind);

// One job on the chip: CLEAR, absorb msg, squeeze outlen bytes (PRF kinds ignore outlen and give 64 * eta).
// Squeezes whole lanes and copies outlen bytes. Fails with HASH_ERR_QUIET if uo_out is nonzero when no byte
// is due (the chip must be quiet between commands). The chip is left in a defined state (next job starts with CLEAR).
int hash_chip_job(hash_chip_t *c, hash_kind_t kind, const uint8_t *msg, uint32_t len, uint8_t *out, uint32_t outlen,
                  unsigned opts);

const char *hash_err_str(int err);

#endif
