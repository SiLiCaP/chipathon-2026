// SPDX-License-Identifier: Apache-2.0
// Speed tests for the hash chip (hash_bench.h). Timing is the host's: on the RP2350 the cycle counter at the
// system clock, so "chip" times include everything the firmware does (building the pin words, the PIO + DMA run
// at the chip clock, picking the bytes out and checking the quiet clocks), as the firmware is written today.
// "software" is the same work in portable C on the same CPU (sha3_sw.c: FIPS 202, SamplePolyCBD, SampleNTT).
#include "hash_bench.h"

#include <stdio.h>
#include <string.h>

#include "hash_app.h"
#include "sha3_sw.h"

// ---------------- the workload: ML-KEM-512 (FIPS 203), one key exchange ----------------
typedef struct { const char *op, *what; hash_kind_t kind; uint16_t inlen, outlen; } call_t;

// keygen: G(d||k), 4 x XOF -> A, 4 x PRF_3 -> s, e, H(ek); encaps: H(ek), G(m||h), 4 x XOF, 2 x PRF_3 (y),
// 3 x PRF_2 (e1, e2); decaps: G(m'||h), J(z||c), then the re-encryption: 4 x XOF, 2 x PRF_3, 3 x PRF_2.
static const call_t MLKEM512[] = {
    {"keygen", "G", HASH_SHA3_512, 33, 64},
    {"keygen", "XOF", HASH_XOF_NTT, 34, 384}, {"keygen", "XOF", HASH_XOF_NTT, 34, 384},
    {"keygen", "XOF", HASH_XOF_NTT, 34, 384}, {"keygen", "XOF", HASH_XOF_NTT, 34, 384},
    {"keygen", "PRF3", HASH_PRF_CBD3, 33, 192}, {"keygen", "PRF3", HASH_PRF_CBD3, 33, 192},
    {"keygen", "PRF3", HASH_PRF_CBD3, 33, 192}, {"keygen", "PRF3", HASH_PRF_CBD3, 33, 192},
    {"keygen", "H", HASH_SHA3_256, 800, 32},
    {"encaps", "H", HASH_SHA3_256, 800, 32}, {"encaps", "G", HASH_SHA3_512, 64, 64},
    {"encaps", "XOF", HASH_XOF_NTT, 34, 384}, {"encaps", "XOF", HASH_XOF_NTT, 34, 384},
    {"encaps", "XOF", HASH_XOF_NTT, 34, 384}, {"encaps", "XOF", HASH_XOF_NTT, 34, 384},
    {"encaps", "PRF3", HASH_PRF_CBD3, 33, 192}, {"encaps", "PRF3", HASH_PRF_CBD3, 33, 192},
    {"encaps", "PRF2", HASH_PRF_CBD2, 33, 128}, {"encaps", "PRF2", HASH_PRF_CBD2, 33, 128},
    {"encaps", "PRF2", HASH_PRF_CBD2, 33, 128},
    {"decaps", "G", HASH_SHA3_512, 64, 64}, {"decaps", "J", HASH_SHAKE256, 800, 32},
    {"decaps", "XOF", HASH_XOF_NTT, 34, 384}, {"decaps", "XOF", HASH_XOF_NTT, 34, 384},
    {"decaps", "XOF", HASH_XOF_NTT, 34, 384}, {"decaps", "XOF", HASH_XOF_NTT, 34, 384},
    {"decaps", "PRF3", HASH_PRF_CBD3, 33, 192}, {"decaps", "PRF3", HASH_PRF_CBD3, 33, 192},
    {"decaps", "PRF2", HASH_PRF_CBD2, 33, 128}, {"decaps", "PRF2", HASH_PRF_CBD2, 33, 128},
    {"decaps", "PRF2", HASH_PRF_CBD2, 33, 128},
};
#define NCALL (sizeof MLKEM512 / sizeof MLKEM512[0])
static const char *OPS[3] = {"keygen", "encaps", "decaps"};

static const struct { unsigned rate; uint8_t dom; } SW[HASH_NKIND] = {
    [HASH_SHA3_256] = {136, 0x06}, [HASH_SHA3_512] = {72, 0x06}, [HASH_SHAKE128] = {168, 0x1F},
    [HASH_SHAKE256] = {136, 0x1F}, [HASH_PRF_CBD2] = {136, 0x1F}, [HASH_PRF_CBD3] = {136, 0x1F},
    [HASH_XOF_NTT] = {168, 0x1F},
};

// The RP2350-alone version of one call, as a software ML-KEM does it: SHA-3 + CBD, or SHAKE128 + SampleNTT with
// just enough XOF blocks (3, now and then 4), not the reference's fixed 8.
static void sw_call(hash_kind_t kind, const uint8_t *msg, uint32_t len, uint8_t *out, uint32_t outlen) {
    static uint8_t buf[8 * 168];
    if (kind == HASH_PRF_CBD2 || kind == HASH_PRF_CBD3) {
        const int eta = kind == HASH_PRF_CBD2 ? 2 : 3;
        sha3_sponge(SW[kind].rate, SW[kind].dom, msg, len, buf, 64u * eta);
        cbd_pack(buf, eta, out);
    } else if (kind == HASH_XOF_NTT) {
        for (uint32_t n = 3 * 168; n <= sizeof buf; n += 168) {
            sha3_sponge(168, 0x1F, msg, len, buf, n);
            if (sample_ntt_pack(buf, n, out)) return;
        }
    } else {
        sha3_sponge(SW[kind].rate, SW[kind].dom, msg, len, out, outlen);
    }
}

static uint32_t rnd_state = 0x2545F491u;
static uint32_t rnd(void) {
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 17;
    rnd_state ^= rnd_state << 5;
    return rnd_state;
}

static double us(const bench_plat_t *p, uint64_t cyc) { return (double)cyc * 1e6 / (double)p->cyc_hz; }

// The chip's fastest way to do a call: bare data bytes in packets (ABSORB_N), the chip's own padding (FINISH),
// one burst word for the whole squeeze (blocks that follow by themselves). SampleNTT needs framing mode 1.
#define FAST (HASH_RAW | HASH_FINISH | HASH_AUTO)

typedef struct { uint64_t chip, sw, clocks; uint32_t calls; } tally_t;

// Where the chip path's time goes: a wrapper around the transport times its runs (the PIO + DMA run at the chip
// clock, plus the word conversion inside it); the rest is the driver's own CPU work (building the pin words,
// picking the bytes out, checking the quiet clocks).
static tt_io_t *real_io;
static const bench_plat_t *cur_plat;
static uint64_t run_cyc;
static int timed_run(tt_io_t *io, const uint16_t *words, uint32_t n, uint8_t *uo, uint8_t *aux) {
    (void)io;
    const uint32_t t0 = cur_plat->cycles();
    const int r = real_io->run(real_io, words, n, uo, aux);
    run_cyc += cur_plat->cycles() - t0;
    return r;
}

int app_bench(hash_chip_t *c, const bench_plat_t *p) {
    static uint8_t msg[4096], got[1344], want[1344];
    const uint32_t hz = p->clock_hz ? p->clock_hz() : 0;
    int errors = 0;
    const int mode1 = hash_chip_has_mode1(c);
    if (mode1 && hash_chip_set_mode(c, 1)) {
        printf("bench: the chip did not switch to framing mode 1\n");
        return 1;
    }
    printf("bench: ML-KEM-512, the 32 hash calls of one key exchange, on the chip and in software on the %s\n"
           "  chip clock %.2f MHz, sample skew %d, framing mode %d, chip path: packets + on-chip padding + one burst word\n",
           p->host, hz / 1e6, c->skew, c->mode);
    tally_t op[3] = {{0}}, all = {0};
    rnd_state = 0x2545F491u;
    tt_io_t proxy = *c->io;
    real_io = c->io;
    cur_plat = p;
    run_cyc = 0;
    proxy.run = timed_run;
    c->io = &proxy;
    const uint32_t calls0 = c->calls;
    struct { const char *what; uint32_t n, runs; uint64_t chip, run, clocks; } kinds[8] = {{0}};
    unsigned nk = 0;
    for (unsigned i = 0; i < NCALL; i++) {
        const call_t *k = &MLKEM512[i];
        hash_kind_t kind = k->kind;
        if (kind == HASH_XOF_NTT && !mode1) kind = HASH_SHAKE128;   // no valid pin on this transport: raw XOF bytes
        const uint32_t outlen = kind == HASH_SHAKE128 && k->kind == HASH_XOF_NTT ? 504 : k->outlen;
        for (unsigned j = 0; j < k->inlen; j++) msg[j] = (uint8_t)rnd();
        const uint64_t clk0 = c->clocks_total, run0 = run_cyc;
        const uint32_t ncall0 = c->calls;
        uint32_t t0 = p->cycles();
        int err = hash_chip_job(c, kind, msg, k->inlen, got, outlen, FAST);
        const uint32_t tc = p->cycles() - t0;
        unsigned w = 0;
        while (w < nk && strcmp(kinds[w].what, k->what)) w++;
        if (w == nk && nk < 8) kinds[nk++].what = k->what;
        if (w < 8) {
            kinds[w].n++; kinds[w].chip += tc; kinds[w].run += run_cyc - run0;
            kinds[w].clocks += c->clocks_total - clk0; kinds[w].runs += c->calls - ncall0;
        }
        t0 = p->cycles();
        sw_call(k->kind, msg, k->inlen, want, k->outlen);
        const uint32_t ts = p->cycles() - t0;
        uint8_t ref[1344];
        hash_ref(kind, msg, k->inlen, ref, outlen);
        if (err || memcmp(got, ref, outlen)) {
            printf("  FAIL %s %s: %s\n", k->op, k->what, err ? hash_err_str(err) : "result differs from the reference");
            errors++;
        }
        const int o = k->op[0] == 'k' ? 0 : k->op[0] == 'e' ? 1 : 2;
        op[o].chip += tc; op[o].sw += ts; op[o].clocks += c->clocks_total - clk0; op[o].calls++;
    }
    c->io = real_io;
    const uint32_t tcalls = c->calls - calls0;
    printf("  %-7s %5s %14s %14s %9s %12s %16s\n", "op", "calls", "software", "with the chip", "speed-up",
           "chip clocks", "clocks / f_chip");
    for (int o = 0; o < 3; o++) {
        all.chip += op[o].chip; all.sw += op[o].sw; all.clocks += op[o].clocks; all.calls += op[o].calls;
        printf("  %-7s %5lu %11.1f us %11.1f us %8.2fx %12llu %13.1f us\n", OPS[o], (unsigned long)op[o].calls,
               us(p, op[o].sw), us(p, op[o].chip), (double)op[o].sw / (double)op[o].chip,
               (unsigned long long)op[o].clocks, hz ? op[o].clocks * 1e6 / hz : 0.0);
    }
    printf("  %-7s %5lu %11.1f us %11.1f us %8.2fx %12llu %13.1f us\n", "total", (unsigned long)all.calls, us(p, all.sw),
           us(p, all.chip), (double)all.sw / (double)all.chip, (unsigned long long)all.clocks,
           hz ? all.clocks * 1e6 / hz : 0.0);
    printf("BENCH workload=mlkem512_exchange clock_hz=%lu skew=%d mode=%d calls=%lu sw_us=%.1f chip_us=%.1f "
           "speedup=%.3f chip_clocks=%llu chip_only_us=%.1f errors=%d\n",
           (unsigned long)hz, c->skew, c->mode, (unsigned long)all.calls, us(p, all.sw), us(p, all.chip),
           (double)all.sw / (double)all.chip, (unsigned long long)all.clocks, hz ? all.clocks * 1e6 / hz : 0.0,
           errors);
    printf("  where the chip path's %.1f us go: %.1f us in %lu transport runs (PIO + DMA at the chip clock, incl. word\n"
           "  conversion), %.1f us in the driver (building the pin words, picking the bytes out, checking quiet clocks)\n",
           us(p, all.chip), us(p, run_cyc), (unsigned long)tcalls, us(p, all.chip - run_cyc));
    printf("  per call: %-5s %5s %12s %10s %12s %14s %14s\n", "kind", "n", "chip clocks", "runs", "with chip", "in runs", "in driver");
    for (unsigned w = 0; w < nk; w++)
        printf("            %-5s %5lu %12llu %10lu %9.1f us %11.1f us %11.1f us\n", kinds[w].what, (unsigned long)kinds[w].n,
               (unsigned long long)(kinds[w].clocks / kinds[w].n), (unsigned long)(kinds[w].runs / kinds[w].n),
               us(p, kinds[w].chip) / kinds[w].n, us(p, kinds[w].run) / kinds[w].n,
               us(p, kinds[w].chip - kinds[w].run) / kinds[w].n);
    printf("BENCH workload=breakdown clock_hz=%lu run_us=%.1f driver_us=%.1f transport_calls=%lu\n", (unsigned long)hz,
           us(p, run_cyc), us(p, all.chip - run_cyc), (unsigned long)tcalls);
    for (int o = 0; o < 3; o++)
        printf("BENCH workload=%s clock_hz=%lu sw_us=%.1f chip_us=%.1f chip_clocks=%llu\n", OPS[o], (unsigned long)hz,
               us(p, op[o].sw), us(p, op[o].chip), (unsigned long long)op[o].clocks);

    // raw rates: absorb (SHA3-256 of 4 KiB) and squeeze (SHAKE128, 8 blocks = 1344 bytes)
    for (unsigned j = 0; j < sizeof msg; j++) msg[j] = (uint8_t)rnd();
    struct { const char *name; hash_kind_t kind; uint32_t inlen, outlen; } RAW[] = {
        {"absorb", HASH_SHA3_256, 4096, 32}, {"squeeze", HASH_SHAKE128, 34, 1344}};
    for (unsigned r = 0; r < 2; r++) {
        const uint64_t clk0 = c->clocks_total;
        uint32_t t0 = p->cycles();
        int err = hash_chip_job(c, RAW[r].kind, msg, RAW[r].inlen, got, RAW[r].outlen, FAST);
        const uint32_t tc = p->cycles() - t0;
        t0 = p->cycles();
        sha3_sponge(SW[RAW[r].kind].rate, SW[RAW[r].kind].dom, msg, RAW[r].inlen, want, RAW[r].outlen);
        const uint32_t ts = p->cycles() - t0;
        if (err || memcmp(got, want, RAW[r].outlen)) {
            printf("  FAIL %s: %s\n", RAW[r].name, err ? hash_err_str(err) : "result differs");
            errors++;
        }
        const uint32_t bytes = r ? RAW[r].outlen : RAW[r].inlen;
        const uint64_t clocks = c->clocks_total - clk0;
        printf("  %-7s %5lu B   software %7.3f MB/s   with the chip %7.3f MB/s   chip alone %7.3f MB/s (%llu clocks)\n",
               RAW[r].name, (unsigned long)bytes, bytes / us(p, ts), bytes / us(p, tc),
               hz ? bytes * (hz / 1e6) / (double)clocks : 0.0, (unsigned long long)clocks);
        printf("BENCH workload=%s clock_hz=%lu bytes=%lu sw_us=%.1f chip_us=%.1f chip_clocks=%llu\n", RAW[r].name,
               (unsigned long)hz, (unsigned long)bytes, us(p, ts), us(p, tc), (unsigned long long)clocks);
    }
    if (mode1) hash_chip_set_mode(c, 0);
    printf("bench: %s\n", errors ? "FAIL" : "PASS (every chip result equals the software one)");
    return errors != 0;
}

// ---------------- wire speed ----------------
// A known-answer set that exercises every pin and output path: bursts, blocks that follow by themselves,
// packets + on-chip padding, CBD; SampleNTT and the valid / busy pins when the transport has mode 1.
static int kat_set(hash_chip_t *c) {
    static uint8_t msg[200], got[504], want[504];
    static const struct { hash_kind_t kind; uint16_t inlen, outlen; unsigned opts; } SET[] = {
        {HASH_SHA3_256, 3, 32, 0}, {HASH_SHA3_512, 200, 64, HASH_RAW | HASH_FINISH},
        {HASH_SHAKE128, 34, 504, HASH_AUTO}, {HASH_PRF_CBD3, 33, 192, HASH_AUTO}, {HASH_PRF_CBD2, 33, 128, 0},
        {HASH_SHA3_256, 136, 32, HASH_BYTEWISE | HASH_SKIP},
    };
    for (unsigned i = 0; i < sizeof SET / sizeof SET[0]; i++) {
        for (unsigned j = 0; j < SET[i].inlen; j++) msg[j] = i == 0 ? (uint8_t)"abc"[j] : (uint8_t)rnd();
        if (hash_chip_job(c, SET[i].kind, msg, SET[i].inlen, got, SET[i].outlen, SET[i].opts)) return 0;
        hash_ref(SET[i].kind, msg, SET[i].inlen, want, SET[i].outlen);
        if (memcmp(got, want, hash_kind_fixed_len(SET[i].kind) ? hash_kind_fixed_len(SET[i].kind) : SET[i].outlen))
            return 0;
    }
    if (!hash_chip_has_mode1(c)) return 1;
    int ok = !hash_chip_set_mode(c, 1);
    for (unsigned j = 0; j < 34; j++) msg[j] = (uint8_t)rnd();
    if (ok) ok = !hash_chip_job(c, HASH_XOF_NTT, msg, 34, got, 384, FAST);
    if (ok) {
        hash_ref(HASH_XOF_NTT, msg, 34, want, 384);
        ok = !memcmp(got, want, 384);
    }
    if (ok) ok = !hash_chip_job(c, HASH_PRF_CBD3, msg, 33, got, 192, FAST);
    if (ok) {
        hash_ref(HASH_PRF_CBD3, msg, 33, want, 192);
        ok = !memcmp(got, want, 192);
    }
    hash_chip_set_mode(c, 0);
    return ok;
}

int app_speedtest(hash_chip_t *c, const bench_plat_t *p, uint32_t max_hz) {
    if (!p->set_clock) {
        printf("speedtest: this transport has a fixed clock; running the benchmark only\n");
        return app_bench(c, p);
    }
    // clocks the host can make: system clock / n, from slow to fast
    // (a faster board than the FPGA rig is expected: the real chip on its PCB. The list goes to 125 MHz, which a
    // 250 MHz RP2350 can make; rp2350/speedtest.py raises the system clock when even the fastest clock passes.)
    static const uint32_t TRY_KHZ[] = {1000, 5000, 10000, 15000, 20000, 25000, 30000, 37500, 42900, 50000, 62500,
                                       66700, 75000, 83300, 100000, 125000};
    enum { NT = sizeof TRY_KHZ / sizeof TRY_KHZ[0], NSKEW = 4 };
    uint32_t got_hz[NT];
    uint8_t pass[NT][NSKEW];
    const uint32_t keep_hz = p->clock_hz(), limit = max_hz ? max_hz : p->cyc_hz / 2;
    const int keep_skew = c->skew;
    unsigned n = 0;
    printf("speedtest 1/2: wire speed. At each chip clock and sample skew: 6 known-answer jobs (bursts, padding,\n"
           "packets, CBD)%s, 3 rounds; pass = every byte right.\n  %10s  skew 0  skew 1  skew 2  skew 3\n",
           hash_chip_has_mode1(c) ? " + SampleNTT and the valid / busy pins" : "", "clock");
    for (unsigned i = 0; i < NT; i++) {
        if (TRY_KHZ[i] * 1000u > limit) break;
        const uint32_t hz = p->set_clock(TRY_KHZ[i] * 1000u);
        if (n && hz == got_hz[n - 1]) continue;              // the host can't make this one: same as the last
        got_hz[n] = hz;
        printf("  %7.2f MHz", hz / 1e6);
        for (int k = 0; k < NSKEW; k++) {
            c->skew = k;
            int ok = 1;
            for (int r = 0; r < 3 && ok; r++) ok = kat_set(c);
            if (!ok) hash_chip_reset(c);                     // a failed run can leave the chip mid-command
            pass[n][k] = (uint8_t)ok;
            printf("  %6s", ok ? "pass" : "-");
        }
        printf("\nSWEEP clock_hz=%lu skew0=%d skew1=%d skew2=%d skew3=%d\n", (unsigned long)hz, pass[n][0], pass[n][1],
               pass[n][2], pass[n][3]);
        n++;
    }
    // highest clock passing at any skew; recommended = the highest clock whose skew also passes one step lower
    int best = -1, best_k = 0, top = -1;
    for (unsigned i = 0; i < n; i++)
        for (int k = 0; k < NSKEW; k++) {
            if (pass[i][k]) top = (int)i;
            if (pass[i][k] && i > 0 && pass[i - 1][k] && (int)i > best) { best = (int)i; best_k = k; }
        }
    if (best < 0 && n && pass[0][0]) { best = 0; best_k = 0; }
    if (best < 0) {
        printf("SPEEDTEST result=fail reason=no_clock_passes\nspeedtest: no clock passes. Check the board, `select`, and the reset.\n");
        p->set_clock(keep_hz);
        c->skew = keep_skew;
        return 1;
    }
    printf("SPEEDTEST max_pass_hz=%lu recommended_hz=%lu recommended_skew=%d tried_up_to_hz=%lu host_limited=%d sys_hz=%lu\n",
           (unsigned long)got_hz[top], (unsigned long)got_hz[best], best_k, (unsigned long)got_hz[n - 1],
           top == (int)n - 1, (unsigned long)p->cyc_hz);
    if (top == (int)n - 1)
        printf("speedtest: even the fastest clock this RP2350 can make passes: the board may go faster (`sys 250`)\n");
    printf("speedtest: highest passing clock %.2f MHz; running at %.2f MHz, skew %d (passes one step lower too)\n",
           got_hz[top] / 1e6, got_hz[best] / 1e6, best_k);
    printf("speedtest 2/2: throughput at that clock\n");
    p->set_clock(got_hz[best]);
    c->skew = best_k;
    hash_chip_reset(c);
    const int fail = app_bench(c, p);
    printf("SPEEDTEST result=%s\n", fail ? "fail" : "pass");
    printf("speedtest: left at %.2f MHz, skew %d (`clk %lu` sets the clock back)\n", p->clock_hz() / 1e6, c->skew,
           (unsigned long)keep_hz);
    return fail;
}
