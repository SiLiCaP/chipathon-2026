# RP2350 harness and CI check for the hash-only chip

Firmware that drives the Keccak hash chip (`tt_um_NTT_Core`: Keccak ring K = 8 + `hash_io` adapter + on-chip
SamplePolyCBD, see `docs/info.md`) through PIO and DMA, runs its self test, and runs the **CI emulator check**
(`ci` command, [docs/ci-emulator.md](../docs/ci-emulator.md)): the ML-KEM-512 hash sequence of the commit, on the chip.
It builds for two test environments:
- **`dbv3`**: the Tiny Tapeout demo board v3 with the real chip.
- **`pico2w_tang`**: a Raspberry Pi Pico 2 W (the same RP2350) wired pin for pin to a Tang Nano 20K, which runs
  the chip's hardened gate netlist (`fpga/`, [docs/PICO_TANG_RIG.md](../docs/PICO_TANG_RIG.md)). The host CI uses this one.

The same C code also builds on a PC, where it runs against a software chip or against a Tang Nano over USB.
**`rp2350/` and `fpga/` are required parts of this repo:** a change to the chip's pins or commands must update
`core/hash_chip.c`, `core/hash_swchip.c` and `fpga/` in the same push (docs/ci-emulator.md).

```
rp2350/
  core/          portable C, used by both builds
    tt_io.h        the pin transport interface (16-bit pin words in, uo_out samples out) and TT_WORD
    hash_chip.c    the driver: builds the pin words of a sponge job exactly like test/hash_jobs.py (CLEAR, absorb,
                   PERMUTE + busy, squeeze by SQUEEZE or BURST, BURST_CBD for the PRF), picks the bytes out, checks
                   that uo_out is quiet when no byte is due
    hash_swchip.c  software chip: pin-level twin of hash_io.v + keccak_ring (K = 8) on sha3_sw.c's Keccak-f,
                   records protocol violations (a command while busy, SKIP off a lane, ...)
    hash_app.c     selftest, fuzz, calibrate
    ci_app.c       app_ci: the compiled-in ML-KEM hash jobs of ci/ci_vectors.h
    sha3_sw.c      FIPS 202 + SamplePolyCBD in software: the reference every chip result is checked against
  ci/            ci_vectors.h = the stub (CI_NJOB 0); golden/ = committed fixed-seed vectors; check_contract.py
  rp/            RP2350 only
    tt_lockstep.pio  one loop = one chip clock: input word out, uo_out sampled, clk via side-set
    tt_rp.h        the two targets' pin maps (TT_TARGET)
    tt_rp.c        pins, TT mux select, reset, PIO + DMA transport, chip clock settings, package check
    main.c         USB command line + the TTE1 binary protocol
  pc/            the PC build (pc_main.c, Makefile)
  boards/        board header for the demo board v3 (RP2350B); the rig uses the SDK's pico2_w
```

## Build

**PC harness**, any C compiler, no SDK:

```bash
make -C rp2350/pc test                               # selftest + fuzz against the software chip
make -C rp2350/pc ci-golden                          # the committed ML-KEM hash jobs on the software chip
rp2350/pc/hash_host --port /dev/ttyUSB1 all           # against a Tang Nano running the chip (see below)
```

**Firmware**, with pico-sdk 2.1.1 and an Arm GCC:

```bash
cmake -S rp2350 -B rp2350/build_dbv3 -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk [-DTT_PROJECT_ADDR=<n>]
cmake --build rp2350/build_dbv3                      # -> tt_hash_dbv3.uf2        (demo board, the default)
cmake -S rp2350 -B rp2350/build_pico2w_tang -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk -DTT_TARGET=pico2w_tang
cmake --build rp2350/build_pico2w_tang               # -> tt_hash_pico2w_tang.uf2  (Pico 2 W + Tang Nano rig)
```

Every push builds both on GitHub (workflow `rp2350`); the UF2 files are attached to the run as `tt_hash-firmware`.
Each firmware checks the RP2350 package at boot (QFN80 for the demo board, QFN60 for the Pico 2 W). On the wrong
board it leaves every pin alone, says so in `status`, and only offers the software chip.
`TT_PROJECT_ADDR` is our project's address on the shuttle's mux. Without it, select the project at run time
with `select <n>`. The address is in the shuttle index once the chips ship.

To flash it, put the board's RP2350 into BOOTSEL mode and copy the UF2 onto the drive that appears, or
use `picotool load -x <file>.uf2`. This replaces the Tiny Tapeout MicroPython firmware. To go back, flash
the TT MicroPython UF2 from the tt-micropython-firmware releases.

## Use

Open the USB serial port, for example `python3 -m serial.tools.miniterm /dev/ttyACM0`, and type `help`.

| Command | What it does |
|---|---|
| `status` | board, project, chip clock (divider and phases), counters, uo_out right now |
| `select <n>` | select project n on the TT mux (the TT SDK sequence), then reset the chip. On the rig: just a reset |
| `clk <hz>` | chip clock. Starts at 1 MHz |
| `sys <mhz>` | RP2350 clock: 150 by default, 200 gives 50 MHz with even phases |
| `selftest` | known answers, SHA-3 / SHAKE at the rate edges (byte + burst squeeze, +- SKIP), PRF + CBD, vs the C reference |
| `fuzz [n]` | n random jobs of every kind (random lengths, zero lanes, outputs) vs the C reference |
| `ci` | the CI emulator check: the compiled-in per-commit ML-KEM hash jobs (docs/ci-emulator.md); `ci: NOVECTORS` on the stub |
| `cal` | find the sample skew (see bring-up) |
| `sweep` | SHA3-256(abc) at 1-75 MHz, sample skews 0-2 |
| `sw` / `hw` | software chip / real chip. With `sw`, any RP2350 board runs everything without a TT chip |
| `all` | selftest, fuzz |

**TTE1 on the same port.** The firmware also speaks the binary protocol of the Tang Nano emulator: PING, RESET,
SET_IN, STEP, SET_DIV, LOAD, RUN, READ_CAP. Its opcodes are non-printable bytes, so they never clash with
typed commands. The PC tools written for the FPGA in
[tt-viktor-chiptor](https://github.com/SiLiCaP/tt-viktor-chiptor) then work on the real chip unchanged
(`host/test_variant.py --port /dev/ttyACM0 --hash 8 --hash-only`, `host/sha3_demo.py`).

## Bring-up on silicon

1. `select <n>`, then `status`. The chip clock is 1 MHz.
2. `selftest`. All lines should say ok. If they don't, try `cal`.
3. `cal` finds the **sample skew**. At high clocks, the pad and clock-insertion delays (up to about 10 ns
   on TT) can make results appear a clock later than at the pins of the design. The skew shifts where the
   driver reads them. It is 0 at low clocks and on the FPGA.
4. `sweep` runs a known answer from 1 to 75 MHz at skews 0-2. Pick a clock that passes with margin on
   both sides, set it with `clk`, then run `all`.

## How it works

- **Pins (demo board v3).** The TT MicroPython SDK, `gpio_map_dbv3.py`, maps them as below.

  | Signal | GPIO |
  |---|---|
  | `ctrl_ena`, `ctrl_sel_rst_n`, `ctrl_sel_inc` | 0, 1, 2 |
  | `rst_n` | 14 |
  | `clk` | 16 |
  | `ui_in[7:0]` | 17–24 |
  | `uio[7:0]` | 25–32 |
  | `uo_out[7:0]` | 33–40 |
  | LED | 11 |

  One 16-bit pin word `{uio, ui_in}` is exactly 16 consecutive GPIOs. The PIO runs with GPIO base 16, and
  the build refuses any board that isn't an RP2350B.
- **Pins (Pico 2 W rig).** The Pico 2 W has 26 usable GPIOs (0–22, 26–28) for the chip's 26 signals:

  | Signal | GPIO |
  |---|---|
  | `ui_in[7:0]` | 0–7 |
  | `uio[4:0]` | 8–12 |
  | `uio[7:5]` | 26–28 |
  | `uo_out[7:0]` | 13–20 |
  | `clk` | 21 |
  | `rst_n` | 22 |

  The PIO's 16 output pins must form one window, and GPIO 0–22 is too short. So the window starts at GPIO 26
  and wraps around at 32: 19 bits, of which 29–31 stay unconnected. The firmware spreads each word over it,
  about 3 cycles per word. The log records this as `conv` and the predictions drop it.
- **Lockstep PIO, two instructions.** Each loop is one chip clock:
  - `out pins, 16 side 0`: clk falls and the next input word goes on the pins.
  - `in pins, 8 side 1`: `uo_out` is sampled as clk rises, just before the chip captures the word. This is
    exactly the `docs/info.md` convention, so the 2-clock output delay of a squeezed byte applies unchanged.
- **DMA.** TX writes 16-bit words (32-bit on the rig), one per FIFO entry; RX reads 8-bit samples, one per entry. Clock counts are exact, with no padding.
- **Stalls are harmless.** If DMA ever falls behind, the PIO stalls with the clock held, low or high. A
  synchronous chip can't tell that from a slower clock, so no word is ever lost or repeated.
- **Clock.** The chip clock is `sys / (div × (low + high))`, with an integer divider (no jitter).
  - The phase lengths are patched into the instruction delay fields.
  - The longer phase is high: inputs change at the falling edge, so the high time is the chip's hold margin
    against its own clock insertion delay.
  - At 150 MHz: 75, 50 (1 + 2 cycles), 37.5, 30, 25 MHz and lower are exact.
- **Faults.** A nonzero `uo_out` where no byte is due resets the chip (rst_n low for 10 clocks) and returns an error.
  The DMA has a timeout that restarts the state machine.

## Verification

| Run | Result |
|---|---|
| PC, software chip: selftest, fuzz, golden ML-KEM hash jobs | PASS; 0 protocol violations |
| C driver's pin stream through the RTL (`make -C test rp2350`, iverilog + cocotb) | uo_out identical to the software chip on every clock |
| Firmware build (pico-sdk 2.1.1, Arm GCC) for `pico2w_tang` with the golden vectors | builds without warnings |
| **On the Pico 2 W + Tang Nano rig** | by the host CI (`host-ci/rig`) |
