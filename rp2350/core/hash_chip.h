// SPDX-License-Identifier: Apache-2.0
// Host driver for the hash-only chip (Keccak ring K = 8 + hash_io adapter + on-chip SamplePolyCBD and SampleNTT).
// It builds the per-clock pin words of a sponge job exactly like test/hash_jobs.py (HashJob.words), runs them
// through a transport (tt_io.h) and picks the squeezed bytes out at their fixed clock positions (or, for
// SampleNTT, at the clocks the chip marks valid).
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
enum { HC_ABSORB = 0, HC_SQUEEZE = 1, HC_PERMUTE = 2, HC_CLEAR = 3, HC_SKIP = 4, HC_BURST = 5, HC_BURST_CBD = 6,
       HC_EXT = 7 };
#define HC_WORD(cmd, data) TT_WORD(0xF00 | ((data) & 0xFF), 1, (cmd) >> 2, (cmd) & 3)      // framing mode 0
#define HC_WORD1(cmd, data) TT_WORD((data) & 0xFF, 1, (cmd) >> 2, (cmd) & 3)               // mode 1: no marker
// EXT sub-commands, data[7:6] (src/hash_io.v):
#define HC_EXT_ABSORB_N 0x40    // | (n - 1): the next n (1..64) clocks' ui bytes are absorbed as they are
#define HC_EXT_FINISH 0x80      // | function (0 SHA3-256, 1 SHA3-512, 2 SHAKE128, 3 SHAKE256): the chip pads + permutes
#define HC_EXT_MODE 0xC0        // | framing mode (1: no marker, uio[0] = valid out, uio[1] = busy out, packed eta 3)
#define HC_BURST_NTT 0x40       // BURST_CBD data bit 6: SampleNTT instead of CBD (needs mode 1: the valid pin)
#define HC_BURST_AUTO 0x20      // BURST_CBD data bit 5 (BURST: data[7:5] = how many): the chip permutes and
                                // sends the next blocks by itself
#define HC_RAW_MAX 64

// K = 8 core: CLEAR 8 busy clocks, PERMUTE 368 (M + 24 (2M - 1), M = 8); a squeezed byte is on uo_out 2 clocks
// after the word that asked for it. A block that follows by itself starts HC_AUTO_GAP + 1 + HC_PERM_BUSY clocks
// after the last byte of the one before was asked for. SampleNTT bytes leave up to HC_NTT_TAIL clocks late.
#define HC_CLEAR_BUSY 8
#define HC_PERM_BUSY 368
#define HC_SQ_DELAY 2
#define HC_AUTO_GAP 3
#define HC_NTT_TAIL 3

// What a job computes. The first four are the contract-2 kinds (docs/ci-emulator.md); the PRF kinds are SHAKE256
// whose output goes through BURST_CBD on the way out: ML-KEM's PRF_eta + SamplePolyCBD_eta in one chip command.
typedef enum {
    HASH_SHA3_256 = 0, HASH_SHA3_512 = 1, HASH_SHAKE128 = 2, HASH_SHAKE256 = 3,
    HASH_PRF_CBD2 = 4,          // out: 128 bytes, {f[2i+1], f[2i]} per byte (4-bit two's complement)
    HASH_PRF_CBD3 = 5,          // out: 192 bytes, {0,f0} {0,f1} {f3,f2} per three bytes (in either framing mode)
    HASH_XOF_NTT = 6,           // SHAKE128 + SampleNTT: out = 384 bytes, ByteEncode_12 of the polynomial. Mode 1 only
    HASH_NKIND = 7
} hash_kind_t;

enum { HASH_OK = 0, HASH_ERR_ARG = -1, HASH_ERR_IO = -2, HASH_ERR_QUIET = -3, HASH_ERR_VALID = -4, HASH_ERR_NTT = -5 };

// job options
#define HASH_BYTEWISE 1u        // squeeze with one SQUEEZE word per byte instead of BURST
#define HASH_SKIP 2u            // absorb all-zero lanes with SKIP
#define HASH_FINISH 4u          // the chip pads the last block (FINISH) instead of the host
#define HASH_RAW 8u             // absorb with ABSORB_N packets: bare data bytes, one header word per 64 (no SKIP)
#define HASH_AUTO 16u           // one burst word for the whole squeeze: the chip permutes between the blocks

#define HASH_WORDS_MAX 4096     // longest transport call (an absorb block, or a squeeze of several blocks)
#define HASH_WIN_MAX 256        // output windows tracked per transport call

typedef struct {
    tt_io_t *io;
    int skew;                   // extra clocks between the chip and our sampling (0 unless calibrated)
    int mode;                   // the chip's framing mode (hash_chip_set_mode)
    uint64_t clocks_total;      // chip clocks played
    uint32_t calls, faults;
    uint16_t words[HASH_WORDS_MAX];
    uint8_t uo[HASH_WORDS_MAX];
    uint8_t aux[HASH_WORDS_MAX];    // mode 1: the chip's valid / busy pins per clock (TT_AUX_*)
    uint8_t due[HASH_WORDS_MAX];    // per clock: 0 nothing may come, 1 a byte is due, 2 a byte may come (valid says)
    uint32_t due_hw;                // due[] is all zero from here on (so begin() clears only what was used)
    // the clock windows that can carry a byte, in clock order, so unpacking skips the idle clocks
    uint16_t win_at[HASH_WIN_MAX], win_len[HASH_WIN_MAX];
    uint8_t win_what[HASH_WIN_MAX]; // the window's due value (1: a byte on every clock, 2: where valid says)
    uint32_t nwin;                  // HASH_WIN_MAX + 1: too many or out of order, unpack scans every clock
    uint32_t checked;               // clocks of the running call already checked (while the transport ran)
    int check_err;
} hash_chip_t;

void hash_chip_init(hash_chip_t *c, tt_io_t *io);
int hash_chip_reset(hash_chip_t *c);              // also returns the chip (and the host pins) to framing mode 0

// 1 if this transport can run framing mode 1 (it samples uio[1:0] and can stop driving them).
int hash_chip_has_mode1(const hash_chip_t *c);
// Switch the chip's framing mode (0 or 1). To mode 1: the MODE word (with the marker) and three idle words are
// played with the host still driving uio[1:0]; the idle words drive them low, which is what the chip drives there
// from 2 clocks after the word, so the two never fight. Then the host lets go.
int hash_chip_set_mode(hash_chip_t *c, int mode);

// Output length of a kind with a fixed output (SHA3: the digest, PRF kinds: 64 * eta, XOF_NTT: 384), else 0.
uint32_t hash_kind_fixed_len(hash_kind_t kind);
const char *hash_kind_name(hash_kind_t kind);

// One job on the chip: CLEAR, absorb msg, squeeze outlen bytes (kinds with a fixed output ignore outlen).
// Squeezes whole lanes and copies outlen bytes. Fails with HASH_ERR_QUIET if uo_out is nonzero when no byte
// is due, and in mode 1 with HASH_ERR_VALID if the valid pin disagrees with the clocks a byte is due on.
// The chip is left in a defined state (the next job starts with CLEAR).
int hash_chip_job(hash_chip_t *c, hash_kind_t kind, const uint8_t *msg, uint32_t len, uint8_t *out, uint32_t outlen,
                  unsigned opts);

// Padding steps of FINISH from byte pointer p in the block; the chip is then busy for these + 1 + HC_PERM_BUSY.
unsigned hash_fin_steps(unsigned p, unsigned rate);

const char *hash_err_str(int err);

#endif
