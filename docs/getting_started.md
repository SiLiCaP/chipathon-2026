# Getting started: from a fresh checkout to the chip running ML-KEM's hashing

Two levels: simulation (no hardware) and the chip on the Tiny Tapeout demo board. Both check every result byte
against the C or Python reference, so a failure tells you where the problem is. Start with simulation even if you
have the chip.

| Level | Hardware | What it proves | Time |
|---|---|---|---|
| 1. Simulation | none | the RTL and the RP2350 driver are right | 10 min |
| 2. Silicon | Tiny Tapeout demo board v3 with our chip | the chip itself: self-test, wire speed, throughput | 15 min |

Everything needs no root except the udev rule. Paths below are relative to the repo root.

## 0. Tools

- **[OSS CAD Suite](https://github.com/YosysHQ/oss-cad-suite-build/releases)** (iverilog, cocotb): unpack to `~/tools/oss-cad-suite`, then `source ~/tools/oss-cad-suite/environment` in each
  shell. Its Python has cocotb; add `python3 -m pip install -r test/requirements.txt pyserial`.
- **For the RP2350 firmware:** pico-sdk 2.1.1, an Arm GCC (`arm-none-eabi-gcc`), cmake and ninja
  (`python3 -m venv ~/tools/cmake-venv && ~/tools/cmake-venv/bin/pip install cmake ninja`). Or download the UF2
  files from the latest `rp2350` workflow run on GitHub (artifact `tt_hash-firmware`).
- **USB access to the board's RP2350:** `sudo cp fpga/70-raspberrypi-pico.rules /etc/udev/rules.d/ && sudo udevadm control --reload && sudo udevadm trigger`,
  then replug (or add yourself to the `dialout` group).

## 1. Simulation

```bash
cd test && make                       # the chip's RTL: every SHA-3 / SHAKE, bursts, CBD, SampleNTT, padding, busy pin
cd .. && make -C rp2350/pc test       # the RP2350 driver (C) against its software chip: selftest + fuzz
make -C rp2350/pc ci-golden           # the committed ML-KEM-512 hash jobs through the same driver
./rp2350/pc/hash_host --sw bench      # the speed test's logic (times are meaningless on a PC)
```

`test/` is the TT testbench (`docs/info.md` lists what it checks). The C driver in `rp2350/core/` is the code that
runs on the real board; `make -C test rp2350` replays its exact pin stream through the RTL, clock for clock.

## 2. Silicon: the Tiny Tapeout demo board

The demo board v3 has an RP2350 that normally runs Tiny Tapeout's MicroPython. Our firmware replaces it (you can
go back at any time) and drives our project through PIO, one pin word per chip clock.

1. **Build or download** `tt_hash_dbv3.uf2` (`cmake -S rp2350 -B rp2350/build_dbv3 -G Ninja -DPICO_SDK_PATH=...`,
   `cmake --build rp2350/build_dbv3`). If you know our project's mux address, build with `-DTT_PROJECT_ADDR=<n>`.
2. **Flash it:** hold the board's BOOTSEL button while plugging in USB, copy the UF2 onto the drive that appears.
3. **Open the console:** `python3 -m serial.tools.miniterm /dev/ttyACM0` and type `help`.
4. **Bring-up**, in this order:

   | Command | Expect |
   |---|---|
   | `status` | "demo board", chip clock 1 MHz |
   | `select <n>` | our project on the mux (address from the shuttle's project list), then a reset |
   | `selftest` | every line ok. If not: `cal` (sample skew), then `selftest` again |
   | `fuzz 100` | PASS |
   | `speedtest` | the wire-speed table and the throughput (next section) |

5. **Run the speed test from the PC** and keep the results: `python3 rp2350/speedtest.py --select <n>`.
6. **Back to MicroPython:** flash the TT MicroPython UF2 from the
   [tt-micropython-firmware releases](https://github.com/TinyTapeout/tt-micropython-firmware/releases).

Command reference and how the PIO transport works: [rp2350/README.md](../rp2350/README.md).

## The speed test

`speedtest` (firmware command) or `rp2350/speedtest.py` (the same, saved as JSON and Markdown) measures two things.

**1. Wire speed: how fast this board's pins and our chip run together.** The silicon has other delays than the
FPGA: the TT mux, the pads, the clock tree. They can't be known before the chips exist, so the test measures them.
At every chip clock the RP2350 can make (1 MHz up to half its system clock: 75 MHz at 150 MHz) and at sample
skews 0 to 3, it runs six known-answer jobs, three times. The real board is expected to be faster than any
pre-silicon setup, so if even the fastest clock passes, `speedtest.py` raises the RP2350 to 200 and then 250 MHz
and sweeps again, up to 125 MHz chip clock (`--no-overclock` turns that off):
- the jobs use every input and output path: byte and burst squeeze, packets with on-chip padding, blocks that
  follow by themselves, CBD, and in framing mode 1 SampleNTT with the `valid` and `busy` pins;
- **sample skew** is how many clocks late the firmware reads `uo_out`. Output delay (pads, mux, clock insertion,
  up to about 10 ns on TT) makes bytes arrive a clock or more late at high clocks; a skew of 1 to 3 absorbs that.
  Input delay can't be absorbed, so if every skew fails above some clock, that is the input limit.

The highest clock that passes at any skew is the board's wire speed. The test then picks the highest clock whose
skew also passes one step lower (margin), and measures throughput there.

**2. Throughput at that clock.** The 32 hash calls of one ML-KEM-512 key exchange (G, H, J, 12 × XOF through
SampleNTT, 16 × PRF through CBD), timed with the RP2350's cycle counter:
- **with the chip:** the firmware as written, from building the pin words through PIO + DMA to unpacking;
- **software:** the same work in portable C on the same RP2350 (SHA-3, SamplePolyCBD, SampleNTT);
- **chip alone:** the chip's clocks at that clock, so the result can be projected to other clocks;
- plus absorb and squeeze rates in MB/s.

Every chip result is compared with the software one, so a "fast" result is always a correct one.

`bench compute` compares the computation only: each piece of C code against the chip's clocks for the same work.
`bench` alone drives the chip the fastest way known (framing mode 0, packets, on-chip padding, one burst word per
squeeze) and times the whole system. `bench mode1` and `bench legacy` drive the same workload two other ways for
comparison. `speedtest.py` runs all of them. Results so far: [speed_results.md](speed_results.md).

No extra silicon is needed for any of this: the chip has no speed-test logic. The test is all in the firmware.

## Before silicon (history)

Until the chips arrive, the same firmware was run against an FPGA copy of the chip: a Tang Nano 20K running the
chip's hardened gate netlist, wired pin for pin to a Pico 2 W, the same RP2350 as on the demo board. The owner's CI
server ran it on every push (`host-ci/rig`). The rig, its wiring and results: [PICO_TANG_RIG.md](PICO_TANG_RIG.md),
[ci-emulator.md](ci-emulator.md), [fpga/README.md](../fpga/README.md). Every command above (`selftest`, `speedtest`, ...)
works on it too, with `tt_hash_pico2w_tang.uf2`.

## Where to look next

| Topic | Doc |
|---|---|
| Pins, commands, timing (the datasheet) | [info.md](info.md) |
| CI: the owner's server | [host-ci.md](host-ci.md) |
| Measured speed so far | [speed_results.md](speed_results.md) |
| Why a hash chip, tile sizes, speed model | [tile_routes.md](tile_routes.md), [sha3_only_3x2.md](sha3_only_3x2.md), [onchip_offload.md](onchip_offload.md) |
