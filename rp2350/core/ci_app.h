// SPDX-License-Identifier: Apache-2.0
// CI emulator check (CI_CONTRACT 2, docs/ci-emulator.md): per-commit ML-KEM-512 hash jobs compiled in, run on the chip.
#ifndef CI_APP_H
#define CI_APP_H

#include "hash_chip.h"

// Runs every job of ci_vectors.h on the chip and prints CI-BEGIN / CI-OUT / CI-END / "ci: PASS|FAIL|NOVECTORS".
// Returns 0 on PASS or NOVECTORS, nonzero on FAIL (chip error or a job that disagrees with the C reference).
int app_ci(hash_chip_t *c);

#endif
