// SPDX-License-Identifier: Apache-2.0
// Software chip: see hash_swchip.h. Behavioural twin of src/hash_io.v + sample_ntt.v + keccak_ring.v (K = 8).
//
// Time: step(t) takes pin word t and returns what the pins show while that word is on them. The RTL sees word t
// one clock later and its registers show the effect one clock after that, so everything a word causes is
// scheduled for clock t + 2 (HC_SQ_DELAY): a squeezed byte, and the valid / busy pins.
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

static void emit(hash_swchip_t *s, uint32_t at, uint8_t v, int from_ntt) {
    s->out[at & 7] = v;
    s->out_clk[at & 7] = at;
    s->out_ntt[at & 7] = (uint8_t)from_ntt;
}

// SampleNTT (sample_ntt.v): three bytes give two candidates; each pair of accepted coefficients leaves as three
// bytes on the three clocks after the byte that completed it.
static void sample_byte(hash_swchip_t *s, uint32_t t, uint8_t b) {
    if (s->s_cnt >= 256) return;
    if (s->s_ph == 0) { s->s_c0 = b; s->s_ph = 1; return; }
    if (s->s_ph == 1) { s->s_c1 = b; s->s_ph = 2; return; }
    s->s_ph = 0;
    const unsigned d1 = s->s_c0 | (unsigned)(s->s_c1 & 15) << 8, d2 = (unsigned)(s->s_c1 >> 4) | (unsigned)b << 4;
    const int a1 = d1 < 3329, a2 = d2 < 3329;
    if (!a1 && !a2) return;
    const unsigned first = a1 ? d1 : d2;
    unsigned x, y;
    if (s->s_na) {                                  // the waiting coefficient gets its pair
        x = s->s_acc; y = first;
        if (a1 && a2 && s->s_cnt != 255) { s->s_acc = (uint16_t)d2; s->s_cnt += 2; }
        else { s->s_na = 0; s->s_cnt += 1; }
    } else if (a1 && a2) {
        x = d1; y = d2;
        s->s_cnt += 2;
    } else {
        s->s_acc = (uint16_t)first;
        s->s_na = 1;
        s->s_cnt += 1;
        return;
    }
    emit(s, t + HC_SQ_DELAY + 1, (uint8_t)x, 1);
    emit(s, t + HC_SQ_DELAY + 2, (uint8_t)((x >> 8) | (y & 15) << 4), 1);
    emit(s, t + HC_SQ_DELAY + 3, (uint8_t)(y >> 4), 1);
}

// The next state byte leaves the chip 2 clocks after the clock that asks for it; CBD bytes are converted on the
// way, SampleNTT bytes go to the sampler instead.
static void squeeze_byte(hash_swchip_t *s, uint32_t t, int cbd, int eta3, int ntt) {
    if (s->ptr >= 200) {
        violation(s, "read past the state");
        return;
    }
    uint8_t raw = (uint8_t)(s->st[s->ptr / 8] >> (8 * (s->ptr % 8))), v = raw;
    s->ptr++;
    if (ntt) {
        sample_byte(s, t, raw);
        return;
    }
    if (cbd) {
        if (!eta3) {
            v = (uint8_t)((cbd2(raw >> 4) << 4) | cbd2(raw & 15));
        } else {
            const unsigned p3 = s->p3;
            s->p3 = p3 == 2 ? 0 : p3 + 1;
            switch (p3) {
            case 0:
                v = cbd3(raw & 63);
                s->f0 = v;
                s->left2 = raw >> 6;
                if (s->mode) return;                // packed: f0 leaves with the next byte
                break;
            case 1:
                v = cbd3((unsigned)((raw & 15) << 2) | s->left2);
                s->left4 = raw >> 4;
                if (s->mode) v = (uint8_t)(v << 4 | s->f0);
                break;
            default: v = (uint8_t)((cbd3(raw >> 2) << 4) | cbd3((unsigned)((raw & 3) << 4) | s->left4)); break;
            }
        }
    }
    emit(s, t + HC_SQ_DELAY, v, 0);
}

static void absorb_byte(hash_swchip_t *s, unsigned data) {
    if (s->ptr >= 200) violation(s, "absorb past the state");
    else s->st[s->ptr / 8] ^= (uint64_t)data << (8 * (s->ptr % 8));
    s->ptr++;
}

static void permute(hash_swchip_t *s) {
    keccak_f1600(s->st);
    s->ptr = 0;
    s->busy = HC_PERM_BUSY;
}

// One clock of FINISH (hash_io.v): the domain byte, the rest of its lane, a SKIP per whole lane, the last lane
// with 0x80 at rate - 1, then the PERMUTE.
static void finish_step(hash_swchip_t *s) {
    const unsigned last = s->fin_alg == 1 ? 8 : s->fin_alg == 2 ? 20 : 16;      // last lane of the rate
    const int at = s->ptr >= last * 8 + 7;
    if (s->fin_end) {
        permute(s);
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

// what one pin word does to the chip
static void act(hash_swchip_t *s, uint32_t t, uint16_t w, int framing) {
    const int valid = ((w >> 12) & 1) && (framing || ((w >> 8) & 15) == 15);    // uio[4]; mode 0: uio[3:0] = 0xF too
    const unsigned cmd = (unsigned)(((w >> 13) & 1) << 2 | ((w >> 14) & 3)), data = w & 0xFF;
    if (s->busy) {                                                          // PERMUTE / CLEAR running
        s->busy--;
        if (valid) violation(s, "command while the chip is busy");
        return;
    }
    if (s->ap_wait) {                                                       // the core is free: the next block starts
        if (valid) violation(s, "command during a burst");
        s->ap_wait = 0;
        s->burst_left = 8 * s->ap_n - 1;
        squeeze_byte(s, t, s->cbd_on, s->eta3, s->ntt_on);
        return;
    }
    if (s->burst_left) {                                                    // a burst streams one byte per clock
        s->burst_left--;
        if (valid) violation(s, "command during a burst");
        squeeze_byte(s, t, s->cbd_on, s->eta3, s->ntt_on);
        if (!s->burst_left && (s->ap_inf || s->ap_cnt)) s->ap_gap = HC_AUTO_GAP;
        return;
    }
    if (s->ap_gap) {                                                        // 3 clocks, then: another block?
        if (valid) violation(s, "command during a burst");
        if (--s->ap_gap == 0) {
            if (s->ap_inf ? s->s_cnt < 256 : s->ap_cnt != 0) {
                permute(s);
                s->ap_wait = 1;
                if (!s->ap_inf) s->ap_cnt--;
            } else s->ap_inf = 0;
        }
        return;
    }
    if (s->raw_left) {                                                      // ABSORB_N: the data byte, whatever uio says
        s->raw_left--;
        absorb_byte(s, data);
        return;
    }
    if (s->fin_on) {                                                        // FINISH pads, one step per clock
        if (valid) violation(s, "command during FINISH");
        finish_step(s);
        return;
    }
    if (!valid) return;
    switch (cmd) {
    case HC_ABSORB: absorb_byte(s, data); break;
    case HC_SQUEEZE: squeeze_byte(s, t, 0, 0, 0); break;
    case HC_PERMUTE: permute(s); break;
    case HC_CLEAR:
        memset(s->st, 0, sizeof s->st);
        s->ptr = 0;
        s->p3 = 0;
        s->s_ph = s->s_cnt = 0;
        s->s_na = 0;
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
        s->cbd_on = cmd == HC_BURST_CBD && !(data & HC_BURST_NTT);
        s->ntt_on = cmd == HC_BURST_CBD && (data & HC_BURST_NTT);
        s->eta3 = (data >> 7) & 1;
        // what follows by itself: BURST data[7:5] blocks; CBD eta 3 the rest of the 24 lanes; SampleNTT until done
        s->ap_inf = s->ntt_on && (data & HC_BURST_AUTO);
        s->ap_cnt = cmd == HC_BURST ? data >> 5 : (s->cbd_on && s->eta3 && (data & HC_BURST_AUTO)) ? 1 : 0;
        s->ap_n = s->cbd_on ? 24 - n : n;
        if (s->ntt_on && !s->mode) violation(s, "SampleNTT burst in framing mode 0 (no valid pin)");
        s->burst_left = 8 * n - 1;
        squeeze_byte(s, t, s->cbd_on, s->eta3, s->ntt_on);
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
        case HC_EXT_MODE: s->mode = data & 1; break;
        default: break;                                                     // 00: reserved, no-op
        }
        break;
    default: break;
    }
}

static void step(hash_swchip_t *s, uint16_t w, uint8_t *uo, uint8_t *aux) {
    const uint32_t t = s->clk++;
    // the pins while this word is on them (sampled before the edge that takes it)
    const int byte = s->out_clk[t & 7] == t;
    const uint8_t fl = s->flag_clk[t & 7] == t ? s->flag[t & 7] : 0;
    *uo = byte ? s->out[t & 7] : 0;
    if (aux) {
        const int busy = (fl & 2) || (byte && s->out_ntt[t & 7]);
        *aux = (fl & 1) ? (uint8_t)((byte ? TT_AUX_VALID : 0) | (busy ? TT_AUX_BUSY : 0)) : 0;
    }
    const int framing = s->mode_prev;
    s->mode_prev = s->mode;
    act(s, t, w, framing);
    // the pins two clocks on: the chip drives uio[1:0] in mode 1; busy while it takes no command
    const int busy = s->busy || s->burst_left || s->raw_left || s->fin_on || s->ap_gap || s->ap_wait;
    s->flag[(t + HC_SQ_DELAY) & 7] = (uint8_t)((s->mode ? 1 : 0) | (busy ? 2 : 0));
    s->flag_clk[(t + HC_SQ_DELAY) & 7] = t + HC_SQ_DELAY;
}

static int sw_run(tt_io_t *io, const uint16_t *words, uint32_t n, uint8_t *uo, uint8_t *aux) {
    hash_swchip_t *s = io->ctx;
    for (uint32_t i = 0; i < n; i++) step(s, words[i], &uo[i], aux ? &aux[i] : NULL);
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

static int sw_release(tt_io_t *io, int release) { (void)io; (void)release; return 0; }   // no pins to fight over

tt_io_t hash_swchip_io(hash_swchip_t *s) {
    return (tt_io_t){.run = sw_run, .reset = sw_reset, .uio_release = sw_release, .name = "software chip", .ctx = s};
}
