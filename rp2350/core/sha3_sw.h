// SPDX-License-Identifier: Apache-2.0
// SHA-3 / SHAKE in software (FIPS 202) and SamplePolyCBD (FIPS 203), the RP2350-alone reference.
#pragma once
#include <stdint.h>

void keccak_f1600(uint64_t st[25]);
// rate in bytes (SHA3-256 136, SHA3-512 72, SHAKE128 168, SHAKE256 136); dom 0x06 (SHA-3) or 0x1F (SHAKE)
void sha3_sponge(unsigned rate, uint8_t dom, const uint8_t *msg, uint32_t len, uint8_t *out, uint32_t outlen);
// The fastest Keccak-f[1600] this build has, on a 200-byte state (for the benchmark), and its name: XKCP's ARMv7-M
// assembly on the RP2350 (SHA3_XKCP), else portable C
void sha3_sw_permute(void *state);
const char *sha3_sw_impl(void);
// 64 * eta PRF bytes -> 256 coefficients packed as the hash chip's BURST_CBD output
void cbd_pack(const uint8_t *buf, int eta, uint8_t *out);
// SampleNTT (FIPS 203 Alg. 7) on n XOF bytes, the polynomial as ByteEncode_12 (384 bytes), the hash chip's
// SampleNTT output. Returns 1, or 0 if the n bytes did not give 256 coefficients.
int sample_ntt_pack(const uint8_t *xof, uint32_t n, uint8_t *out);
