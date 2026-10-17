// SPDX-License-Identifier: Apache-2.0
// Reference model in C, the twin of test/ntt_ref.py. See ntt_ref.h.
#include "ntt_ref.h"

#include <string.h>

uint16_t ntt_zeta[128], ntt_gamma[128], ntt_zeta_s[128], ntt_gamma_s[128];

static uint16_t mulq(uint32_t a, uint32_t b) { return (uint16_t)(a * b % NTT_Q); }

static uint16_t powq(uint32_t b, uint32_t e) {
    uint32_t r = 1;
    for (; e; e >>= 1, b = b * b % NTT_Q)
        if (e & 1) r = r * b % NTT_Q;
    return (uint16_t)r;
}

static int bitrev7(int k) {
    int r = 0;
    for (int i = 0; i < 7; i++) r |= ((k >> i) & 1) << (6 - i);
    return r;
}

void ntt_ref_init(void) {
    for (int k = 0; k < 128; k++) {
        ntt_zeta[k] = powq(17, bitrev7(k));
        ntt_gamma[k] = powq(17, 2 * bitrev7(k) + 1);
        ntt_zeta_s[k] = mulq(ntt_zeta[k], NTT_KM_INV);
        ntt_gamma_s[k] = mulq(ntt_gamma[k], NTT_KM_INV);
    }
}

// ---------------- plain model ----------------
void ref_ntt_layer(uint16_t f[NTT_N], int layer) {
    int len = 128 >> layer, k = 1 << layer;
    for (int start = 0; start < NTT_N; start += 2 * len) {
        uint32_t z = ntt_zeta[k++];
        for (int j = start; j < start + len; j++) {
            uint32_t t = z * f[j + len] % NTT_Q;
            f[j + len] = (uint16_t)((f[j] + NTT_Q - t) % NTT_Q);
            f[j] = (uint16_t)((f[j] + t) % NTT_Q);
        }
    }
}

void ref_intt_layer(uint16_t f[NTT_N], int layer) {
    int len = 2 << layer, k = (256 >> (layer + 1)) - 1;
    for (int start = 0; start < NTT_N; start += 2 * len) {
        uint32_t z = ntt_zeta[k--];
        for (int j = start; j < start + len; j++) {
            uint16_t t = f[j];
            f[j] = (uint16_t)((t + f[j + len]) % NTT_Q);
            f[j + len] = (uint16_t)(z * ((f[j + len] + NTT_Q - t) % NTT_Q) % NTT_Q);
        }
    }
}

void ref_ntt(uint16_t f[NTT_N]) { for (int l = 0; l < 7; l++) ref_ntt_layer(f, l); }
void ref_intt(uint16_t f[NTT_N]) { for (int l = 0; l < 7; l++) ref_intt_layer(f, l); }

void ref_basemul(const uint16_t f[NTT_N], const uint16_t g[NTT_N], uint16_t h[NTT_N]) {
    for (int i = 0; i < 128; i++) {
        uint32_t f0 = f[2 * i], f1 = f[2 * i + 1], g0 = g[2 * i], g1 = g[2 * i + 1];
        h[2 * i] = (uint16_t)((f0 * g0 + f1 * g1 % NTT_Q * ntt_gamma[i]) % NTT_Q);
        h[2 * i + 1] = (uint16_t)((f0 * g1 + f1 * g0) % NTT_Q);
    }
}

void ref_scale(uint16_t f[NTT_N]) { for (int i = 0; i < NTT_N; i++) f[i] = mulq(f[i], NTT_SCALE); }

// Negacyclic product mod X^256 + 1. The wrapped and unwrapped partial sums are kept apart; each stays
// below 256 * 3328^2 < 2^32, so plain 32-bit accumulation is exact (fast on the M33).
void ref_schoolbook(const uint16_t f[NTT_N], const uint16_t g[NTT_N], uint16_t h[NTT_N]) {
    for (int k = 0; k < NTT_N; k++) {
        uint32_t pos = 0, neg = 0;
        for (int i = 0; i <= k; i++) pos += (uint32_t)f[i] * g[k - i];
        for (int i = k + 1; i < NTT_N; i++) neg += (uint32_t)f[i] * g[k + NTT_N - i];
        h[k] = (uint16_t)(((pos % NTT_Q) + NTT_Q - (neg % NTT_Q)) % NTT_Q);
    }
}

// ---------------- chip level ----------------
static int32_t kred(int32_t x) { return 13 * (x & 255) - (x >> 8); }   // >> is arithmetic (GCC, Clang)

uint16_t chip_mul(uint16_t a, uint16_t b) {
    int32_t c = (int32_t)a * b;
    int32_t t = 169 * (c & 255) - 13 * ((c >> 8) & 255) + (c >> 16);   // kred(kred(c)), merged
    int32_t u = kred(t);                                               // lands in -169 .. 3328
    return (uint16_t)(u < 0 ? u + NTT_Q : u);
}

void chip_ntt_layer(uint16_t f[NTT_N], int layer, int inverse) {
    int len, k, step;
    if (inverse) { len = 2 << layer; k = (256 >> (layer + 1)) - 1; step = -1; }
    else         { len = 128 >> layer; k = 1 << layer; step = 1; }
    for (int start = 0; start < NTT_N; start += 2 * len, k += step) {
        uint16_t z = ntt_zeta_s[k];
        for (int j = start; j < start + len; j++) {
            uint16_t b = f[j + len], a = f[j];
            if (inverse) {
                f[j] = (uint16_t)((a + b) % NTT_Q);
                f[j + len] = chip_mul((uint16_t)((b + NTT_Q - a) % NTT_Q), z);
            } else {
                uint16_t t = chip_mul(b, z);
                f[j] = (uint16_t)((a + t) % NTT_Q);
                f[j + len] = (uint16_t)((a + NTT_Q - t) % NTT_Q);
            }
        }
    }
}

void chip_basemul(const uint16_t f[NTT_N], const uint16_t g[NTT_N], uint16_t h[NTT_N]) {
    for (int i = 0; i < 128; i++) {
        uint16_t f0 = f[2 * i], f1 = f[2 * i + 1], g0 = g[2 * i], g1 = g[2 * i + 1];
        uint16_t m2 = chip_mul(chip_mul(f1, g1), ntt_gamma_s[i]);
        h[2 * i] = (uint16_t)((chip_mul(f0, g0) + m2) % NTT_Q);
        h[2 * i + 1] = (uint16_t)((chip_mul(f0, g1) + chip_mul(f1, g0)) % NTT_Q);
    }
}

void chip_scale(uint16_t f[NTT_N]) { for (int i = 0; i < NTT_N; i++) f[i] = chip_mul(f[i], NTT_SCALE_C); }

void pack_pair(uint16_t r0, uint16_t r1, uint8_t b[3]) {
    uint32_t v = r0 | ((uint32_t)r1 << 12);
    b[0] = v & 0xFF; b[1] = (v >> 8) & 0xFF; b[2] = (v >> 16) & 0xFF;
}

void unpack_pair(const uint8_t b[3], uint16_t *r0, uint16_t *r1) {
    *r0 = (uint16_t)(b[0] | ((b[1] & 0x0F) << 8));
    *r1 = (uint16_t)((b[1] >> 4) | (b[2] << 4));
}

// ---------------- known answers ----------------
static uint32_t lcg = 1;
static uint16_t rnd(void) { lcg = lcg * 1664525u + 1013904223u; return (uint16_t)((lcg >> 8) % NTT_Q); }

#define CHECK(cond, text) do { n++; if (!(cond)) { if (what) *what = text; return n; } } while (0)

int ntt_ref_selfcheck(const char **what) {
    int n = 0;
    static uint16_t f[NTT_N], g[NTT_N], h[NTT_N], s[NTT_N];
    ntt_ref_init();
    CHECK(ntt_zeta[0] == 1 && ntt_zeta[1] == 1729 && ntt_zeta[2] == 2580 && ntt_zeta[3] == 3289 &&
          ntt_zeta[64] == 17 && ntt_zeta[127] == 2154, "ROM[k] = 17^BitRev7(k)");
    CHECK(ntt_gamma[0] == 17 && ntt_gamma[1] == 3312 && ntt_gamma[2] == 2761 && ntt_gamma[3] == 568 &&
          ntt_gamma[126] == 2154 && ntt_gamma[127] == 1175, "gamma_i");
    CHECK(128 * NTT_SCALE % NTT_Q == 1, "128 * 3303 = 1");
    for (int i = 0; i < NTT_N; i++) f[i] = (uint16_t)i;
    ref_ntt(f);
    static const uint16_t lo[8] = {2429, 2845, 425, 795, 1865, 1356, 624, 31};
    static const uint16_t hi[8] = {2883, 149, 2594, 3105, 2502, 2134, 2717, 2303};
    CHECK(!memcmp(f, lo, sizeof lo) && !memcmp(f + 248, hi, sizeof hi), "forward NTT of the ramp");
    ref_intt(f);
    CHECK(f[0] == 0 && f[1] == 128 && f[2] == 256 && f[3] == 384, "inverse NTT of it, before the scale");
    ref_scale(f);
    int ramp = 1;
    for (int i = 0; i < NTT_N; i++) ramp &= f[i] == i;
    CHECK(ramp, "scale returns the ramp");
    uint8_t b[3];
    pack_pair(0x123, 0x456, b);
    CHECK(b[0] == 0x23 && b[1] == 0x61 && b[2] == 0x45, "ByteEncode12 of (0x123, 0x456)");
    pack_pair(3328, 3328, b);
    CHECK(b[0] == 0x00 && b[1] == 0x0D && b[2] == 0xD0, "ByteEncode12 of (3328, 3328)");
    uint16_t r0, r1;
    unpack_pair((const uint8_t[]){0x23, 0x61, 0x45}, &r0, &r1);
    CHECK(r0 == 0x123 && r1 == 0x456, "unpack");
    for (int i = 0; i < NTT_N; i++) { f[i] = rnd(); g[i] = rnd(); }
    memcpy(h, f, sizeof h); memcpy(s, g, sizeof s);
    ref_ntt(h); ref_ntt(s); ref_basemul(h, s, h); ref_intt(h); ref_scale(h);
    ref_schoolbook(f, g, s);
    CHECK(!memcmp(h, s, sizeof h), "plain flow equals schoolbook");

    // chip level
    CHECK(NTT_KM == 13 * 13 * 13 && NTT_KM * NTT_KM_INV % NTT_Q == 1 &&
          NTT_SCALE_C == (uint32_t)NTT_SCALE * NTT_KM_INV % NTT_Q * NTT_KM_INV % NTT_Q, "constants 2197, 944, 304");
    CHECK(ntt_zeta_s[0] == 944 && ntt_zeta_s[1] == 966 && ntt_zeta_s[2] == 2021 && ntt_zeta_s[3] == 2188 &&
          ntt_zeta_s[64] == 2732 && ntt_zeta_s[127] == 2686, "scaled ROM'");
    CHECK(ntt_gamma_s[0] == 2732 && ntt_gamma_s[1] == 597 && ntt_gamma_s[127] == 643, "scaled gamma'");
    static const uint16_t mk[][3] = {{1, 1, 2197}, {966, 2000, 2498}, {3328, 3328, 2197}, {3328, 2, 2264},
                                     {1234, 2345, 350}, {0, 3328, 0}, {500, 304, 2023}, {3328, 304, 1241}};
    int mok = 1;
    for (unsigned i = 0; i < sizeof mk / sizeof mk[0]; i++) mok &= chip_mul(mk[i][0], mk[i][1]) == mk[i][2];
    CHECK(mok, "chip_mul worked products");
    int all = 1;
    for (uint32_t a = 0; a < NTT_Q; a += 97)
        for (uint32_t c = 0; c < NTT_Q; c++) all &= chip_mul((uint16_t)a, (uint16_t)c) == NTT_KM * a % NTT_Q * c % NTT_Q;
    CHECK(all, "chip_mul = 2197 a b, sampled");
    memset(f, 0, sizeof f); memset(g, 0, sizeof g);
    f[0] = 5; f[1] = 7; g[0] = 11; g[1] = 13;
    chip_basemul(f, g, h);
    CHECK(h[0] == 841 && h[1] == 2377, "basemul piece 0 at the pins (841, 2377)");
    ref_basemul(f, g, h);
    CHECK(h[0] == 1602 && h[1] == 142, "plain basemul (1602, 142)");
    f[0] = 2197; f[1] = 500u * NTT_KM % NTT_Q; f[2] = 3328u * NTT_KM % NTT_Q;
    chip_scale(f);
    CHECK(f[0] == 3303 && f[1] == 316 && f[2] == 26, "scale 2197 -> 3303, then 316, 26");
    for (int i = 0; i < NTT_N; i++) { f[i] = rnd(); g[i] = rnd(); }
    memcpy(h, f, sizeof h); memcpy(s, g, sizeof s);
    for (int l = 0; l < 7; l++) { chip_ntt_layer(h, l, 0); chip_ntt_layer(s, l, 0); }
    chip_basemul(h, s, h);
    for (int l = 0; l < 7; l++) chip_ntt_layer(h, l, 1);
    chip_scale(h);
    ref_schoolbook(f, g, s);
    CHECK(!memcmp(h, s, sizeof h), "chip-level flow equals schoolbook");
    if (what) *what = "all";
    return 0;
}
