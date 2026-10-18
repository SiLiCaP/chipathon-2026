// SPDX-License-Identifier: Apache-2.0
// Host harness for the NTT chip. See ntt_chip.h; every timing number comes from timing.h.
#include "ntt_chip.h"

#include <string.h>

#include "oplog.h"
#include "timing.h"

static const uint8_t OFFS_F[] = TT_OFFS_F, OFFS_I[] = TT_OFFS_I, OFFS_B[] = TT_OFFS_B, OFFS_S[] = TT_OFFS_S;

static const struct {
    uint8_t period, delay, n_in;
    const uint8_t *offs;
} OPS[4] = {
    [OP_FWD] = {TT_P_F, TT_D_F, TT_N_OFFS_F, OFFS_F},
    [OP_INV] = {TT_P_I, TT_D_I, TT_N_OFFS_I, OFFS_I},
    [OP_BM] = {TT_P_B, TT_D_B, TT_N_OFFS_B, OFFS_B},
    [OP_SC] = {TT_P_S, TT_D_S, TT_N_OFFS_S, OFFS_S},
};

void ntt_chip_init(ntt_chip_t *c, tt_io_t *io) {
    memset(c, 0, sizeof *c);
    c->io = io;
    ntt_ref_init();
}

int ntt_chip_reset(ntt_chip_t *c) {
    return c->io->reset(c->io, 10) ? NTT_ERR_IO : NTT_OK;
}

const char *ntt_err_str(int err) {
    switch (err) {
    case NTT_OK: return "ok";
    case NTT_ERR_RANGE: return "result >= 3329 (hardware fault)";
    case NTT_ERR_IO: return "transport error";
    default: return "unknown error";
    }
}

// Send n_units units of op (in: n_units x n_in coefficients in send order), wait until all their results
// are back and unpack them into pairs (out: 2 coefficients per result pair, one pair per unit).
// Every batch is timed (oplog.h): prep = building the words, run = the transport, post = unpacking.
static int run_units(ntt_chip_t *c, int op, int batch, const uint16_t *in, int n_units, int sof, uint16_t *out) {
    const uint32_t t0 = plat_cycles();
    const int P = OPS[op].period, D = OPS[op].delay, n_in = OPS[op].n_in;
    const uint8_t *offs = OPS[op].offs;
    const uint32_t n = (uint32_t)(n_units * P + D + 3 + c->skew);   // exact: host_timing.md job lengths
    if (n > NTT_WORDS_MAX) return NTT_ERR_IO;
    memset(c->words, 0, n * sizeof c->words[0]);         // idle clocks: valid low, all pins 0
    for (int u = 0; u < n_units; u++)
        for (int i = 0; i < n_in; i++)
            c->words[u * P + offs[i]] = TT_WORD(in[u * n_in + i], 1, sof && u == 0 && i == 0, op);
    const uint32_t t1 = plat_cycles();
    c->io->conv_cyc = 0;
    if (c->io->run(c->io, c->words, n, c->uo)) return NTT_ERR_IO;
    const uint32_t t2 = plat_cycles();
    c->clocks += n;
    c->clocks_total += n;
    int bad = 0;
    for (int k = 0; k < n_units; k++) {
        const uint8_t *b = &c->uo[k * P + D + c->skew];
        uint16_t r0, r1;
        unpack_pair(b, &r0, &r1);
        bad |= (r0 >= NTT_Q) | (r1 >= NTT_Q);
        out[2 * k] = r0;
        out[2 * k + 1] = r1;
    }
    oplog_batch(op, batch, n_units, n, t1 - t0, t2 - t1, c->io->conv_cyc, plat_cycles() - t2, c->skew);
    if (bad) {
        c->faults++;
        ntt_chip_reset(c);                               // abort the job; the next call starts with sof
        return NTT_ERR_RANGE;
    }
    return NTT_OK;
}

// One NTT job: 7 layers, layer by layer, sof on the first coefficient only. The chip stores nothing,
// so each layer's results come back before the next layer is sent (valid low in between).
static int ntt_job(ntt_chip_t *c, const uint16_t in[NTT_N], uint16_t out[NTT_N], int inverse) {
    const int op = inverse ? OP_INV : OP_FWD;
    uint16_t f[NTT_N], units[NTT_N], res[NTT_N];
    memcpy(f, in, sizeof f);
    c->clocks = 0;
    c->calls++;
    for (int layer = 0; layer < 7; layer++) {
        const int len = inverse ? (2 << layer) : (128 >> layer);
        int u = 0;
        for (int start = 0; start < NTT_N; start += 2 * len)
            for (int j = start; j < start + len; j++, u++) {
                units[2 * u] = f[j + len];               // b first, then a
                units[2 * u + 1] = f[j];
            }
        int err = run_units(c, op, layer, units, 128, layer == 0, res);
        if (err) return err;
        u = 0;
        for (int start = 0; start < NTT_N; start += 2 * len)
            for (int j = start; j < start + len; j++, u++) {
                f[j] = res[2 * u];
                f[j + len] = res[2 * u + 1];
            }
        if (c->on_layer) c->on_layer(c->on_layer_arg, op, layer, f);
    }
    memcpy(out, f, sizeof f);
    return NTT_OK;
}

static int logged(ntt_chip_t *c, int op, uint32_t t0, int err) {
    if (!err) oplog_call(op, c->clocks, plat_cycles() - t0);
    return err;
}

int ntt_chip_forward(ntt_chip_t *c, const uint16_t in[NTT_N], uint16_t out[NTT_N]) {
    const uint32_t t0 = plat_cycles();
    return logged(c, OP_FWD, t0, ntt_job(c, in, out, 0));
}

int ntt_chip_inverse(ntt_chip_t *c, const uint16_t in[NTT_N], uint16_t out[NTT_N]) {
    const uint32_t t0 = plat_cycles();
    return logged(c, OP_INV, t0, ntt_job(c, in, out, 1));
}

int ntt_chip_basemul(ntt_chip_t *c, const uint16_t f[NTT_N], const uint16_t g[NTT_N], uint16_t out[NTT_N]) {
    const uint32_t t0 = plat_cycles();
    uint16_t units[4 * 128];
    for (int i = 0; i < 128; i++) {                      // g1, f1, g0, f0 per piece
        units[4 * i] = g[2 * i + 1];
        units[4 * i + 1] = f[2 * i + 1];
        units[4 * i + 2] = g[2 * i];
        units[4 * i + 3] = f[2 * i];
    }
    c->clocks = 0;
    c->calls++;
    return logged(c, OP_BM, t0, run_units(c, OP_BM, 0, units, 128, 1, out));
}

int ntt_chip_scale(ntt_chip_t *c, const uint16_t in[NTT_N], uint16_t out[NTT_N]) {
    const uint32_t t0 = plat_cycles();
    uint16_t units[NTT_N];                               // pairs c, c' on consecutive clocks
    memcpy(units, in, sizeof units);
    c->clocks = 0;
    c->calls++;
    return logged(c, OP_SC, t0, run_units(c, OP_SC, 0, units, NTT_N / 2, 1, out));
}
