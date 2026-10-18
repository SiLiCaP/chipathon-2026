// SPDX-License-Identifier: Apache-2.0
// Test and demo programs on top of the harness. See app.h.
#include "app.h"

#include <stdio.h>
#include <string.h>

#include "timing.h"

// Streaming-only clock estimates from docs/host_timing.md, layer waits included.
#define CLK_NTT (7 * (128 * TT_P_F + TT_D_F + 3))
#define CLK_INTT (7 * (128 * TT_P_I + TT_D_I + 3))
#define CLK_BM (128 * TT_P_B + TT_D_B + 3)
#define CLK_SC (128 * TT_P_S + TT_D_S + 3)

static uint32_t rng_state;
static void seed_rng(uint32_t s) { rng_state = s ? s : 1; }
static uint32_t xorshift(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
static void rand_poly(uint16_t f[NTT_N]) {
    for (int i = 0; i < NTT_N; i++) {
        uint32_t v;
        do v = xorshift() & 0xFFF; while (v >= NTT_Q);    // uniform 0..3328
        f[i] = (uint16_t)v;
    }
}

static double chip_us(uint32_t clocks) {
    uint32_t hz = plat_chip_hz();
    return hz ? clocks * 1e6 / hz : 0.0;
}

static int first_diff(const uint16_t *a, const uint16_t *b, int n) {
    for (int i = 0; i < n; i++)
        if (a[i] != b[i]) return i;
    return -1;
}

#define TRY(call) do { int e_ = (call); if (e_) { printf("  %s: %s\n", #call, ntt_err_str(e_)); return 1; } } while (0)

// ---------------- selftest ----------------
static int report(const char *name, int ok) {
    printf("  %-48s %s\n", name, ok ? "ok" : "FAIL");
    return !ok;
}

int app_selftest(ntt_chip_t *c) {
    int fails = 0;
    const char *what;
    int r = ntt_ref_selfcheck(&what);
    printf("reference model (C twin of test/ntt_ref.py): %s\n", r ? "FAIL" : "all known answers match");
    if (r) { printf("  check %d failed: %s\n", r, what); fails++; }

    static uint16_t f[NTT_N], g[NTT_N], h[NTT_N], s[NTT_N];
    printf("chip known answers (%s):\n", c->io->name);
    for (int i = 0; i < NTT_N; i++) f[i] = (uint16_t)i;
    TRY(ntt_chip_forward(c, f, h));
    static const uint16_t lo[8] = {2429, 2845, 425, 795, 1865, 1356, 624, 31};
    static const uint16_t hi[8] = {2883, 149, 2594, 3105, 2502, 2134, 2717, 2303};
    fails += report("forward NTT of the ramp, outputs 0-7 and 248-255", !memcmp(h, lo, 16) && !memcmp(h + 248, hi, 16));
    TRY(ntt_chip_inverse(c, h, g));
    fails += report("inverse NTT of that: 0, 128, 256, 384", g[0] == 0 && g[1] == 128 && g[2] == 256 && g[3] == 384);
    memset(f, 0, sizeof f); memset(g, 0, sizeof g);
    f[0] = 5; f[1] = 7; g[0] = 11; g[1] = 13;
    TRY(ntt_chip_basemul(c, f, g, h));
    fails += report("basemul piece 0 at the pins: 841, 2377", h[0] == 841 && h[1] == 2377);
    memset(f, 0, sizeof f);
    f[0] = 2197; f[1] = 3259; f[2] = 1132;
    TRY(ntt_chip_scale(c, f, h));
    fails += report("scale 2197 -> 3303, 3259 -> 316, 1132 -> 26", h[0] == 3303 && h[1] == 316 && h[2] == 26);
    seed_rng(1);
    rand_poly(f); rand_poly(g);
    TRY(ntt_chip_forward(c, f, h));
    TRY(ntt_chip_forward(c, g, s));
    TRY(ntt_chip_basemul(c, h, s, h));
    TRY(ntt_chip_inverse(c, h, h));
    TRY(ntt_chip_scale(c, h, h));
    ref_schoolbook(f, g, s);
    fails += report("forward, forward, basemul, inverse, scale = schoolbook", !memcmp(h, s, sizeof h));
    printf("selftest: %s\n", fails ? "FAIL" : "PASS");
    return fails;
}

// ---------------- the Spec 1 demo ----------------
int app_demo(ntt_chip_t *c, uint32_t seed) {
    static uint16_t f[NTT_N], g[NTT_N], F[NTT_N], G[NTT_N], H[NTT_N], ref[NTT_N];
    seed_rng(seed);
    rand_poly(f); rand_poly(g);
    printf("f * g mod (X^256 + 1, 3329), seed %lu, chip via %s\n", (unsigned long)seed, c->io->name);
    printf("  f = %u %u %u %u ...   g = %u %u %u %u ...\n", f[0], f[1], f[2], f[3], g[0], g[1], g[2], g[3]);
    struct { const char *name; uint32_t est, clocks; uint64_t us; } st[5] = {
        {"forward NTT of f", CLK_NTT, 0, 0}, {"forward NTT of g", CLK_NTT, 0, 0}, {"basemul", CLK_BM, 0, 0},
        {"inverse NTT", CLK_INTT, 0, 0}, {"scale (304)", CLK_SC, 0, 0}};
    uint64_t t0 = plat_time_us();
    for (int i = 0; i < 5; i++) {
        uint64_t t = plat_time_us();
        switch (i) {
        case 0: TRY(ntt_chip_forward(c, f, F)); break;
        case 1: TRY(ntt_chip_forward(c, g, G)); break;
        case 2: TRY(ntt_chip_basemul(c, F, G, H)); break;
        case 3: TRY(ntt_chip_inverse(c, H, H)); break;
        default: TRY(ntt_chip_scale(c, H, H)); break;
        }
        st[i].us = plat_time_us() - t;
        st[i].clocks = c->clocks;
    }
    uint64_t chip_wall = plat_time_us() - t0;
    t0 = plat_time_us();
    ref_schoolbook(f, g, ref);
    uint64_t cpu_us = plat_time_us() - t0;
    uint32_t total = 0, est = 0;
    printf("  %-18s %8s %9s %11s %10s\n", "step", "clocks", "estimate", "chip time", "wall");
    for (int i = 0; i < 5; i++) {
        printf("  %-18s %8lu %9lu %9.1f us %7llu us\n", st[i].name, (unsigned long)st[i].clocks,
               (unsigned long)st[i].est, chip_us(st[i].clocks), (unsigned long long)st[i].us);
        total += st[i].clocks;
        est += st[i].est;
    }
    printf("  %-18s %8lu %9lu %9.1f us %7llu us\n", "total", (unsigned long)total, (unsigned long)est,
           chip_us(total), (unsigned long long)chip_wall);
    printf("  CPU schoolbook: %llu us\n", (unsigned long long)cpu_us);
    int d = first_diff(H, ref, NTT_N);
    printf("  h = %u %u %u %u ...  %s\n", H[0], H[1], H[2], H[3], d < 0 ? "PASS: equals schoolbook" : "FAIL");
    if (d >= 0) printf("  first difference at %d: chip %u, schoolbook %u\n", d, H[d], ref[d]);
    return d >= 0;
}

// ---------------- fuzz: every mode against the chip-level model ----------------
static int check_modes(ntt_chip_t *c, const uint16_t f[NTT_N], const uint16_t g[NTT_N], const char *label) {
    static uint16_t got[NTT_N], exp[NTT_N];
    int bad = 0;
    TRY(ntt_chip_forward(c, f, got));
    memcpy(exp, f, sizeof exp);
    for (int l = 0; l < 7; l++) chip_ntt_layer(exp, l, 0);
    if (first_diff(got, exp, NTT_N) >= 0) { printf("  %s: forward NTT differs\n", label); bad++; }
    TRY(ntt_chip_inverse(c, f, got));
    memcpy(exp, f, sizeof exp);
    for (int l = 0; l < 7; l++) chip_ntt_layer(exp, l, 1);
    if (first_diff(got, exp, NTT_N) >= 0) { printf("  %s: inverse NTT differs\n", label); bad++; }
    TRY(ntt_chip_basemul(c, f, g, got));
    chip_basemul(f, g, exp);
    if (first_diff(got, exp, NTT_N) >= 0) { printf("  %s: basemul differs\n", label); bad++; }
    TRY(ntt_chip_scale(c, f, got));
    memcpy(exp, f, sizeof exp);
    chip_scale(exp);
    if (first_diff(got, exp, NTT_N) >= 0) { printf("  %s: scale differs\n", label); bad++; }
    return bad;
}

int app_fuzz(ntt_chip_t *c, uint32_t n, uint32_t seed) {
    static uint16_t f[NTT_N], g[NTT_N];
    int bad = 0;
    char label[32];
    // Spec 1 vector sets: zeros, all 3328, the ramp, single ones, then random polynomials
    memset(f, 0, sizeof f); memset(g, 0, sizeof g);
    bad += check_modes(c, f, g, "zeros");
    for (int i = 0; i < NTT_N; i++) f[i] = g[i] = NTT_Q - 1;
    bad += check_modes(c, f, g, "all 3328");
    for (int i = 0; i < NTT_N; i++) { f[i] = (uint16_t)i; g[i] = (uint16_t)(NTT_N - 1 - i); }
    bad += check_modes(c, f, g, "ramp");
    static const int pos[] = {0, 1, 127, 128, 255};
    for (unsigned p = 0; p < sizeof pos / sizeof pos[0]; p++) {
        memset(f, 0, sizeof f);
        f[pos[p]] = 1;
        snprintf(label, sizeof label, "one at %d", pos[p]);
        bad += check_modes(c, f, f, label);
    }
    seed_rng(seed);
    for (uint32_t i = 0; i < n; i++) {
        rand_poly(f); rand_poly(g);
        snprintf(label, sizeof label, "random %lu", (unsigned long)i);
        bad += check_modes(c, f, g, label);
    }
    printf("fuzz: %lu vectors x 4 modes against the chip-level model: %s\n", (unsigned long)(n + 8),
           bad ? "FAIL" : "PASS");
    return bad;
}

// ---------------- stretch goal: k = 2 matrix-vector product ----------------
int app_matvec(ntt_chip_t *c, uint32_t seed) {
    static uint16_t A[2][2][NTT_N], s[2][NTT_N], sh[2][NTT_N], t[2][NTT_N], p[NTT_N], ref[NTT_N], acc[NTT_N];
    seed_rng(seed);
    for (int i = 0; i < 2; i++) {
        rand_poly(s[i]);
        for (int j = 0; j < 2; j++) rand_poly(A[i][j]);
    }
    uint32_t clk[4] = {0};     // ntt, basemul, intt, scale
    uint64_t t0 = plat_time_us();
    for (int j = 0; j < 2; j++) { TRY(ntt_chip_forward(c, s[j], sh[j])); clk[0] += c->clocks; }
    int bad = 0;
    for (int i = 0; i < 2; i++) {
        memset(t[i], 0, sizeof t[i]);
        for (int j = 0; j < 2; j++) {
            static uint16_t ah[NTT_N];
            TRY(ntt_chip_forward(c, A[i][j], ah)); clk[0] += c->clocks;   // in ML-KEM A comes already in NTT form
            TRY(ntt_chip_basemul(c, ah, sh[j], p)); clk[1] += c->clocks;
            for (int k = 0; k < NTT_N; k++) t[i][k] = (uint16_t)((t[i][k] + p[k]) % NTT_Q);   // sums on the CPU
        }
        TRY(ntt_chip_inverse(c, t[i], t[i])); clk[2] += c->clocks;
        TRY(ntt_chip_scale(c, t[i], t[i])); clk[3] += c->clocks;
        memset(ref, 0, sizeof ref);
        for (int j = 0; j < 2; j++) {
            ref_schoolbook(A[i][j], s[j], acc);
            for (int k = 0; k < NTT_N; k++) ref[k] = (uint16_t)((ref[k] + acc[k]) % NTT_Q);
        }
        bad += first_diff(t[i], ref, NTT_N) >= 0;
    }
    uint64_t us = plat_time_us() - t0;
    uint32_t tot = clk[0] + clk[1] + clk[2] + clk[3];
    printf("t = A s for k = 2 (6 forward, 4 basemul, 2 inverse, 2 scale on the chip): %s\n", bad ? "FAIL" : "PASS");
    printf("  clocks: NTT %lu, basemul %lu (%.0f%%), inverse %lu, scale %lu; total %lu = %.1f us of chip time, %llu us wall\n",
           (unsigned long)clk[0], (unsigned long)clk[1], 100.0 * clk[1] / tot, (unsigned long)clk[2],
           (unsigned long)clk[3], (unsigned long)tot, chip_us(tot), (unsigned long long)us);
    return bad;
}

// ---------------- bench ----------------
int app_bench(ntt_chip_t *c, uint32_t n, uint32_t target_hz) {
    static uint16_t f[NTT_N], g[NTT_N], h[NTT_N];
    oplog_clear();                                       // the summary below covers exactly these calls
    seed_rng(7);
    rand_poly(f); rand_poly(g);
    const char *names[4] = {"forward NTT", "inverse NTT", "basemul", "scale"};
    printf("%lu calls each, chip at %lu Hz via %s\n", (unsigned long)n, (unsigned long)plat_chip_hz(), c->io->name);
    printf("  %-12s %8s %12s %12s\n", "call", "clocks", "chip time", "wall / call");
    for (int op = 0; op < 4; op++) {
        uint64_t t0 = plat_time_us();
        for (uint32_t i = 0; i < n; i++) {
            switch (op) {
            case 0: TRY(ntt_chip_forward(c, f, h)); break;
            case 1: TRY(ntt_chip_inverse(c, f, h)); break;
            case 2: TRY(ntt_chip_basemul(c, f, g, h)); break;
            default: TRY(ntt_chip_scale(c, f, h)); break;
            }
        }
        double wall = (double)(plat_time_us() - t0) / n;
        printf("  %-12s %8lu %9.1f us %9.1f us\n", names[op], (unsigned long)c->clocks, chip_us(c->clocks), wall);
    }
    uint64_t t0 = plat_time_us();
    for (uint32_t i = 0; i < n; i++) {
        ref_ntt(h);
    }
    printf("  CPU reference NTT (portable C, not optimized): %.1f us\n", (double)(plat_time_us() - t0) / n);
    printf("\nwhere the time goes, from the operation log (`log csv` for every batch):\n");
    oplog_summary(target_hz);
    return 0;
}

// ---------------- calibrate ----------------
int app_calibrate(ntt_chip_t *c) {
    static uint16_t f[NTT_N], h[NTT_N];
    static const uint16_t lo[8] = {2429, 2845, 425, 795, 1865, 1356, 624, 31};
    int first = -1, saved = c->skew;
    printf("sample skew scan (forward NTT of the ramp, 3 runs each):\n");
    for (int k = 0; k <= 4; k++) {
        c->skew = k;
        int ok = 1;
        for (int r = 0; r < 3 && ok; r++) {
            for (int i = 0; i < NTT_N; i++) f[i] = (uint16_t)i;
            ok = ntt_chip_forward(c, f, h) == NTT_OK && !memcmp(h, lo, sizeof lo);
        }
        printf("  skew %d: %s\n", k, ok ? "pass" : "fail");
        if (ok && first < 0) first = k;
    }
    c->skew = first >= 0 ? first : saved;
    printf("calibrate: %s, skew = %d\n", first >= 0 ? "PASS" : "FAIL (no skew works: lower the clock)", c->skew);
    return first < 0;
}
