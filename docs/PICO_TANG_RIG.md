# The Pico 2 W + Tang Nano 20K rig: the closest thing to the chip before silicon

This rig replaces the Tiny Tapeout chip with the Tang Nano 20K, and the TT demo board's RP2350 with a
Raspberry Pi Pico 2 W. Both are connected by jumper wires, pin for pin:

- **The FPGA runs the chip's hardened sky130 gate netlist** (`tt_um_NTT_Core`, the gates that go to the fab).
  Its inputs and outputs are wired straight to the header pins.
- **The Pico runs the team's RP2350 firmware** (`rp2350/` in this repo), built for this rig. It has the same
  PIO lockstep program, DMA, harness and tests that will drive the real chip on the demo board.

So the whole system runs as it will on silicon, on the same microcontroller, with real wires and a real clock
between the two. Only the chip itself is replaced by its gate-level copy. That makes this rig the source of
the **measured** RP2350 costs behind the silicon predictions. See [ESTIMATES.md](ESTIMATES.md).

**The precise timing simulation only works with both boards connected and wired.** The Tang Nano alone (the
USB setup in [FPGA_DEMO.md](FPGA_DEMO.md)) checks every result byte, but its timing is USB. The Pico alone
(`sw` mode) has no chip.

> Status (Oct 18, 2026): the code is ready but **untested on hardware**. Neither board was connected while
> it was written. Checked so far:
> - the rig's FPGA top level, in a cocotb simulation driven exactly like the PIO, with every byte correct;
> - both bitstreams build (gate netlist and RTL);
> - the firmware builds for both targets without warnings;
> - the timing-log fitter, against a synthetic log.

## What you need

- Raspberry Pi Pico 2 W (RP2350A) with header pins
- Tang Nano 20K with header pins
- 29 female-to-female jumper wires, as short as possible (10–15 cm is fine): 26 signals + 3 grounds
- Two USB cables. Both boards stay plugged into the PC: the Pico for its firmware and the USB serial
  console, the Tang Nano for its bitstream.

## Safety rules

1. **Connect grounds only, never power.** Each board is powered by its own USB cable. Don't link 3V3, 5V, VBUS
   or VSYS between them.
2. **Both sides are 3.3 V.** Tang Nano 20K header banks are 3.3 V (Sipeed pin label), and so is the Pico 2 W.
3. **Load the matching firmware before wiring, or wire with both unpowered.**
   - The FPGA drives 8 pins (`uo_out`) toward the Pico; the Pico drives the other 18.
   - The UART bitstream (`make -C fpga prog` without `TARGET=pico`) leaves all header pins unused, so it is safe.
   - The demo-board firmware (`tt_ntt_dbv3.uf2`) refuses to touch any pin on a Pico 2 W: it checks the
     package at boot. The rig firmware does the same on a demo board.
4. **Leave the TF card slot and the HDMI connector empty.** Some of the wires share pins with them (see the notes).
5. **Optional protection:** 220–330 Ω in series on the 8 `uo_out` wires limits the current if both sides ever
   drive the same wire. At rig speeds it costs nothing.

## Wiring

Read the Tang Nano from the top, USB-C connector up:
- **Left header:** L1 (top) to L20 (bottom); **right header:** R1 to R20.
- **Pico pin numbers** are the physical header pins: pin 1 = GP0, top left with the USB connector up.

→ = the Pico drives, ← = the FPGA drives.

| Chip signal | Pico 2 W | Pico pin | → / ← | Tang Nano header | FPGA pin | Note |
|---|---|---|---|---|---|---|
| `ui_in[0]` | GP0 | 1 | → | L1 | 73 |  |
| `ui_in[1]` | GP1 | 2 | → | L2 | 74 |  |
| `ui_in[2]` | GP2 | 4 | → | L3 | 75 |  |
| `ui_in[3]` | GP3 | 5 | → | L4 | 85 | SDIO_D1 (leave the TF slot empty) |
| `ui_in[4]` | GP4 | 6 | → | L8 | 27 |  |
| `ui_in[5]` | GP5 | 7 | → | L9 | 28 |  |
| `ui_in[6]` | GP6 | 9 | → | L10 | 25 |  |
| `ui_in[7]` | GP7 | 10 | → | L11 | 26 |  |
| `uio[0]` | GP8 | 11 | → | L12 | 29 | coefficient bit 8 |
| `uio[1]` | GP9 | 12 | → | L13 | 30 | coefficient bit 9 |
| `uio[2]` | GP10 | 14 | → | L14 | 31 | coefficient bit 10 |
| `uio[3]` | GP11 | 15 | → | R3 | 76 | coefficient bit 11 |
| `uio[4]` | GP12 | 16 | → | R4 | 80 | valid; SDIO_D2 |
| `uo_out[0]` | GP13 | 17 | ← | R8 | 54 | I2S_DIN of the unused amplifier |
| `uo_out[1]` | GP14 | 19 | ← | R10 | 48 |  |
| `uo_out[2]` | GP15 | 20 | ← | R11 | 55 | I2S_LRCK of the unused amplifier |
| `uo_out[3]` | GP16 | 21 | ← | R12 | 49 |  |
| `uo_out[4]` | GP17 | 22 | ← | R13 | 86 |  |
| `uo_out[5]` | GP18 | 24 | ← | R17 | 72 |  |
| `uo_out[6]` | GP19 | 25 | ← | R18 | 71 |  |
| `uo_out[7]` | GP20 | 26 | ← | R19 | 53 | EDID_CLK (no HDMI cable) |
| `clk` | GP21 | 27 | → | L5 | 77 | GCLKT_1: global clock input |
| `rst_n` | GP22 | 29 | → | R20 | 52 | EDID_DAT (no HDMI cable) |
| `uio[5]` | GP26 | 31 | → | R5 | 42 | sof |
| `uio[6]` | GP27 | 32 | → | R6 | 41 | op bit 0 |
| `uio[7]` | GP28 | 34 | → | R7 | 56 | op bit 1; I2S_BCLK of the unused amplifier |
| GND | GND | 3 | – | L20 | GND | |
| GND | GND | 18 | – | R15 | GND | |
| GND | GND | 28 | – | R2 | GND | |

This table, `fpga/tangnano20k_pico.cst` and the firmware's pin map come from one source,
`fpga/pico_rig_pins.py`. Its self-check refuses any table that doesn't match the firmware's PIO windows.
Free on purpose:
- Tang L6, L7 and L15–L18: the six LEDs, used as status lights.
- R9 (amplifier enable) and R14 (the RGB LED's data line).

### Why these pins

- **The Pico 2 W has exactly 26 usable GPIOs** (0–22 and 26–28; 23–25 and 29 belong to its wireless chip), and
  the chip has exactly 26 signals. There is no spare pin, so the onboard LED, which sits on the wireless chip,
  isn't used.
- **The PIO needs the 16 chip inputs as one window and the 8 outputs as another.**
  - GPIO 0–22 holds only 23 pins, so the input window starts at GPIO 26 and wraps around at 32:
    26, 27, 28, (29, 30, 31 unused), 0–12.
  - The firmware spreads each 16-bit pin word over those 19 bits. That costs the CPU about 3 cycles per word;
    the log records it as `conv` and the predictions drop it, since the demo board doesn't need it.
- **The clock goes to the FPGA's global clock input** (GCLKT_1, pin 77), so it reaches every flip-flop with low
  skew, as on the chip.

### Wiring for clean signals

The wires are 2–4× longer than the demo board's traces. That's fine at the rig's clock rates, with care:
- **Twist the clock wire with a ground wire,** or run them side by side. A clock that rings across the
  threshold counts twice, and the known answers fail at once.
- **Bundle the wires and keep them short.**
- The rig firmware already drives the pins with slow edges and 4 mA, which rings less than the demo board's
  fast setting.
- **If `sweep` fails at clocks where it should pass,** put 33–47 Ω in series with the clock wire at the Pico end.

## Software

### FPGA (Tang Nano 20K)

```bash
cd ~/Documents/Projects/Chipathon2026/tt-viktor-chiptor
source ~/tools/oss-cad-suite/environment
make -C fpga netlist                 # once per design change: harden the team chip with the TT flow (~15 min)
make -C fpga prog TARGET=pico        # its gate netlist, wired to the headers (GL=0: the RTL instead)
```

The FPGA's LEDs (active low):

| LED | Meaning |
|---|---|
| LED0 | heartbeat: the bitstream is loaded |
| LED1 | blinks while the chip clock runs |
| LED2 | the chip is in reset (`rst_n` low) |
| LED3 | valid inputs are arriving |
| LED4 | result bytes are leaving |
| LED5 | the design drives a `uio` pin, which this chip never does: something is wrong |

### Pico 2 W

```bash
cd <this repo>
export PICO_SDK_PATH=~/tools/pico-sdk PICO_TOOLCHAIN_PATH=~/tools/arm-gnu-toolchain
export PATH=~/tools/cmake-venv/bin:~/tools/arm-gnu-toolchain/bin:$PATH
cmake -S rp2350 -B rp2350/build_pico2w_tang -G Ninja -DTT_TARGET=pico2w_tang \
      -Dpioasm_DIR=$HOME/tools/pico-tools/pioasm -Dpicotool_DIR=$HOME/tools/pico-tools/picotool
cmake --build rp2350/build_pico2w_tang      # -> tt_ntt_pico2w_tang.uf2
```

The UF2 is also built by the team repo's CI (workflow `rp2350`, artifact `tt_ntt-firmware`). To flash it, hold
BOOTSEL while plugging the Pico in and copy the UF2 onto the drive that appears, or run
`~/tools/pico-tools/picotool/picotool load -x tt_ntt_pico2w_tang.uf2 -f`. The Pico then shows up as
`/dev/ttyACM0`.

## Wiring check (before running the chip)

`fpga/rig_pinchk.v` is a stand-in "chip" with the same pin directions. On each clock it latches one input byte,
alternating `ui_in` and `uio`, onto `uo_out`. The check drives it through the Pico's firmware (TTE1 over USB)
with walking ones and zeros, and names any open, shorted or swapped wire by its jumper:

```bash
sudo cp fpga/70-raspberrypi-pico.rules /etc/udev/rules.d/ && sudo udevadm control --reload && sudo udevadm trigger   # once
make -C fpga prog TARGET=pico GL=0 TT_TOP=tt_um_rig_pinchk EXTRA_SRCS=$PWD/fpga/rig_pinchk.v
python3 tools/rig_pincheck.py --port /dev/ttyACM0 --mhz 1 5 13.5
```

Expect `all 26 wires OK` at every clock, then PASS. A line like
`BAD uio[5] Pico GP26 (pin 31) <-> Tang R5 (FPGA pin 42)` names the jumper to fix.
Measured on Oct 18, 2026: all 26 wires OK at 0.96, 4.5 and 13.5 MHz.

## The hash-only chip on the rig (branch `sha3-only`)

The NTT-free chip (Keccak K = 8 + burst squeeze + SamplePolyCBD, `docs/TILE_ROUTES.md` section 7). Its ring
core is built with enable flops instead of clock gates (`CLOCK_GATING 0`), which the core test shows equivalent:

```bash
git worktree add ~/tools/work/br/sha3-only sha3-only        # once
make -C fpga prog TARGET=pico GL=0 DESIGN_ROOT=~/tools/work/br/sha3-only BUILD_TAG=_hashonly_k8 \
     CHPARAM='chparam -set CLOCK_GATING 0 keccak_ring;'
python3 host/test_variant.py --port /dev/ttyACM0 --hash 8 --hash-only
```

It runs 42 jobs: 24 byte-squeeze (every algorithm at the rate edges), 12 bursts, and 6 ML-KEM PRF calls
through Burst CBD (η = 2 and 3). Each is checked against hashlib or FIPS 203 SamplePolyCBD, and `uo_out`
must stay 0 on every clock where no byte is due. Through the Tang Nano alone (USB build, `--port /dev/ttyUSB1`) the
same 42 jobs pass too. Rig result: **42/42 bit-exact at 1.04, 3.38, 6.75 and 13.5 MHz**, the top of the rig's clock range, and `uo_out` is 0 on every clock where no byte is due. All 48,676 clocks take 1.9 s through the TTE1 bridge.

### The SHA-3 demo website

With the hash-only chip loaded (above), `python3 host/sha3_demo.py` serves http://localhost:8080. It uses the
Pico on /dev/ttyACM0 when present, or `--port /dev/ttyUSB1` for the Tang Nano alone, or `--sim` for a
pin-level Python model of the chip. If the FPGA doesn't answer like the hash chip (the Tang loses its image
when power-cycled), the script loads it itself with openFPGALoader, building the bitstream first if needed
(about 2 minutes). `--flash` forces a reload, `--no-flash` never touches the FPGA. `--check` runs the 42-job
self-test and one key exchange, then exits. It runs from a plain shell: without pyserial it restarts itself
under the OSS CAD Suite's Python.

- **ML-KEM-512 map:** the block diagram of KeyGen, Encaps and Decaps. Each block is marked chip or RP2350,
  lights up while it runs (pick a slow pace), and opens its hash calls when clicked. A timeline shows the
  exchange on the Tiny Tapeout board (model: 3.18 ms) or as measured on the rig, with a replay.
- **The rig:** every hash call's LOAD / RUN / READ_CAP bytes and times, animated.
- **The chip / Hash on the chip / Performance:** pinout and commands, any text through any of the four
  functions (checked against hashlib), and the route model's numbers.

Measured on Oct 18, 2026: one exchange is 32 hash calls and about 42,900 chip clocks. It takes 1.6 s on the
rig, with the keys identical to pure-software FIPS 203 and every call equal to hashlib.

## Bring-up

Open the console with `python3 -m serial.tools.miniterm /dev/ttyACM0` and type:

| Step | Command | Expect |
|---|---|---|
| 1 | `status` | "Pico 2 W + Tang Nano 20K rig", chip clock 1 MHz. FPGA LED0 blinks, LED2 goes off after the reset |
| 2 | `selftest` | every line ok, at 1 MHz |
| 3 | `sweep` | the known answers at 1–75 MHz, skew 0–2. Expect skew 0 at low clocks; the wires set the top end |
| 4 | `clk <hz>` | a clock that passes `sweep` with margin on both sides, then `cal` |
| 5 | `all` | selftest, demo, fuzz, matvec: PASS |
| 6 | `bench 20` | time per call, where it goes, predicted at 50 MHz |

Then, on the PC, with the console closed:

```bash
cd ~/Documents/Projects/Chipathon2026/tt-viktor-chiptor
source ~/tools/oss-cad-suite/environment
python3 tools/rp_log.py capture --port /dev/ttyACM0 --n 20    # benchmark, log, fit, save perf/rp_coeffs.json
python3 tools/rp_log.py show                                  # the firmware costs now marked "measured"
python3 sim/arch_explore.py                                   # every prediction updates from the measured costs
```

The PC tools written for the Tang Nano alone also run through the Pico, with the FPGA behind it: the full
demo-board path, PC → RP2350 → wires → chip gates:
- `python3 host/test_ntt.py --port /dev/ttyACM0 --seeds 3` runs 3 ML-KEM-512 exchanges, every byte checked.
- `python3 host/showcase.py --port /dev/ttyACM0 --form "gate netlist via Pico 2 W"` runs the demo website.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `status` says WRONG BOARD | the demo-board UF2 is on the Pico: flash `tt_ntt_pico2w_tang.uf2` |
| LED1 never blinks during `selftest` | clock wire (GP21 → L5) |
| LED2 stays on | reset wire (GP22 → R20), or the FPGA has the UART bitstream |
| `selftest` fails at 1 MHz | a swapped or missing data wire. Run `fuzz`: zeros, all-3328 and single-one vectors point to the bit |
| passes at low clocks, fails higher | wire length, ringing: shorten, twist clock with ground, series resistor on the clock |
| LED5 on | the FPGA thinks it should drive `uio`: wrong bitstream or design |
