# The FPGA emulator check is part of CI

**Requirement.** Every push to this repo is checked on the FPGA emulator (the Tang Nano 20K running the hardened
gate-level netlist of the chip, driven by the Pico 2 W running `rp2350/`). The result is the commit status
**`host-ci/rig`**. The check is not optional: **`fpga/` and `rp2350/` are required parts of this repo** and the
emulator is built from them, so any change to the chip's pins or command protocol (`src/`, `docs/info.md`) must
update `rp2350/core/hash_chip.c`, `rp2350/core/hash_swchip.c`, `fpga/` and this file **in the same push**. A push
that leaves them out of step fails `host-ci/rig`.

The check's workload is the chip's job in ML-KEM itself, with per-commit data, so a stale or hard-wired answer
cannot pass: **the validation sequence is the ML-KEM calculation sequence.**

## What is checked (contract 2: the hash sequence)

The chip is the hash-only chip: Keccak ring (K = 8) + `hash_io` adapter + on-chip SamplePolyCBD. In ML-KEM-512
(FIPS 203) it does every hash call: G = SHA3-512, H = SHA3-256, J and PRF = SHAKE256, XOF = SHAKE128 (the matrix A),
and, for the PRF, the SamplePolyCBD that follows it. Keygen, encaps and decaps make 32 such calls (10 + 11 + 11).

## Pipeline (services/chipci/rig in d0b0-domain)

1. commit SHA -> seed = `sha256(sha)`; `d`, `z`, `m` are drawn from `random.Random(seed)`.
2. The host runs ML-KEM-512 keygen / encaps / decaps (viktor `model/mlkem.py`) and records **every hash call** in call
   order: its kind, input bytes and the number of output bytes the model consumes (XOF: the 840 bytes SampleNTT
   asks for). A PRF call is recorded as one job of its own kind "SHAKE256 then SamplePolyCBD" (kinds 4/5).
3. `ci_vectors.h` is written (format below) and compiled into the firmware (`-DCI_VECTORS_DIR=<dir>`); the PC
   model is built the same way (`CI_INC=-I<dir>`) and must pass first.
4. The rig loads the netlist and the firmware; the console command `ci` runs the jobs on the chip in that order.
5. The host compares every output with `hashlib` (the PRF jobs with FIPS 203 SamplePolyCBD), then **replays ML-KEM
   with the chip's outputs in place of its own hashing** and requires ek, ct and K to equal the reference run.
   Match = pass (`rig.json` `detail.ci`).

## Contract (`CI_CONTRACT 2`)

`ci_vectors.h` (include dir order: generated dir, then the committed stub `rp2350/ci/`):

```c
#define CI_CONTRACT 2
#define CI_VEC_HASH 0x........u          // hash of all jobs, echoed back
#define CI_NJOB n    #define CI_NBYTES b
static const uint8_t ci_data[CI_NBYTES + 1];     // the jobs' input bytes, back to back
static const struct { uint8_t kind; uint16_t len, outlen; uint32_t off; } ci_job[CI_NJOB];
        // kind 0 SHA3-256, 1 SHA3-512, 2 SHAKE128, 3 SHAKE256, 4 PRF eta 2 + CBD, 5 PRF eta 3 + CBD
        // input = ci_data[off .. off+len), output = outlen bytes (kinds 4/5: 128 / 192 packed coefficient bytes)
```

Kinds 0-3 are bare hashes; the output is the digest or the first `outlen` bytes of the XOF stream. Kinds 4 and 5 run
SHAKE256(`seed || nonce`) through the chip's BURST_CBD command (eta = 2: `{f[2i+1], f[2i]}` per byte; eta = 3:
`{0,f0} {0,f1} {f3,f2}` per three bytes, 4-bit two's complement, see `docs/info.md`).

`app_ci` (`rp2350/core/ci_app.c`) runs one chip job per hash call through `hash_chip_job` (`rp2350/core/hash_chip.c`:
CLEAR, absorb the padded message block by block, PERMUTE and wait the busy clocks, squeeze whole lanes with BURST /
BURST_CBD, exactly the pin words of `test/hash_jobs.py`), and cross-checks each result against the firmware's own
software SHA-3 (`sha3_sw.c`). Console output:

```
CI-BEGIN contract=2 vec=<8 hex> jobs=<n>
CI-OUT <j> <2 * outlen hex chars>  the chip's output bytes of job j
CI-OUT <j> ERR                     the chip/transport returned an error on job j (a functional failure)
CI-END n=<n> fail=<k>
ci: PASS | FAIL | NOVECTORS
```

The committed stub has `CI_NJOB 0` and prints `ci: NOVECTORS`.

The console `selftest` (and `all` = selftest + fuzz) is the hash chip's known-answer and sweep test: SHA3-256/512,
SHAKE128/256 at the rate edges with byte and burst squeeze, with and without SKIP, and PRF + CBD for eta 2 and 3,
against the software reference; it prints `selftest: PASS`.

## Commands outside the contract (Nov 23, 2026)

The chip has more commands (`docs/info.md`): Absorb N, Finish, Mode, Burst NTT, and bursts whose next blocks
follow by themselves. Every command word the
contract-2 jobs send is unchanged, so `CI_CONTRACT` stays 2 and the golden vectors are not regenerated.

| Command | `hash_chip.c` | `hash_swchip.c` | Checked by |
|---|---|---|---|
| Absorb N, Finish | job options `HASH_RAW`, `HASH_FINISH` | modelled, clock for clock | `selftest`, `fuzz`, and their pin stream through the RTL (`make -C test rp2350`) |
| Blocks that follow by themselves (Burst x, `auto`) | job option `HASH_AUTO` | modelled, clock for clock | the same |
| Mode 1 (`uio[0]` = valid out, `uio[1]` = busy out, packed eta = 3) | `hash_chip_set_mode`; jobs then check the valid pin on every clock | modelled, both pins clock for clock | the same; the replay also compares `uio[1:0]` on every clock |
| Burst NTT | job kind 6, `HASH_XOF_NTT` (384 bytes, mode 1 only); with `HASH_AUTO` it runs until busy falls | modelled | the same |

Mode 1 needs a transport that samples `uio[1:0]` next to `uo_out` and can stop driving them
(`tt_io.h`: the `aux` samples and `uio_release`):
- **RP2350** (`rp/tt_rp.c`): in mode 1 the PIO's IN window is widened to take `uio[1:0]` with `uo_out` (16-bit
  samples) and the two pins become inputs. In mode 0 the transport is exactly as before (8-bit samples).
- **Tang Nano over USB** (`pc/pc_main.c`): the emulator's capture already holds `uio_out` and `uio_oe`.
- **No fight on the wires:** `hash_chip_set_mode` plays the Mode word and two idle clocks while the host still
  drives `uio[1:0]` low, which is what the chip drives there once it takes over; only then does the host let go.
  Back to mode 0 the chip lets go first. `fpga/tangnano20k_pico_top.v` drives `uio[1:0]` back in mode 1.

`selftest` and `fuzz` run their mode-1 part wherever the transport has it, so on the rig they exercise the real
pins. The `ci` jobs (contract 2) do not use the new commands.

## Sync rule

Any change to the chip's commands, framing or busy times must keep `hash_chip.c`, `hash_swchip.c`, `app_ci` and
`fpga/` working in the same push. `python3 rp2350/ci/check_contract.py` guards the plumbing (`app_ci`, the `ci`
command in `rp/main.c` and `pc/pc_main.c`, `CI_CONTRACT` in the stub and the golden header, the `fpga/` and `tools/`
files, this file); a rig mismatch fails `host-ci/rig`.

## Run it locally

```
make -C rp2350/pc test                 # selftest + fuzz against the software chip
make -C rp2350/pc ci-golden            # committed golden vectors (rp2350/ci/golden) on the software chip
python3 rp2350/ci/check_contract.py
make -C test rp2350                    # the C driver's pin stream through the RTL (iverilog + cocotb), vs the software chip
```

The PC build is plain C11 and also builds on macOS.
For a commit's own vectors: `services/chipci/rig/ci_vectors.py gen <sha> --viktor <dir> --out <dir>`, then
`make -C rp2350/pc CI_INC=-I<dir> && rp2350/pc/hash_host --sw ci`.

The golden header is `ci_vectors.py gen golden-fixture-v1` (fixed seed); regenerate it only if the contract changes.
