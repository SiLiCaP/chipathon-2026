// SPDX-License-Identifier: Apache-2.0
// CI emulator check. See ci_app.h and docs/ci-emulator.md.
#include "ci_app.h"

#include <stdio.h>
#include <string.h>

#include "ci_vectors.h"
#include "ntt_ref.h"

#ifndef CI_CONTRACT
#error "ci_vectors.h must define CI_CONTRACT"
#endif

int app_ci(ntt_chip_t *c) {
    if (CI_NJOB == 0) { printf("ci: NOVECTORS\n"); return 0; }
    printf("CI-BEGIN contract=%d vec=%08lx jobs=%d\n", CI_CONTRACT, (unsigned long)CI_VEC_HASH, CI_NJOB);
    static uint16_t ah[NTT_N], bh[NTT_N], p[NTT_N], t[NTT_N], ref[NTT_N], acc[NTT_N];
    int fail = 0, done = 0;
    for (int j = 0; j < CI_NJOB; j++) {
        // same sequence as app_matvec: forward both operands, basemul, sums on the CPU, inverse, scale
        memset(t, 0, sizeof t);
        memset(ref, 0, sizeof ref);
        int err = 0;
        for (int k = 0; k < ci_job[j].nterm && !err; k++) {
            const uint16_t *a = ci_poly[ci_job[j].a[k]], *b = ci_poly[ci_job[j].b[k]];
            err = ntt_chip_forward(c, a, ah) || ntt_chip_forward(c, b, bh) || ntt_chip_basemul(c, ah, bh, p);
            if (err) break;
            for (int i = 0; i < NTT_N; i++) t[i] = (uint16_t)((t[i] + p[i]) % NTT_Q);
            ref_schoolbook(a, b, acc);
            for (int i = 0; i < NTT_N; i++) ref[i] = (uint16_t)((ref[i] + acc[i]) % NTT_Q);
        }
        if (!err) err = ntt_chip_inverse(c, t, t) || ntt_chip_scale(c, t, t);
        if (err) {                      // marked per job, so the host reports a chip fault, not truncation
            printf("ci: job %d: chip error %s\n", j, ntt_err_str(err));
            printf("CI-OUT %d ERR\n", j);
            fail++; done++;
            continue;
        }
        if (memcmp(t, ref, sizeof t)) fail++;
        printf("CI-OUT %d ", j);
        for (int i = 0; i < NTT_N; i++) printf("%04x", t[i]);
        printf("\n");
        done++;
    }
    printf("CI-END n=%d fail=%d\n", done, fail);
    printf("ci: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
