// SPDX-License-Identifier: Apache-2.0
// Self test, fuzz and skew calibration for the hash chip. Mirrors test/test_hash_only.py: SHA3-256/512 and
// SHAKE128/256 at the rate edges (with and without SKIP, byte and burst squeeze) and ML-KEM's PRF through
// BURST_CBD, all against the software reference (sha3_sw.c), plus known answers that anchor the reference itself.
#include "hash_app.h"

#include <stdio.h>
#include <string.h>

#include "sha3_sw.h"

static const struct { unsigned rate; uint8_t dom; } PARAM[HASH_NKIND] = {
    [HASH_SHA3_256] = {136, 0x06}, [HASH_SHA3_512] = {72, 0x06}, [HASH_SHAKE128] = {168, 0x1F},
    [HASH_SHAKE256] = {136, 0x1F}, [HASH_PRF_CBD2] = {136, 0x1F}, [HASH_PRF_CBD3] = {136, 0x1F},
};

void hash_ref(hash_kind_t kind, const uint8_t *msg, uint32_t len, uint8_t *out, uint32_t outlen) {
    if (kind == HASH_PRF_CBD2 || kind == HASH_PRF_CBD3) {
        const int eta = kind == HASH_PRF_CBD2 ? 2 : 3;
        uint8_t prf[192];
        sha3_sponge(PARAM[kind].rate, PARAM[kind].dom, msg, len, prf, 64u * eta);
        cbd_pack(prf, eta, out);
        return;
    }
    sha3_sponge(PARAM[kind].rate, PARAM[kind].dom, msg, len, out, outlen);
}

static uint32_t rnd_state;
static uint32_t rnd(void) {                           // xorshift32
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 17;
    rnd_state ^= rnd_state << 5;
    return rnd_state;
}

// one job on the chip vs the reference; prints the first difference
static int check_job(hash_chip_t *c, hash_kind_t kind, const uint8_t *msg, uint32_t len, uint32_t outlen, unsigned opts,
                     const char *what) {
    static uint8_t got[1200], want[1200];
    if (outlen > sizeof got) return 1;
    const uint32_t n = kind >= HASH_PRF_CBD2 ? hash_kind_fixed_len(kind) : outlen;   // PRF kinds: 64 * eta
    int err = hash_chip_job(c, kind, msg, len, got, outlen, opts);
    if (err) {
        printf("  FAIL %s %s len %lu: %s\n", what, hash_kind_name(kind), (unsigned long)len, hash_err_str(err));
        return 1;
    }
    hash_ref(kind, msg, len, want, outlen);
    if (memcmp(got, want, n)) {
        uint32_t i = 0;
        while (got[i] == want[i]) i++;
        printf("  FAIL %s %s len %lu opts %u: byte %lu is %02x, want %02x\n", what, hash_kind_name(kind), (unsigned long)len,
               opts, (unsigned long)i, got[i], want[i]);
        return 1;
    }
    return 0;
}

// anchors for the software reference itself (hashlib values)
static int known_answers(hash_chip_t *c) {
    static const struct { hash_kind_t kind; const char *msg; unsigned outlen; const char *hex; } KAT[] = {
        {HASH_SHA3_256, "abc", 32, "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"},
        {HASH_SHA3_512, "", 64, "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a6"
                                "15b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26"},
        {HASH_SHAKE128, "", 32, "7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26"},
        {HASH_SHAKE256, "", 32, "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f"},
    };
    int fail = 0;
    for (unsigned i = 0; i < sizeof KAT / sizeof KAT[0]; i++)
        for (unsigned burst = 0; burst < 2; burst++) {
            uint8_t got[64];
            char hx[129];
            int err = hash_chip_job(c, KAT[i].kind, (const uint8_t *)KAT[i].msg, (uint32_t)strlen(KAT[i].msg), got,
                                    KAT[i].outlen, burst ? 0 : HASH_BYTEWISE);
            for (unsigned k = 0; k < KAT[i].outlen && !err; k++) snprintf(hx + 2 * k, 3, "%02x", got[k]);
            if (err || strcmp(hx, KAT[i].hex)) {
                printf("  FAIL known answer %s%s: %s\n", hash_kind_name(KAT[i].kind), burst ? " (burst)" : "",
                       err ? hash_err_str(err) : hx);
                fail++;
            }
        }
    printf("  known answers (SHA3-256 abc, SHA3-512 / SHAKE128 / SHAKE256 empty, byte + burst): %s\n", fail ? "FAIL" : "ok");
    return fail;
}

static int algo_sweep(hash_chip_t *c) {
    int fail = 0, n = 0;
    uint8_t msg[300];
    for (int kind = HASH_SHA3_256; kind <= HASH_SHAKE256; kind++) {
        const unsigned rate = PARAM[kind].rate;
        const unsigned lens[] = {0, 1, rate - 1, rate, rate + 1, 200};
        for (unsigned i = 0; i < sizeof lens / sizeof lens[0]; i++) {
            for (unsigned k = 0; k < lens[i]; k++) msg[k] = (uint8_t)(7 * k + lens[i]);
            const uint32_t outlen = kind == HASH_SHA3_256 ? 32 : kind == HASH_SHA3_512 ? 64 : kind == HASH_SHAKE128 ? 504 : 192;
            for (unsigned opts = 0; opts < 4; opts++) {      // bytewise / burst, with / without SKIP
                fail += check_job(c, (hash_kind_t)kind, msg, lens[i], outlen, ((opts & 1) ? HASH_BYTEWISE : 0) | ((opts & 2) ? HASH_SKIP : 0), "sweep");
                n++;
            }
        }
    }
    printf("  SHA-3 / SHAKE at the rate edges (%d jobs, byte + burst squeeze, +- SKIP): %s\n", n, fail ? "FAIL" : "ok");
    return fail;
}

static int cbd_sweep(hash_chip_t *c) {
    int fail = 0, n = 0;
    uint8_t seed[33];
    for (int eta = 2; eta <= 3; eta++)
        for (unsigned nonce = 0; nonce < 4; nonce++) {
            for (unsigned k = 0; k < 32; k++) seed[k] = (uint8_t)(5 * k + nonce);
            seed[32] = (uint8_t)nonce;
            fail += check_job(c, eta == 2 ? HASH_PRF_CBD2 : HASH_PRF_CBD3, seed, 33, 0, 0, "cbd");
            n++;
        }
    printf("  PRF + SamplePolyCBD through BURST_CBD, eta 2 and 3 (%d jobs): %s\n", n, fail ? "FAIL" : "ok");
    return fail;
}

int app_selftest(hash_chip_t *c) {
    printf("selftest: hash chip, %s\n", c->io->name);
    int fail = known_answers(c);
    fail += algo_sweep(c);
    fail += cbd_sweep(c);
    printf("selftest: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}

int app_fuzz(hash_chip_t *c, uint32_t n, uint32_t seed) {
    static uint8_t msg[400];
    int fail = 0;
    rnd_state = seed ? seed : 1;
    for (uint32_t j = 0; j < n; j++) {
        const hash_kind_t kind = (hash_kind_t)(rnd() % HASH_NKIND);
        const uint32_t len = rnd() % 380;
        for (uint32_t k = 0; k < len; k++) msg[k] = (uint8_t)rnd();
        if (rnd() % 4 == 0) memset(msg + len / 3, 0, (len - len / 3) / 2);      // zero lanes for SKIP
        uint32_t outlen = hash_kind_fixed_len(kind);
        if (kind == HASH_SHAKE128 || kind == HASH_SHAKE256) outlen = 1 + rnd() % 420;
        const unsigned opts = (rnd() & 1 ? HASH_BYTEWISE : 0) | (rnd() & 1 ? HASH_SKIP : 0);
        fail += check_job(c, kind, msg, len, outlen, opts, "fuzz");
    }
    printf("fuzz: %s (%lu random jobs)\n", fail ? "FAIL" : "PASS", (unsigned long)n);
    return fail ? 1 : 0;
}

int app_calibrate(hash_chip_t *c) {
    static const uint8_t abc[3] = {'a', 'b', 'c'};
    uint8_t got[32], want[32];
    hash_ref(HASH_SHA3_256, abc, 3, want, 32);
    const int keep = c->skew;
    int found = -1;
    for (int skew = 0; skew <= 2 && found < 0; skew++) {
        c->skew = skew;
        if (!hash_chip_job(c, HASH_SHA3_256, abc, 3, got, 32, 0) && !memcmp(got, want, 32)) found = skew;
    }
    c->skew = found < 0 ? keep : found;
    if (found < 0) printf("cal: no skew 0..2 gives the SHA3-256 known answer\n");
    else printf("cal: skew %d\n", found);
    return found < 0;
}
