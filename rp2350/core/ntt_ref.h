// SPDX-License-Identifier: Apache-2.0
// Reference model in C, the twin of test/ntt_ref.py (Spec 1).
//   plain level: FIPS 203 Algorithms 9-12 plus the final multiply by 3303, and schoolbook
//   chip level:  what the pins show. Every product carries 2197 = 13^3 (three K-RED rounds), the chip
//                stores ROM' = ROM * 2197^-1 and uses the scale constant 304 = 3303 * 2197^-2.
#ifndef NTT_REF_H
#define NTT_REF_H

#include <stdint.h>

#define NTT_N 256
#define NTT_Q 3329
#define NTT_SCALE 3303        // 128^-1 mod q
#define NTT_KM 2197           // factor every chip product carries
#define NTT_KM_INV 944        // 2197^-1 mod q
#define NTT_SCALE_C 304       // chip scale constant

extern uint16_t ntt_zeta[128];     // ROM[k] = 17^BitRev7(k) mod q
extern uint16_t ntt_gamma[128];    // gamma_i = 17^(2 BitRev7(i) + 1) mod q
extern uint16_t ntt_zeta_s[128];   // ROM' = ROM * 2197^-1 (what zeta_rom.v stores)
extern uint16_t ntt_gamma_s[128];  // gamma' = gamma * 2197^-1

void ntt_ref_init(void);           // fills the tables; call once

// plain model (in place where it has one argument)
void ref_ntt_layer(uint16_t f[NTT_N], int layer);
void ref_intt_layer(uint16_t f[NTT_N], int layer);      // without the final scale
void ref_ntt(uint16_t f[NTT_N]);
void ref_intt(uint16_t f[NTT_N]);
void ref_basemul(const uint16_t f[NTT_N], const uint16_t g[NTT_N], uint16_t h[NTT_N]);
void ref_scale(uint16_t f[NTT_N]);
void ref_schoolbook(const uint16_t f[NTT_N], const uint16_t g[NTT_N], uint16_t h[NTT_N]);

// chip-level model
uint16_t chip_mul(uint16_t a, uint16_t b);               // 2197 * a * b mod q, bit-exact K-RED
void chip_ntt_layer(uint16_t f[NTT_N], int layer, int inverse);
void chip_basemul(const uint16_t f[NTT_N], const uint16_t g[NTT_N], uint16_t h[NTT_N]);
void chip_scale(uint16_t f[NTT_N]);

// ByteEncode12 of one pair, and back
void pack_pair(uint16_t r0, uint16_t r1, uint8_t b[3]);
void unpack_pair(const uint8_t b[3], uint16_t *r0, uint16_t *r1);

// The known answers of Spec 1 and test/ntt_ref.py self_check(). Returns 0, or the number of the
// first failing check (and its description in *what).
int ntt_ref_selfcheck(const char **what);

#endif
