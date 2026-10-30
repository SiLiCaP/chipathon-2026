// SPDX-License-Identifier: Apache-2.0
// Software chip: see hash_swchip.h. Behavioural twin of src/hash_io.v + keccak_ring.v (K = 8).
#include "hash_swchip.h"

#include <string.h>

#include "hash_chip.h"
#include "sha3_sw.h"

void hash_swchip_init(hash_swchip_t *s) { memset(s, 0, sizeof *s); }

static void violation(hash_swchip_t *s, const char *what) {
    s->violations++;
    s->last_violation = what;
}

static uint8_t cbd2(unsigned x) { return (uint8_t)(((x & 1) + ((x >> 1) & 1) - ((x >> 2) & 1) - ((x >> 3) & 1)) & 15); }
static uint8_t cbd3(unsigned x) {
    return (uint8_t)(((x & 1) + ((x >> 1) & 1) + ((x >> 2) & 1) - ((x >> 3) & 1) - ((x >> 4) & 1) - ((x >> 5) & 1)) & 15);
}

// The next state byte leaves the chip 2 clocks after the clock that asks for it; CBD bytes are converted on the way.
static void squeeze_byte(hash_swchip_t *s, uint32_t t, int cbd, int eta3) {
    if (s->ptr >= 200) {
        violation(s, "read past the state");
        return;
    }
    uint8_t raw = (uint8_t)(s->st[s->ptr / 8] >> (8 * (s->ptr % 8))), v = raw;
    s->ptr++;
    if (cbd) {
        if (!eta3) {
            v = (uint8_t)((cbd2(raw >> 4) << 4) | cbd2(raw & 15));
        } else {
            switch (s->p3) {
            case 0: v = cbd3(raw & 63); s->left2 = raw >> 6; break;
            case 1: v = cbd3((unsigned)((raw & 15) << 2) | s->left2); s->left4 = raw >> 4; break;
            default: v = (uint8_t)((cbd3(raw >> 2) << 4) | cbd3((unsigned)((raw & 3) << 4) | s->left4)); break;
            }
            s->p3 = s->p3 == 2 ? 0 : s->p3 + 1;
        }
    }
    s->out[(t + HC_SQ_DELAY) & 7] = v;
    s->out_clk[(t + HC_SQ_DELAY) & 7] = t + HC_SQ_DELAY;
}

static uint8_t step(hash_swchip_t *s, uint16_t w) {
    const uint32_t t = s->clk++;
    const uint8_t uo = (s->out_clk[t & 7] == t) ? s->out[t & 7] : 0;      // sampled before this edge
    const int valid = ((w >> 12) & 1) && ((w >> 8) & 15) == 15;           // uio[4] valid, uio[3:0] = 0xF marks a command
    const unsigned cmd = (unsigned)(((w >> 13) & 1) << 2 | ((w >> 14) & 3)), data = w & 0xFF;
    if (s->busy) {                                                          // PERMUTE / CLEAR running
        s->busy--;
        if (valid) violation(s, "command while the chip is busy");
        return uo;
    }
    if (s->burst_left) {                                                    // a burst streams one byte per clock
        s->burst_left--;
        if (valid) violation(s, "command during a burst");
        squeeze_byte(s, t, s->cbd_on, s->eta3);
        return uo;
    }
    if (!valid) return uo;
    switch (cmd) {
    case HC_ABSORB:
        if (s->ptr >= 200) violation(s, "absorb past the state");
        else s->st[s->ptr / 8] ^= (uint64_t)data << (8 * (s->ptr % 8));
        s->ptr++;
        break;
    case HC_SQUEEZE: squeeze_byte(s, t, 0, 0); break;
    case HC_PERMUTE:
        keccak_f1600(s->st);
        s->ptr = 0;
        s->busy = HC_PERM_BUSY;
        break;
    case HC_CLEAR:
        memset(s->st, 0, sizeof s->st);
        s->ptr = 0;
        s->p3 = 0;
        s->busy = HC_CLEAR_BUSY;
        break;
    case HC_SKIP:
        if (s->ptr % 8) violation(s, "skip off a lane boundary");
        s->ptr = (s->ptr + 8) / 8 * 8;       // to the next lane, which stays untouched
        break;
    case HC_BURST:
    case HC_BURST_CBD: {
        const unsigned n = data & 31;
        if (n < 1 || n > 21) {
            violation(s, "burst length outside 1..21");
            break;
        }
        s->cbd_on = cmd == HC_BURST_CBD;
        s->eta3 = (data >> 7) & 1;
        s->burst_left = 8 * n - 1;
        squeeze_byte(s, t, s->cbd_on, s->eta3);
        break;
    }
    default: break;                                                         // 7: reserved, no-op
    }
    return uo;
}

static int sw_run(tt_io_t *io, const uint16_t *words, uint32_t n, uint8_t *uo) {
    hash_swchip_t *s = io->ctx;
    for (uint32_t i = 0; i < n; i++) uo[i] = step(s, words[i]);
    return 0;
}

static int sw_reset(tt_io_t *io, uint32_t cycles) {
    hash_swchip_t *s = io->ctx;
    const uint32_t v = s->violations;
    const char *lv = s->last_violation;
    hash_swchip_init(s);
    s->clk = cycles;
    s->violations = v;                                                      // keep the record across resets
    s->last_violation = lv;
    return 0;
}

tt_io_t hash_swchip_io(hash_swchip_t *s) {
    return (tt_io_t){.run = sw_run, .reset = sw_reset, .name = "software chip", .ctx = s};
}
