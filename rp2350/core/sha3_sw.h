// SPDX-License-Identifier: Apache-2.0
// SHA-3 / SHAKE in software (FIPS 202) and SamplePolyCBD (FIPS 203), the RP2350-alone reference.
#pragma once
#include <stdint.h>

void keccak_f1600(uint64_t st[25]);
// rate in bytes (SHA3-256 136, SHA3-512 72, SHAKE128 168, SHAKE256 136); dom 0x06 (SHA-3) or 0x1F (SHAKE)
void sha3_sponge(unsigned rate, uint8_t dom, const uint8_t *msg, uint32_t len, uint8_t *out, uint32_t outlen);
// 64 * eta PRF bytes -> 256 coefficients packed as the hash chip's BURST_CBD output
void cbd_pack(const uint8_t *buf, int eta, uint8_t *out);
