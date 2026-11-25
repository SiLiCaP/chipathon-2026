// SPDX-License-Identifier: Apache-2.0
// Host driver for the hash-only chip. See hash_chip.h. Mirrors test/hash_jobs.py HashJob.words() word for word.
#include "hash_chip.h"

#include <string.h>

#ifdef HASH_PROF
uint32_t (*hash_prof_cycles)(void);
uint64_t hash_prof[4];          // 0 play: transport run, 1 play: check after the run, 2 unpack loop, 3 begin()
#define PROF_T() (hash_prof_cycles ? hash_prof_cycles() : 0)
#else
#define PROF_T() 0
#endif

static const struct { unsigned rate; uint8_t dom; uint32_t fixed; uint8_t fn; const char *name; } KIND[HASH_NKIND] = {
    [HASH_SHA3_256] = {136, 0x06, 32, 0, "sha3-256"},
    [HASH_SHA3_512] = {72, 0x06, 64, 1, "sha3-512"},
    [HASH_SHAKE128] = {168, 0x1F, 0, 2, "shake128"},
    [HASH_SHAKE256] = {136, 0x1F, 0, 3, "shake256"},
    [HASH_PRF_CBD2] = {136, 0x1F, 128, 3, "prf-cbd2"},
    [HASH_PRF_CBD3] = {136, 0x1F, 192, 3, "prf-cbd3"},
    [HASH_XOF_NTT] = {168, 0x1F, 384, 2, "xof-ntt"},
};

uint32_t hash_kind_fixed_len(hash_kind_t kind) { return (unsigned)kind < HASH_NKIND ? KIND[kind].fixed : 0; }
const char *hash_kind_name(hash_kind_t kind) { return (unsigned)kind < HASH_NKIND ? KIND[kind].name : "?"; }

void hash_chip_init(hash_chip_t *c, tt_io_t *io) {
    memset(c, 0, sizeof *c);
    c->io = io;
}

int hash_chip_reset(hash_chip_t *c) {
    // rst_n clears the chip's framing mode, so it lets go of uio[1:0]; only then does the host drive them again
    const int err = c->io->reset(c->io, 10) ? HASH_ERR_IO : HASH_OK;
    c->mode = 0;
    if (c->io->uio_release && c->io->uio_release(c->io, 0)) return HASH_ERR_IO;
    return err;
}

int hash_chip_has_mode1(const hash_chip_t *c) { return c->io->uio_release != NULL; }

const char *hash_err_str(int err) {
    switch (err) {
    case HASH_OK: return "ok";
    case HASH_ERR_ARG: return "bad argument";
    case HASH_ERR_IO: return "transport error";
    case HASH_ERR_QUIET: return "uo_out not quiet between commands (hardware fault)";
    case HASH_ERR_VALID: return "the valid pin disagrees with the clocks a byte is due on (hardware fault)";
    case HASH_ERR_NTT: return "SampleNTT did not return exactly 384 bytes";
    default: return "unknown error";
    }
}

// a command word in the chip's current framing
static uint16_t cw(const hash_chip_t *c, unsigned cmd, unsigned data) {
    return c->mode ? HC_WORD1(cmd, data) : HC_WORD(cmd, data);
}

// Play c->words[0..n-1] (n already includes any trailing idle clocks for the sample delay) and check every clock
// against c->due: nothing where nothing is due; in mode 1 the valid pin exactly on the due clocks.
// Per-clock rule (exact): nothing where nothing is due; in mode 1 the valid pin exactly on the due clocks.
static int check_clock(const hash_chip_t *c, uint32_t k) {
    const int v = c->mode && (c->aux[k] & TT_AUX_VALID);
    if (c->due[k] == 0) return c->uo[k] ? HASH_ERR_QUIET : v ? HASH_ERR_VALID : HASH_OK;
    if (c->due[k] == 1) return c->mode && !v ? HASH_ERR_VALID : HASH_OK;
    return c->uo[k] && !v ? HASH_ERR_QUIET : HASH_OK;
}

// Four clocks at a time: bytes as 32-bit words. nz8 sets bit 7 of each byte that is nonzero.
static inline uint32_t ld32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
static inline uint32_t nz8(uint32_t x) { return (((x & 0x7F7F7F7Fu) + 0x7F7F7F7Fu) | x) & 0x80808080u; }

// 1 if clocks k..k+3 certainly pass check_clock (otherwise they are checked one by one)
static int clean4(const hash_chip_t *c, uint32_t k) {
    const uint32_t uw = ld32(c->uo + k);
    if (!c->mode) {
        if (!uw) return 1;                                      // the usual case: four quiet clocks
    } else if (!uw && !(ld32(c->aux + k) & 0x01010101u) && !ld32(c->due + k)) {
        return 1;                                               // quiet, not valid, nothing due
    }
    const uint32_t d = ld32(c->due + k), u = nz8(uw);
    const uint32_t is1 = ~nz8(d ^ 0x01010101u) & 0x80808080u;
    if (!c->mode) return (u & ~is1) == 0;                       // mode 0: only due clocks may carry a byte
    const uint32_t v = (ld32(c->aux + k) & 0x01010101u) << 7;    // valid pin, at bit 7 of each byte
    const uint32_t is0 = ~nz8(d) & 0x80808080u, is2 = ~nz8(d ^ 0x02020202u) & 0x80808080u;
    return !(is0 & (u | v)) && !(is1 & ~v) && !(is2 & u & ~v);
}

// Clocks k..n-1 of a transport call against c->due (check_clock's rule), fast where the clocks are quiet.
static int check_range(const hash_chip_t *c, uint32_t k, uint32_t n) {
    int err = HASH_OK;
    // 16 clocks at a time when all are quiet: mode 0 needs only uo_out = 0; mode 1 also no valid and nothing due
    for (; k + 16 <= n && !err; k += 16) {
        uint32_t q = ld32(c->uo + k) | ld32(c->uo + k + 4) | ld32(c->uo + k + 8) | ld32(c->uo + k + 12);
        if (c->mode)
            q |= ((ld32(c->aux + k) | ld32(c->aux + k + 4) | ld32(c->aux + k + 8) | ld32(c->aux + k + 12)) & 0x01010101u) |
                 ld32(c->due + k) | ld32(c->due + k + 4) | ld32(c->due + k + 8) | ld32(c->due + k + 12);
        if (!q) continue;
        for (uint32_t j = k; j < k + 16 && !err; j += 4)
            if (!clean4(c, j))
                for (uint32_t i = j; i < j + 4 && !err; i++) err = check_clock(c, i);
    }
    for (; k + 4 <= n && !err; k += 4)
        if (!clean4(c, k))
            for (uint32_t j = k; j < k + 4 && !err; j++) err = check_clock(c, j);
    for (; k < n && !err; k++) err = check_clock(c, k);
    return err;
}

// called by the transport while the chip runs: check the samples that have arrived, 16 clocks at a time
static void play_progress(void *ctx, uint32_t done) {
    hash_chip_t *c = ctx;
    const uint32_t upto = done & ~15u;
    if (c->check_err || upto <= c->checked) return;
    c->check_err = check_range(c, c->checked, upto);
    c->checked = upto;
}

static int play(hash_chip_t *c, uint32_t n) {
    if (n > HASH_WORDS_MAX) return HASH_ERR_ARG;
    uint32_t pt0 = PROF_T();
    c->checked = 0;
    c->check_err = HASH_OK;
    if (!c->mode) {                     // mode 0: the samples land in c->uo as they come, check them meanwhile
        c->io->progress = play_progress;
        c->io->progress_ctx = c;
    }
    const int ioerr = c->io->run(c->io, c->words, n, c->uo, c->mode ? c->aux : NULL);
    c->io->progress = NULL;
    if (ioerr) return HASH_ERR_IO;
#ifdef HASH_PROF
    hash_prof[0] += PROF_T() - pt0;
    pt0 = PROF_T();
#endif
    c->clocks_total += n;
    c->calls++;
    int err = c->check_err ? c->check_err : check_range(c, c->checked, n);
#ifdef HASH_PROF
    hash_prof[1] += PROF_T() - pt0;
#endif
    (void)pt0;
    if (err) {
        c->faults++;
        hash_chip_reset(c);
    }
    return err;
}

// start the next transport call's words
static uint32_t begin(hash_chip_t *c) {
    const uint32_t t0 = PROF_T();
    memset(c->due, 0, c->due_hw);
    c->due_hw = 0;
    c->nwin = 0;
#ifdef HASH_PROF
    hash_prof[3] += PROF_T() - t0;
#endif
    (void)t0;
    return 0;
}

int hash_chip_set_mode(hash_chip_t *c, int mode) {
    mode = mode ? 1 : 0;
    if (mode == c->mode) return HASH_OK;
    if (!c->io->uio_release) return HASH_ERR_ARG;
    uint32_t n = begin(c);
    c->words[n++] = cw(c, HC_EXT, HC_EXT_MODE | (unsigned)mode);
    for (int i = 0; i < 3; i++) c->words[n++] = 0;
    // To mode 1: the chip drives uio[1:0] from 2 clocks after the word, both low (not valid, not busy); the host's
    // idle words drive them low too, and it lets go once the clock has stopped. To mode 0: the chip lets go first.
    // (Its pins are not sampled in that call: once it lets go they float until the host drives them again.)
    c->mode = 0;
    const int err = play(c, n);
    if (err) return err;
    if (c->io->uio_release(c->io, mode)) return HASH_ERR_IO;
    c->mode = mode;
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
            c->words[n++] = cw(c, HC_EXT, HC_EXT_ABSORB_N | (part - 1));
            for (unsigned k = 0; k < part; k++) c->words[n++] = b[i + k];      // the bare byte: no valid, no marker
        }
        return n;
    }
    for (unsigned ln = 0; ln < cnt; ln += 8) {
        const unsigned part = cnt - ln < 8 ? cnt - ln : 8;
        int any = 0;
        for (unsigned k = 0; k < part; k++) any |= b[ln + k];
        if ((opts & HASH_SKIP) && part == 8 && !any) c->words[n++] = cw(c, HC_SKIP, 0);
        else for (unsigned k = 0; k < part; k++) c->words[n++] = cw(c, HC_ABSORB, b[ln + k]);
    }
    return n;
}

static uint32_t put_idle(hash_chip_t *c, uint32_t n, uint32_t cnt) {
    if (cnt > HASH_WORDS_MAX - n) cnt = n < HASH_WORDS_MAX ? HASH_WORDS_MAX - n : 0;
    memset(&c->words[n], 0, cnt * sizeof c->words[0]);
    return n + cnt;
}

// remember [d0, d0 + cnt) as a window that can carry bytes (merged with the last one when they touch)
static void add_window(hash_chip_t *c, uint32_t d0, uint32_t cnt, uint8_t what) {
    if (c->nwin > HASH_WIN_MAX || !cnt) return;
    if (c->nwin) {
        const uint32_t last = c->nwin - 1, end = (uint32_t)c->win_at[last] + c->win_len[last];
        if (d0 < end) { c->nwin = HASH_WIN_MAX + 1; return; }  // out of order: fall back to a full scan
        if (d0 == end && c->win_what[last] == what) { c->win_len[last] = (uint16_t)(c->win_len[last] + cnt); return; }
    }
    if (c->nwin == HASH_WIN_MAX) { c->nwin = HASH_WIN_MAX + 1; return; }
    c->win_at[c->nwin] = (uint16_t)d0;
    c->win_len[c->nwin] = (uint16_t)cnt;
    c->win_what[c->nwin] = what;
    c->nwin++;
}

// mark the clocks of `cnt` bytes asked for from word `at` on (one per clock): due at + delay
static void mark(hash_chip_t *c, uint32_t at, uint32_t cnt, uint8_t what) {
    const uint32_t d0 = at + HC_SQ_DELAY + (uint32_t)c->skew;
    if (d0 >= HASH_WORDS_MAX) return;
    if (cnt > HASH_WORDS_MAX - d0) cnt = HASH_WORDS_MAX - d0;
    memset(c->due + d0, what, cnt);
    if (d0 + cnt > c->due_hw) c->due_hw = d0 + cnt;
    add_window(c, d0, cnt, what);
}

// One transport call is one block, so the clock budget of the longest block must fit the buffers:
// CLEAR + 168 absorb words (+ 3 packet headers) + PERMUTE + 368 busy, or the last block with FINISH
// (167 bytes + 3 headers + FINISH + 35 steps + PERMUTE + 368 busy) followed by at least one squeezed block.
#define ABSORB_MAX (1 + HC_CLEAR_BUSY + 168 + 3 + 1 + 35 + 1 + HC_PERM_BUSY)
#define AUTO_PERIOD(rate) ((rate) + HC_AUTO_GAP + HC_PERM_BUSY)              /* block start to block start */
_Static_assert(ABSORB_MAX + AUTO_PERIOD(168) + 168 + HC_NTT_TAIL + HC_SQ_DELAY + 4 + 8 < HASH_WORDS_MAX,
               "block does not fit HASH_WORDS_MAX");

int hash_chip_job(hash_chip_t *c, hash_kind_t kind, const uint8_t *msg, uint32_t len, uint8_t *out, uint32_t outlen,
                  unsigned opts) {
    if (c->skew < 0 || c->skew > 4) return HASH_ERR_ARG;
    if ((unsigned)kind >= HASH_NKIND || (!msg && len) || !out) return HASH_ERR_ARG;
    const unsigned rate = KIND[kind].rate;
    const int cbd_eta = kind == HASH_PRF_CBD2 ? 2 : kind == HASH_PRF_CBD3 ? 3 : 0;
    const int ntt = kind == HASH_XOF_NTT;
    if (ntt && !c->mode) return HASH_ERR_ARG;                  // its output is marked by the valid pin
    if (cbd_eta || ntt) outlen = KIND[kind].fixed;
    else if (KIND[kind].fixed && outlen > KIND[kind].fixed) return HASH_ERR_ARG;   // SHA3 digests are fixed
    if (outlen == 0) return HASH_OK;
    const int packed = cbd_eta == 3 && c->mode;                // mode 1: two bytes per three, marked by valid
    const uint32_t delay = HC_SQ_DELAY + (uint32_t)c->skew;
    int err;

    // ---- absorb: CLEAR, then the message block by block; each block ends with PERMUTE + busy ----
    const uint32_t padded = (len / rate + 1) * rate;       // message + domain byte + zeros + 0x80, whole blocks
    uint32_t n = begin(c);
    c->words[n++] = cw(c, HC_CLEAR, 0);
    n = put_idle(c, n, HC_CLEAR_BUSY);
    if (opts & HASH_FINISH) {
        // the chip pads: the message's whole blocks, then what is left and FINISH (pads, permutes)
        const uint32_t full = len / rate * rate;
        for (uint32_t base = 0; base < full; base += rate) {
            n = put_bytes(c, n, msg + base, rate, opts);
            c->words[n++] = cw(c, HC_PERMUTE, 0);
            n = put_idle(c, n, HC_PERM_BUSY);
            if ((err = play(c, n))) return err;
            n = begin(c);
        }
        if (len > full) n = put_bytes(c, n, msg + full, len - full, opts);
        c->words[n++] = cw(c, HC_EXT, HC_EXT_FINISH | KIND[kind].fn);
        n = put_idle(c, n, hash_fin_steps(len - full, rate) + 1 + HC_PERM_BUSY);
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
            c->words[n++] = cw(c, HC_PERMUTE, 0);
            n = put_idle(c, n, HC_PERM_BUSY);
            if (base + rate < padded) {                         // next block needs a fresh buffer
                if ((err = play(c, n))) return err;
                n = begin(c);
            }
        }
    }

    // ---- squeeze: one transport call per block (or per burst word that covers several blocks) ----
    const uint32_t period = AUTO_PERIOD(rate);
    uint8_t pk[128] = {0};                                  // packed eta = 3 bytes (zeroed: GCC 14 -Werror)
    uint32_t got = 0, npk = 0, req = 0, idx3 = 0, calls = 0;
    for (;;) {
        if (req) {
            c->words[n++] = cw(c, HC_PERMUTE, 0);
            n = put_idle(c, n, HC_PERM_BUSY);
        }
        const uint32_t at = n;                              // the word that asks for this call's first byte
        uint32_t take;
        if (ntt && (opts & HASH_AUTO)) {
            // one word: the chip bursts and permutes until the polynomial is complete; busy says when
            c->words[n++] = cw(c, HC_BURST_CBD, HC_BURST_NTT | HC_BURST_AUTO | rate / 8);
            n = put_idle(c, n, period + 64);
            mark(c, at, n - at, 2);
            take = rate;
        } else if (ntt) {
            c->words[n++] = cw(c, HC_BURST_CBD, HC_BURST_NTT | rate / 8);
            n = put_idle(c, n, rate - 1 + HC_NTT_TAIL);
            mark(c, at, rate + HC_NTT_TAIL, 2);
            take = rate;
        } else {
            const uint32_t rem = (outlen - req + 7) / 8 * 8;
            take = rem < rate ? rem : rate;                 // bytes this block, whole lanes
            uint32_t nblk = 1, second = 0;
            if ((opts & HASH_BYTEWISE) && !cbd_eta) {
                for (uint32_t i = 0; i < take; i++) c->words[n++] = cw(c, HC_SQUEEZE, 0);
            } else if (cbd_eta) {
                unsigned data = take / 8 | (cbd_eta == 3 ? 0x80u : 0u);
                if ((opts & HASH_AUTO) && cbd_eta == 3 && req == 0) {      // all of PRF_3 from one word
                    data |= HC_BURST_AUTO;
                    second = 192 - rate;
                }
                c->words[n++] = cw(c, HC_BURST_CBD, data);
                n = put_idle(c, n, take - 1);
            } else {
                if ((opts & HASH_AUTO) && outlen - req > rate) {            // several whole blocks from one word
                    const uint32_t room = (HASH_WORDS_MAX - n - delay - 8) / period;
                    nblk = (outlen - req + rate - 1) / rate;
                    if (nblk > 8) nblk = 8;
                    if (nblk > room) nblk = room;
                    if (nblk < 1) nblk = 1;
                }
                c->words[n++] = cw(c, HC_BURST, take / 8 | (nblk - 1) << 5);
                n = put_idle(c, n, take - 1);
            }
            // which of these clocks carry a byte: all, or for packed eta = 3 the last two of every three
            for (uint32_t b = 0; b < nblk + (second ? 1 : 0); b++) {
                const uint32_t cnt = b && second ? second : take;
                if (b) n = put_idle(c, n, at + b * period + cnt - n);
                if (!packed) {                              // every clock of the block: one window
                    mark(c, at + b * period, cnt, 1);
                    idx3 += cnt;
                } else {                                    // packed eta = 3: two of every three clocks
                    for (uint32_t i = 0; i < cnt; i++, idx3++)
                        if (idx3 % 3) mark(c, at + b * period + i, 1, 1);
                }
            }
            take = nblk * take + second;
        }
        n = put_idle(c, n, delay);                          // the last byte's sample
        if ((err = play(c, n))) return err;
        for (;;) {
            const uint32_t pu0 = PROF_T();
            (void)pu0;
            // the windows that can carry bytes, or every clock if they weren't tracked
            const uint32_t nw = c->nwin <= HASH_WIN_MAX ? c->nwin : 1;
            for (uint32_t w = 0; w < nw; w++) {
                const uint32_t k0 = c->nwin <= HASH_WIN_MAX ? c->win_at[w] : 0;
                uint32_t k1 = c->nwin <= HASH_WIN_MAX ? k0 + c->win_len[w] : n;
                if (k1 > n) k1 = n;
                if (c->nwin <= HASH_WIN_MAX && c->win_what[w] == 1 && !packed) {   // a byte on every clock: copy
                    uint32_t m = k1 > k0 ? k1 - k0 : 0;
                    if (m > outlen - got) m = outlen - got;
                    memcpy(out + got, c->uo + k0, m);
                    got += m;
                    continue;
                }
                for (uint32_t k = k0; k < k1; k++) {
                    if (c->due[k] == 1) {
                        if (packed) { if (npk < sizeof pk) pk[npk++] = c->uo[k]; }
                        else if (got < outlen) out[got++] = c->uo[k];
                    } else if (c->due[k] == 2 && (c->aux[k] & TT_AUX_VALID)) {
                        if (got >= outlen) return HASH_ERR_NTT;
                        out[got++] = c->uo[k];
                    }
                }
            }
#ifdef HASH_PROF
            hash_prof[2] += PROF_T() - pu0;
#endif
            // the automatic SampleNTT is still going while busy is high: keep the clock running
            if (!(ntt && (opts & HASH_AUTO)) || !(c->aux[n - 1] & TT_AUX_BUSY)) break;
            if (++calls > 40) return HASH_ERR_NTT;
            n = put_idle(c, begin(c), 512);
            memset(c->due, 2, n);
            if (n > c->due_hw) c->due_hw = n;
            add_window(c, 0, n, 2);
            if ((err = play(c, n))) return err;
        }
        req += take;
        if (ntt) {
            if (got == outlen) break;
            if ((opts & HASH_AUTO) || ++calls > 16) return HASH_ERR_NTT;    // stopped short / never completes
        } else if (req >= outlen) break;
        n = begin(c);
    }
    if (packed) {                                           // {f1,f0} {f3,f2} -> {0,f0} {0,f1} {f3,f2}
        if (npk != sizeof pk) return HASH_ERR_VALID;
        for (unsigned j = 0; j < 64; j++) {
            out[3 * j] = pk[2 * j] & 15;
            out[3 * j + 1] = pk[2 * j] >> 4;
            out[3 * j + 2] = pk[2 * j + 1];
        }
    }
    return HASH_OK;
}
