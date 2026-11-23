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

static void absorb_byte(hash_swchip_t *s, unsigned data) {
    if (s->ptr >= 200) violation(s, "absorb past the state");
    else s->st[s->ptr / 8] ^= (uint64_t)data << (8 * (s->ptr % 8));
    s->ptr++;
}

// One clock of FINISH (hash_io.v): the domain byte, the rest of its lane, a SKIP per whole lane, the last lane
// with 0x80 at rate - 1, then the PERMUTE.
static void finish_step(hash_swchip_t *s) {
    const unsigned last = s->fin_alg == 1 ? 8 : s->fin_alg == 2 ? 20 : 16;      // last lane of the rate
    const int at = s->ptr >= last * 8 + 7;
    if (s->fin_end) {
        keccak_f1600(s->st);
        s->ptr = 0;
        s->busy = HC_PERM_BUSY;
        s->fin_on = 0;
        return;
    }
    if (s->fin_dom && s->ptr % 8 == 0 && s->ptr / 8 < last) {
        s->ptr += 8;
    } else {
        absorb_byte(s, (at ? 0x80u : 0u) | (s->fin_dom ? 0u : (s->fin_alg & 2) ? 0x1Fu : 0x06u));
        if (at) s->fin_end = 1;
    }
    s->fin_dom = 1;
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
    if (s->raw_left) {                                                      // ABSORB_N: the data byte, whatever uio says
        s->raw_left--;
        absorb_byte(s, data);
        return uo;
    }
    if (s->fin_on) {                                                        // FINISH pads, one step per clock
        if (valid) violation(s, "command during FINISH");
        finish_step(s);
        return uo;
    }
    if (!valid) return uo;
    switch (cmd) {
    case HC_ABSORB: absorb_byte(s, data); break;
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
        if (cmd == HC_BURST_CBD && (data & HC_BURST_NTT)) {
            violation(s, "SampleNTT burst: not modelled (it needs the valid pin, which the transports do not sample)");
            break;
        }
        if (cmd == HC_BURST ? (data >> 5) != 0 : (data & HC_BURST_AUTO) != 0) {
            violation(s, "burst with blocks that follow by themselves: not modelled");
            break;
        }
        s->cbd_on = cmd == HC_BURST_CBD;
        s->eta3 = (data >> 7) & 1;
        s->burst_left = 8 * n - 1;
        squeeze_byte(s, t, s->cbd_on, s->eta3);
        break;
    }
    case HC_EXT:
        switch (data & 0xC0) {
        case HC_EXT_ABSORB_N: s->raw_left = (data & 63) + 1; break;
        case HC_EXT_FINISH:
            s->fin_alg = data & 3;
            if (s->ptr >= (s->fin_alg == 1 ? 72u : s->fin_alg == 2 ? 168u : 136u))
                violation(s, "FINISH with a full block (PERMUTE first)");
            s->fin_on = 1;
            s->fin_dom = s->fin_end = 0;
            break;
        case HC_EXT_MODE: violation(s, "MODE: framing mode 1 is not modelled (uio[0] as the valid output)"); break;
        default: break;                                                     // 00: reserved, no-op
        }
        break;
    default: break;
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
