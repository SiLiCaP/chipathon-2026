// SPDX-License-Identifier: Apache-2.0
// CI emulator check. See ci_app.h and docs/ci-emulator.md.
#include "ci_app.h"

#include <stdio.h>
#include <string.h>

#include "ci_vectors.h"
#include "hash_app.h"

#ifndef CI_CONTRACT
#error "ci_vectors.h must define CI_CONTRACT"
#endif

#define CI_OUT_MAX 4096

int app_ci(hash_chip_t *c) {
    if (CI_NJOB == 0) { printf("ci: NOVECTORS\n"); return 0; }
    printf("CI-BEGIN contract=%d vec=%08lx jobs=%d\n", CI_CONTRACT, (unsigned long)CI_VEC_HASH, CI_NJOB);
    static uint8_t got[CI_OUT_MAX], want[CI_OUT_MAX];
    int fail = 0, done = 0;
    for (int j = 0; j < CI_NJOB; j++) {
        // the ML-KEM hash sequence in the order the algorithm makes the calls: one chip job per hash call
        const hash_kind_t kind = (hash_kind_t)ci_job[j].kind;
        const uint32_t outlen = ci_job[j].outlen;
        int err = outlen > CI_OUT_MAX || (unsigned)kind >= HASH_NKIND ? HASH_ERR_ARG
                  : hash_chip_job(c, kind, ci_data + ci_job[j].off, ci_job[j].len, got, outlen, 0);
        if (err) {                      // marked per job, so the host reports a chip fault, not truncation
            printf("ci: job %d: chip error %s\n", j, hash_err_str(err));
            printf("CI-OUT %d ERR\n", j);
            fail++; done++;
            continue;
        }
        hash_ref(kind, ci_data + ci_job[j].off, ci_job[j].len, want, outlen);
        if (memcmp(got, want, outlen)) fail++;
        printf("CI-OUT %d ", j);
        for (uint32_t i = 0; i < outlen; i++) printf("%02x", got[i]);
        printf("\n");
        done++;
    }
    printf("CI-END n=%d fail=%d\n", done, fail);
    printf("ci: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
