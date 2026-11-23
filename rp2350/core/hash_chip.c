// SPDX-License-Identifier: Apache-2.0
// Host driver for the hash-only chip. See hash_chip.h. Mirrors test/hash_jobs.py HashJob.words() word for word.
#include "hash_chip.h"

#include <string.h>

static const struct { unsigned rate; uint8_t dom; uint32_t fixed; const char *name; } KIND[HASH_NKIND] = {
    [HASH_SHA3_256] = {136, 0x06, 32, "sha3-256"},
    [HASH_SHA3_512] = {72, 0x06, 64, "sha3-512"},
    [HASH_SHAKE128] = {168, 0x1F, 0, "shake128"},
    [HASH_SHAKE256] = {136, 0x1F, 0, "shake256"},
    [HASH_PRF_CBD2] = {136, 0x1F, 128, "prf-cbd2"},
    [HASH_PRF_CBD3] = {136, 0x1F, 192, "prf-cbd3"},
};

uint32_t hash_kind_fixed_len(hash_kind_t kind) { return (unsigned)kind < HASH_NKIND ? KIND[kind].fixed : 0; }
const char *hash_kind_name(hash_kind_t kind) { return (unsigned)kind < HASH_NKIND ? KIND[kind].name : "?"; }

void hash_chip_init(hash_chip_t *c, tt_io_t *io) {
    memset(c, 0, sizeof *c);
    c->io = io;
}

int hash_chip_reset(hash_chip_t *c) { return c->io->reset(c->io, 10) ? HASH_ERR_IO : HASH_OK; }

const char *hash_err_str(int err) {
    switch (err) {
    case HASH_OK: return "ok";
    case HASH_ERR_ARG: return "bad argument";
    case HASH_ERR_IO: return "transport error";
    case HASH_ERR_QUIET: return "uo_out not quiet between commands (hardware fault)";
    default: return "unknown error";
    }
}

// Play c->words[0..n-1] (n already includes any trailing idle clocks for the sample delay).
static int play(hash_chip_t *c, uint32_t n) {
    if (c->io->run(c->io, c->words, n, c->uo)) return HASH_ERR_IO;
    c->clocks_total += n;
    c->calls++;
    return HASH_OK;
}

// uo_out must be 0 on every clock except the `take` clocks from d0 on, where the squeezed bytes are.
static int check_quiet(hash_chip_t *c, uint32_t n, uint32_t d0, uint32_t take) {
    for (uint32_t k = 0; k < n; k++)
        if (c->uo[k] && !(k >= d0 && k < d0 + take)) {
            c->faults++;
            hash_chip_reset(c);
            return HASH_ERR_QUIET;
        }
    return HASH_OK;
}

unsigned hash_fin_steps(unsigned p, unsigned rate) {
    if (p / 8 == rate / 8 - 1) return 8 - p % 8;                 // already in the last lane
    return (8 - p % 8) + (rate / 8 - 2 - p / 8) + 8;              // rest of the lane, a SKIP per lane, the last lane
}

// Words that absorb cnt bytes: ABSORB words (SKIP for a whole all-zero lane if asked), or ABSORB_N packets.
static uint32_t put_bytes(hash_chip_t *c, uint32_t n, const uint8_t *b, unsigned cnt, unsigned opts) {
    if (opts & HASH_RAW) {
        for (unsigned i = 0; i < cnt; i += HC_RAW_MAX) {
            const unsigned part = cnt - i < HC_RAW_MAX ? cnt - i : HC_RAW_MAX;
            c->words[n++] = HC_WORD(HC_EXT, HC_EXT_ABSORB_N | (part - 1));
            for (unsigned k = 0; k < part; k++) c->words[n++] = b[i + k];      // the bare byte: no valid, no marker
        }
        return n;
    }
    for (unsigned ln = 0; ln < cnt; ln += 8) {
        const unsigned part = cnt - ln < 8 ? cnt - ln : 8;
        int any = 0;
        for (unsigned k = 0; k < part; k++) any |= b[ln + k];
        if ((opts & HASH_SKIP) && part == 8 && !any) c->words[n++] = HC_WORD(HC_SKIP, 0);
        else for (unsigned k = 0; k < part; k++) c->words[n++] = HC_WORD(HC_ABSORB, b[ln + k]);
    }
    return n;
}

// One transport call is one block, so the clock budget of the longest block must fit the buffers:
// CLEAR + 168 absorb words (+ 3 packet headers) + PERMUTE + 368 busy, or the last block with FINISH
// (167 bytes + 3 headers + FINISH + 35 steps + PERMUTE + 368 busy) followed by 168 squeeze clocks + delay.
_Static_assert(1 + HC_CLEAR_BUSY + 168 + 3 + 1 + 35 + 1 + HC_PERM_BUSY + 168 + HC_SQ_DELAY + 4 < HASH_WORDS_MAX,
               "block does not fit HASH_WORDS_MAX");

int hash_chip_job(hash_chip_t *c, hash_kind_t kind, const uint8_t *msg, uint32_t len, uint8_t *out, uint32_t outlen,
                  unsigned opts) {
    if (c->skew < 0 || c->skew > 4) return HASH_ERR_ARG;
    if ((unsigned)kind >= HASH_NKIND || (!msg && len) || !out) return HASH_ERR_ARG;
    const unsigned rate = KIND[kind].rate;
    const int cbd_eta = kind == HASH_PRF_CBD2 ? 2 : kind == HASH_PRF_CBD3 ? 3 : 0;
    if (cbd_eta) outlen = KIND[kind].fixed;
    else if (KIND[kind].fixed && outlen > KIND[kind].fixed) return HASH_ERR_ARG;   // SHA3 digests are fixed
    if (outlen == 0) return HASH_OK;

    // ---- absorb: CLEAR, then the padded message block by block; each block ends with PERMUTE + busy ----
    const uint32_t padded = (len / rate + 1) * rate;       // message + domain byte + zeros + 0x80, whole blocks
    uint32_t n = 0;
    c->words[n++] = HC_WORD(HC_CLEAR, 0);
    for (int i = 0; i < HC_CLEAR_BUSY; i++) c->words[n++] = 0;
    if (opts & HASH_FINISH) {
        // the chip pads: the message's whole blocks, then what is left and FINISH (pads, permutes)
        const uint32_t full = len / rate * rate;
        for (uint32_t base = 0; base < full; base += rate) {
            n = put_bytes(c, n, msg + base, rate, opts);
            c->words[n++] = HC_WORD(HC_PERMUTE, 0);
            for (int i = 0; i < HC_PERM_BUSY; i++) c->words[n++] = 0;
            int err = play(c, n) ? HASH_ERR_IO : check_quiet(c, n, 0, 0);
            if (err) return err;
            n = 0;
        }
        if (len > full) n = put_bytes(c, n, msg + full, len - full, opts);
        const unsigned fn = kind == HASH_SHA3_256 ? 0 : kind == HASH_SHA3_512 ? 1 : kind == HASH_SHAKE128 ? 2 : 3;
        c->words[n++] = HC_WORD(HC_EXT, HC_EXT_FINISH | fn);
        const unsigned wait = hash_fin_steps(len - full, rate) + 1 + HC_PERM_BUSY;
        for (unsigned i = 0; i < wait; i++) c->words[n++] = 0;
    } else {
        for (uint32_t base = 0; base < padded; base += rate) {
            uint8_t blk[168] = {0};    // fully written below; GCC 14 -O2 can't prove it (-Werror=maybe-uninitialized)
            for (unsigned b = 0; b < rate; b++) {
                const uint32_t i = base + b;
                uint8_t v = i < len ? msg[i] : 0;
                if (i == len) v ^= KIND[kind].dom;
                if (i == padded - 1) v |= 0x80;
                blk[b] = v;
            }
            n = put_bytes(c, n, blk, rate, opts);
            c->words[n++] = HC_WORD(HC_PERMUTE, 0);
            for (int i = 0; i < HC_PERM_BUSY; i++) c->words[n++] = 0;
            if (base + rate < padded) {                         // next block needs a fresh buffer
                int err = play(c, n) ? HASH_ERR_IO : check_quiet(c, n, 0, 0);
                if (err) return err;
                n = 0;
            }
        }
    }

    // ---- squeeze: one transport call per rate block, PERMUTE + busy before every block but the first ----
    uint32_t got = 0;
    while (got < outlen) {
        const uint32_t rem = (outlen - got + 7) / 8 * 8;
        const uint32_t take = rem < rate ? rem : rate;     // bytes this block, whole lanes
        if (got) {
            c->words[n++] = HC_WORD(HC_PERMUTE, 0);
            for (int i = 0; i < HC_PERM_BUSY; i++) c->words[n++] = 0;
        }
        const uint32_t first = n;                           // word k asks for byte k - first (burst: the first byte)
        if ((opts & HASH_BYTEWISE) && !cbd_eta) {
            for (uint32_t i = 0; i < take; i++) c->words[n++] = HC_WORD(HC_SQUEEZE, 0);
        } else {
            const unsigned lanes = take / 8;
            if (cbd_eta) c->words[n++] = HC_WORD(HC_BURST_CBD, lanes | (cbd_eta == 3 ? 0x80 : 0));
            else c->words[n++] = HC_WORD(HC_BURST, lanes);
            for (uint32_t i = 1; i < take; i++) c->words[n++] = 0;
        }
        for (int i = 0; i < HC_SQ_DELAY + c->skew; i++) c->words[n++] = 0;   // the last byte's sample
        if (n > HASH_WORDS_MAX) return HASH_ERR_ARG;
        int err = play(c, n);
        if (err) return err;
        // byte i of this block is asked for by word first + i (bytewise) or first (burst: i-th clock), due at +2
        const uint32_t d0 = first + HC_SQ_DELAY + (uint32_t)c->skew;
        if ((err = check_quiet(c, n, d0, take))) return err;
        for (uint32_t i = 0; i < take && got + i < outlen; i++) out[got + i] = c->uo[d0 + i];
        got += take;
        n = 0;
    }
    return HASH_OK;
}
