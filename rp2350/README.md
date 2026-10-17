# RP2350 harness and demo (Spec 1)

Firmware for the Tiny Tapeout demo board v3 that drives `tt_um_NTT_Core` through PIO and DMA. It runs the
tests and the demo of Spec 1. The same C code also builds on a PC, where it runs against a software chip or
against a Tang Nano FPGA. That way it was tested on the chip's real logic before any silicon exists.

```
rp2350/
  core/          portable C, used by both builds
    timing.h       GENERATED from test/timing.py by gen_timing.py (the only place for delays and periods)
    ntt_ref.c      reference model, the C twin of test/ntt_ref.py, with all Spec 1 known answers
    ntt_chip.c     the harness: job building, layer waits, unpacking, range checks, clock counting
    swchip.c       software chip: pin-level stand-in with the same protocol and delays
    app.c          selftest, demo, fuzz, matvec (stretch goal), bench, calibrate
  rp/            RP2350 only
    tt_lockstep.pio  one loop = one chip clock: input word out, uo_out sampled, clk via side-set
    tt_rp.c        pins, TT mux select, reset, PIO + DMA transport, chip clock settings
    main.c         USB command line + the TTE1 binary protocol
  pc/            the PC build (pc_main.c, Makefile)
  boards/        board header for the demo board v3 (RP2350B)
```

## Build

**PC harness**, any C compiler, no SDK:

```bash
make -C rp2350/pc test                               # everything against the software chip
rp2350/pc/ntt_host --port /dev/ttyUSB1 all           # against a Tang Nano running the chip (see below)
```

**Firmware**, with pico-sdk 2.1.1 and an Arm GCC:

```bash
cmake -S rp2350 -B rp2350/build -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk [-DTT_PROJECT_ADDR=<n>]
cmake --build rp2350/build                           # -> rp2350/build/tt_ntt.uf2
```

Every push also builds it on GitHub (workflow `rp2350`). The UF2 is attached to the run as `tt_ntt-firmware`.
`TT_PROJECT_ADDR` is our project's address on the shuttle's mux. Without it, select the project at run time
with `select <n>`. The address is in the shuttle index once the chips ship.

To flash it, put the board's RP2350 into BOOTSEL mode and copy `tt_ntt.uf2` onto the drive that appears, or
use `picotool load -x tt_ntt.uf2`. This replaces the Tiny Tapeout MicroPython firmware. To go back, flash
the TT MicroPython UF2 from the tt-micropython-firmware releases.

## Use

Open the USB serial port, for example `python3 -m serial.tools.miniterm /dev/ttyACM0`, and type `help`.

| Command | What it does |
|---|---|
| `status` | board, project, chip clock (divider and phases), counters, uo_out right now |
| `select <n>` | select project n on the TT mux (the TT SDK sequence), then reset the chip |
| `clk <hz>` | chip clock. Starts at 1 MHz, as Spec 1 asks |
| `sys <mhz>` | RP2350 clock: 150 by default, 200 gives 50 MHz with even phases |
| `selftest` | the Spec 1 known answers, first on the reference model, then on the chip |
| `demo [seed]` | Spec 1 demo: f × g on the chip vs schoolbook, with clocks, estimate, chip time and wall time per step |
| `fuzz [n]` | all four modes against the chip-level model: zeros, all 3328, ramp, single ones, n random |
| `matvec [seed]` | stretch goal: t = A·s for k = 2, chip for every NTT and basemul, CPU for the sums |
| `bench [n]` | time per call |
| `cal` | find the sample skew (see bring-up) |
| `sweep` | known answers at 1–75 MHz, sample skews 0–2 |
| `sw` / `hw` | software chip / real chip. With `sw`, any RP2350 board runs everything without a TT chip |
| `all` | selftest, demo, fuzz, matvec |

**TTE1 on the same port.** The firmware also speaks the binary protocol of the Tang Nano emulator: PING, RESET,
SET_IN, STEP, SET_DIV, LOAD, RUN, READ_CAP. Its opcodes are non-printable bytes, so they never clash with
typed commands. The PC tools written for the FPGA in
[tt-viktor-chiptor](https://github.com/SiLiCaP/tt-viktor-chiptor) then work on the real chip unchanged:
- `host/test_ntt.py --port /dev/ttyACM0 --seeds 5` runs 5 full ML-KEM-512 exchanges, byte-checked.
- `host/showcase.py --port /dev/ttyACM0` runs the demo website.

## Bring-up on silicon

1. `select <n>`, then `status`. The chip clock is 1 MHz.
2. `selftest`. All lines should say ok. If they don't, try `cal`.
3. `cal` finds the **sample skew**. At high clocks, the pad and clock-insertion delays (up to about 10 ns
   on TT) can make results appear a clock later than at the pins of the design. The skew shifts where the
   harness reads them. It is 0 at low clocks and on the FPGA.
4. `sweep` runs the known answers from 1 to 75 MHz at skews 0–2. Pick a clock that passes with margin on
   both sides, set it with `clk`, then run `all`.
5. `demo`, then `bench`, for the numbers.

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
- **Lockstep PIO, two instructions.** Each loop is one chip clock:
  - `out pins, 16 side 0`: clk falls and the next input word goes on the pins.
  - `in pins, 8 side 1`: `uo_out` is sampled as clk rises, just before the chip captures the word. This is
    exactly the `docs/host_timing.md` convention, so D and every result position apply unchanged.
- **DMA.** TX writes 16-bit words, one per FIFO entry; RX reads 8-bit samples, one per entry. Clock counts
  are exact, with no padding: forward NTT 2,751, inverse 2,765, basemul 655, scale 394, demo 9,316.
- **Stalls are harmless.** If DMA ever falls behind, the PIO stalls with the clock held, low or high. A
  synchronous chip can't tell that from a slower clock, so no word is ever lost or repeated.
- **Clock.** The chip clock is `sys / (div × (low + high))`, with an integer divider (no jitter).
  - The phase lengths are patched into the instruction delay fields.
  - The longer phase is high: inputs change at the falling edge, so the high time is the chip's hold margin
    against its own clock insertion delay.
  - At 150 MHz: 75, 50 (1 + 2 cycles), 37.5, 30, 25 MHz and lower are exact.
- **Faults.** Any result ≥ 3329 resets the chip (rst_n low for 10 clocks) and returns an error, as Spec 1
  asks. The DMA has a timeout that restarts the state machine.
- **Timing constants.** All come from `core/timing.h`, generated from `test/timing.py`. CI fails if the two
  disagree.

## Verification so far (Oct 17, 2026)

| Run | Result |
|---|---|
| PC, software chip: selftest, demo, fuzz (18 vectors × 4 modes), matvec | PASS; clock counts equal host_timing.md; 0 protocol violations |
| PC + Tang Nano 20K, `tt_um_NTT_Core` **RTL** (`main` 91eb006) | PASS, same tests |
| PC + Tang Nano 20K, `tt_um_NTT_Core` **hardened sky130 gate netlist** | PASS, same tests; sample skew 0 |
| Firmware build (pico-sdk 2.1.1, Arm GCC 14.2) | builds without warnings, 50 kB code |
| **On an RP2350 demo board** | **not run yet**: no board here. The PIO/DMA transport, mux select and USB layer are untested on hardware |

On the Tang Nano the chip runs as its gate netlist: every sky130 standard cell is rebuilt from its logic
function in the cell library, so the FPGA runs the gates that go on the chip (tt-viktor-chiptor
`make -C fpga netlist prog`). On the FPGA the wall time is USB; the chip time is printed next to it.
