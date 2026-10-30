// SPDX-License-Identifier: Apache-2.0
// SHA-3 / SHAKE in software on the RP2350 (FIPS 202): the "RP2350 alone" reference the hash chip is timed
// against. Portable C (the compact loop form of Keccak-f[1600], as in tiny_sha3), compiled -O2.
#include "sha3_sw.h"

#include <string.h>

static const uint64_t RC[24] = {
    0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808Aull, 0x8000000080008000ull,
    0x000000000000808Bull, 0x0000000080000001ull, 0x8000000080008081ull, 0x8000000000008009ull,
    0x000000000000008Aull, 0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000Aull,
    0x000000008000808Bull, 0x800000000000008Bull, 0x8000000000008089ull, 0x8000000000008003ull,
    0x8000000000008002ull, 0x8000000000000080ull, 0x000000000000800Aull, 0x800000008000000Aull,
    0x8000000080008081ull, 0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull};
static const uint8_t ROTC[24] = {1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14, 27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44};
static const uint8_t PILN[24] = {10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4, 15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1};

static inline uint64_t rol(uint64_t x, unsigned n) { return (x << n) | (x >> (64 - n)); }

void keccak_f1600(uint64_t st[25]) {
    uint64_t bc[5], t;
    for (int r = 0; r < 24; r++) {
        for (int i = 0; i < 5; i++) bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
        for (int i = 0; i < 5; i++) {
            t = bc[(i + 4) % 5] ^ rol(bc[(i + 1) % 5], 1);
            for (int j = 0; j < 25; j += 5) st[j + i] ^= t;
        }
        t = st[1];
        for (int i = 0; i < 24; i++) {
            int j = PILN[i];
            bc[0] = st[j];
            st[j] = rol(t, ROTC[i]);
            t = bc[0];
        }
        for (int j = 0; j < 25; j += 5) {
            for (int i = 0; i < 5; i++) bc[i] = st[j + i];
            for (int i = 0; i < 5; i++) st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
        }
        st[0] ^= RC[r];
    }
}

static inline void xor_byte(uint64_t st[25], unsigned i, uint8_t b) { st[i / 8] ^= (uint64_t)b << (8 * (i % 8)); }
static inline uint8_t get_byte(const uint64_t st[25], unsigned i) { return (uint8_t)(st[i / 8] >> (8 * (i % 8))); }

void sha3_sponge(unsigned rate, uint8_t dom, const uint8_t *msg, uint32_t len, uint8_t *out, uint32_t outlen) {
    uint64_t st[25];
    memset(st, 0, sizeof st);
    unsigned pos = 0;
    for (uint32_t i = 0; i < len; i++) {
        xor_byte(st, pos++, msg[i]);
        if (pos == rate) { keccak_f1600(st); pos = 0; }
    }
    xor_byte(st, pos, dom);
    xor_byte(st, rate - 1, 0x80);
    keccak_f1600(st);
    pos = 0;
    for (uint32_t i = 0; i < outlen; i++) {
        if (pos == rate) { keccak_f1600(st); pos = 0; }
        out[i] = get_byte(st, pos++);
    }
}

static int bit(const uint8_t *b, unsigned i) { return (b[i / 8] >> (i % 8)) & 1; }

void cbd_pack(const uint8_t *buf, int eta, uint8_t *out) {
    // SamplePolyCBD (FIPS 203 Alg. 8), packed like the chip's BURST_CBD output (4-bit two's complement):
    // eta 2: {f[2i+1], f[2i]} per byte; eta 3: {0, f0}, {0, f1}, {f3, f2} per three bytes
    int8_t f[256];
    for (int i = 0; i < 256; i++) {
        int x = 0, y = 0;
        for (int j = 0; j < eta; j++) { x += bit(buf, 2 * i * eta + j); y += bit(buf, 2 * i * eta + eta + j); }
        f[i] = (int8_t)(x - y);
    }
    if (eta == 2) {
        for (int i = 0; i < 128; i++) out[i] = (uint8_t)(((f[2 * i + 1] & 15) << 4) | (f[2 * i] & 15));
    } else {
        for (int j = 0; j < 64; j++) {
            out[3 * j] = (uint8_t)(f[4 * j] & 15);
            out[3 * j + 1] = (uint8_t)(f[4 * j + 1] & 15);
            out[3 * j + 2] = (uint8_t)(((f[4 * j + 3] & 15) << 4) | (f[4 * j + 2] & 15));
        }
    }
}
