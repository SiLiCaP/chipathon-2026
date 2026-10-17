// SPDX-License-Identifier: Apache-2.0
// Software chip: see swchip.h.
#include "swchip.h"

#include <string.h>

#include "timing.h"

static const uint8_t PERIOD[4] = {TT_P_F, TT_P_I, TT_P_B, TT_P_S};
static const uint8_t DELAY[4] = {TT_D_F, TT_D_I, TT_D_B, TT_D_S};
static const uint8_t N_IN[4] = {2, 2, 4, 2};                // inputs per result pair (scale pairs two)
static const uint8_t OFFS_F[] = TT_OFFS_F, OFFS_I[] = TT_OFFS_I, OFFS_B[] = TT_OFFS_B, OFFS_S[] = TT_OFFS_S;
static const uint32_t UNITS[4] = {896, 896, 128, 128};      // result pairs per job

void swchip_init(swchip_t *s) { memset(s, 0, sizeof *s); }

static void violation(swchip_t *s, const char *what) {
    s->violations++;
    s->last_violation = what;
}

// clock offset of input i of a result pair, relative to the pair's first input
static int expected_offset(int op, int i) {
    switch (op) {
    case OP_FWD: return OFFS_F[i];
    case OP_INV: return OFFS_I[i];
    case OP_BM: return OFFS_B[i];
    default: return OFFS_S[0] + i * TT_P_S;                // scale: c at 0 of two consecutive units
    }
}

static void emit(swchip_t *s, uint32_t at, uint16_t r0, uint16_t r1) {
    uint8_t b[3];
    pack_pair(r0, r1, b);
    for (int i = 0; i < 3; i++) {
        s->out[(at + i) & 63] = b[i];
        s->out_clk[(at + i) & 63] = at + i;
    }
}

static void finish_unit(swchip_t *s) {
    const uint32_t n = s->units_done++;
    uint16_t r0, r1;
    if (s->op == OP_FWD || s->op == OP_INV) {
        // the chip's own butterfly counter picks the layer and the twiddle
        const int layer = n / 128, i = n % 128;
        uint16_t b = s->in[0], a = s->in[1];
        if (s->op == OP_FWD) {
            const int len = 128 >> layer, k = (1 << layer) + i / len;
            uint16_t t = chip_mul(b, ntt_zeta_s[k]);
            r0 = (uint16_t)((a + t) % NTT_Q);
            r1 = (uint16_t)((a + NTT_Q - t) % NTT_Q);
        } else {
            const int len = 2 << layer, k = (256 >> (layer + 1)) - 1 - i / len;
            r0 = (uint16_t)((a + b) % NTT_Q);
            r1 = chip_mul((uint16_t)((b + NTT_Q - a) % NTT_Q), ntt_zeta_s[k]);
        }
    } else if (s->op == OP_BM) {
        uint16_t g1 = s->in[0], f1 = s->in[1], g0 = s->in[2], f0 = s->in[3];
        uint16_t m2 = chip_mul(chip_mul(f1, g1), ntt_gamma_s[n]);
        r0 = (uint16_t)((chip_mul(f0, g0) + m2) % NTT_Q);
        r1 = (uint16_t)((chip_mul(f0, g1) + chip_mul(f1, g0)) % NTT_Q);
    } else {
        r0 = chip_mul(s->in[0], NTT_SCALE_C);
        r1 = chip_mul(s->in[1], NTT_SCALE_C);
    }
    emit(s, s->unit_start + DELAY[s->op], r0, r1);
    s->n_got = 0;
    if (s->units_done == UNITS[s->op]) s->active = 0;       // a job ends by itself after its last result
}

static uint8_t step(swchip_t *s, uint16_t w) {
    const uint32_t t = s->clk++;
    const uint8_t uo = (s->out_clk[t & 63] == t) ? s->out[t & 63] : 0;   // sampled before this edge
    const uint16_t coeff = w & 0xFFF;
    const int valid = (w >> 12) & 1, sof = (w >> 13) & 1, op = (w >> 14) & 3;
    if (!valid) return uo;
    if (coeff >= NTT_Q) violation(s, "coefficient >= 3329");
    if (sof) {                                               // starts a job, cancelling any running one
        s->active = 1;
        s->op = op;
        s->units_done = 0;
        s->n_got = 0;
    } else if (!s->active) {
        violation(s, "valid input outside a job");
        return uo;
    }
    if (s->n_got == 0) {
        if (s->units_done && t - s->unit_start < (uint32_t)PERIOD[s->op] * (s->op == OP_SC ? 2 : 1) && !sof)
            violation(s, "unit closer than the period");
        s->unit_start = t;
    } else if ((int)(t - s->unit_start) != expected_offset(s->op, s->n_got)) {
        violation(s, "input at the wrong offset");
    }
    s->in[s->n_got++] = coeff % NTT_Q;
    if (s->n_got == N_IN[s->op]) finish_unit(s);
    return uo;
}

static int sw_run(tt_io_t *io, const uint16_t *words, uint32_t n, uint8_t *uo) {
    swchip_t *s = io->ctx;
    for (uint32_t i = 0; i < n; i++) uo[i] = step(s, words[i]);
    return 0;
}

static int sw_reset(tt_io_t *io, uint32_t cycles) {
    swchip_t *s = io->ctx;
    uint32_t v = s->violations;
    const char *lv = s->last_violation;
    swchip_init(s);
    s->clk = cycles;
    s->violations = v;                                       // keep the record across resets
    s->last_violation = lv;
    return 0;
}

tt_io_t swchip_io(swchip_t *s) {
    ntt_ref_init();
    return (tt_io_t){.run = sw_run, .reset = sw_reset, .name = "software chip", .ctx = s};
}
