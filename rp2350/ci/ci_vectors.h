// SPDX-License-Identifier: Apache-2.0
// Stub: no per-commit vectors. CI puts a generated ci_vectors.h earlier on the include path
// (CI_VECTORS_DIR in CMake, CI_INC in rp2350/pc/Makefile); the firmware then prints "ci: NOVECTORS".
#ifndef CI_VECTORS_H
#define CI_VECTORS_H
#include <stdint.h>
#define CI_CONTRACT 2
#define CI_VEC_HASH 0u
#define CI_NJOB 0
#define CI_NBYTES 0
static const uint8_t ci_data[1] = {0};
static const struct { uint8_t kind; uint16_t len, outlen; uint32_t off; } ci_job[1] = {{0, 0, 0, 0}};
#endif
