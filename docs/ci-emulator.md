# The FPGA emulator check is part of CI

**Requirement.** Every push to this repo is checked on the FPGA emulator (the Tang Nano 20K running the hardened
gate-level netlist, driven by the Pico 2 W running `rp2350/`). The result is the commit status **`host-ci/rig`**.
The check is not optional: the RP2350 code in every push must support it, so a change that breaks it fails CI.

The check's workload is the ML-KEM calculation sequence itself, with per-commit data, so a stale or hard-wired
answer cannot pass.

## Pipeline (services/chipci/rig in d0b0-domain)

1. commit SHA -> seed = `sha256(sha)`; `d`, `z`, `m` are drawn from `random.Random(seed)`.
2. The host runs ML-KEM-512 keygen / encaps / decaps (viktor `model/mlkem.py`) and records every polynomial
   product the coprocessor would do (`sum_j a_j * b_j mod (X^256 + 1, q = 3329)`): 9 jobs.
3. `ci_vectors.h` is written (format below) and compiled into the firmware (`-DCI_VECTORS_DIR=<dir>`); the PC
   model is built the same way (`CI_INC=-I<dir>`) and must pass first.
4. The rig loads the netlist and the firmware; the console command `ci` runs every job on the chip.
5. The host compares every product with its own, then replays ML-KEM with the chip's products and requires
   ek, ct and K to equal the reference run. Match = pass (`rig.json` `detail.ci`).

## Contract (`CI_CONTRACT 1`)

The firmware does the ring work on the chip; the host does hashing, sampling and encoding.

`ci_vectors.h` (include dir order: generated dir, then the committed stub `rp2350/ci/`):

```c
#define CI_CONTRACT 1
#define CI_VEC_HASH 0x........u          // hash of all vectors, echoed back
#define CI_NPOLY n   #define CI_NJOB n
static const uint16_t ci_poly[CI_NPOLY][256];                       // plain-domain polynomials
static const struct {uint8_t nterm, a[4], b[4];} ci_job[CI_NJOB];   // P = sum_j ci_poly[a[j]] * ci_poly[b[j]]
```

`app_ci` (`rp2350/core/ci_app.c`) per job: forward NTT of both operands on the chip, basemul, CPU accumulate mod q,
inverse, scale (the `app_matvec` sequence, so the chip's K-RED factor handling is the same), and a cross-check
against the C schoolbook. Console output:

```
CI-BEGIN contract=1 vec=<8 hex> jobs=<n>
CI-OUT <j> <1024 hex chars>        256 coefficients, 4 hex digits each, big-endian
CI-OUT <j> ERR                     the chip returned an error on job j (a functional failure)
CI-END n=<n> fail=<k>
ci: PASS | FAIL | NOVECTORS
```

The committed stub has `CI_NJOB 0` and prints `ci: NOVECTORS`.

## Sync rule

Any change to the chip's operations, the framing, or the scale/K-RED factor must keep `app_ci` working in the same
push. `python3 rp2350/ci/check_contract.py` guards the plumbing (`app_ci`, the `ci` command in `rp/main.c` and
`pc/pc_main.c`, `CI_CONTRACT` in the stub, this file); a rig mismatch fails `host-ci/rig`.

## Run it locally

```
make -C rp2350/pc ci-golden            # committed golden vectors (rp2350/ci/golden) on the software chip
python3 rp2350/ci/check_contract.py
```

For a commit's own vectors: `services/chipci/rig/ci_vectors.py gen <sha> --viktor <dir> --out <dir>`, then
`make -C rp2350/pc CI_INC=-I<dir> && rp2350/pc/ntt_host --sw ci`.

The golden header is `ci_vectors.py gen golden-fixture-v1` (fixed seed); regenerate it only if the contract changes.
