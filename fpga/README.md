> Imported from tt-viktor-chiptor @af81927 (Apache-2.0), now maintained here. The chip is this repo's own
> `info.yaml` + `src/`; `rp2350/` is the matching firmware; keep all three in step (docs/ci-emulator.md).

# Tang Nano 20K emulator

This runs our unmodified `tt_um_*` module from `src/` on a Sipeed Tang Nano 20K. A PC drives
it over USB-UART and plays the role of the RP2350 on the Tiny Tapeout demo board.
Nothing in this directory is listed in `info.yaml`.

```
PC (host/tt_emu.py) ──USB──> BL616 bridge ──UART──> tangnano20k_top.v
                                                     ├─ command processor (PROTOCOL.md)
                                                     ├─ stimulus RAM 8192×16, capture RAM 8192×24
                                                     ├─ DUT clock: 27 MHz / (2·div), max 13.5 MHz
                                                     └─ tt_um_* from src/ (sources + top read from info.yaml)
```

## Toolchain

**Linux (tested on Fedora 44):** use [OSS CAD Suite](https://github.com/YosysHQ/oss-cad-suite-build/releases).
It needs no root.
```bash
mkdir -p ~/tools && cd ~/tools
curl -LO https://github.com/YosysHQ/oss-cad-suite-build/releases/download/2026-10-01/oss-cad-suite-linux-x64-20261001.tgz
tar xzf oss-cad-suite-linux-x64-20261001.tgz
source ~/tools/oss-cad-suite/environment          # do this in every shell
python3 -m pip install -r test/requirements.txt pyserial kyber-py
```
The suite ships its own Python 3.11 (the same version CI uses) and its own cocotb. Use that Python.
A separate venv's cocotb **cannot** load inside the suite's `vvp`.

USB access without sudo: install the udev rule once, then replug the board.
```bash
sudo cp fpga/70-tangnano20k.rules /etc/udev/rules.d/ && sudo udevadm control --reload && sudo udevadm trigger
```
The rule's number must be below 73, because `73-seat-late.rules` is what applies `uaccess`.
The alternative is `sudo usermod -aG dialout $USER` followed by logging out and back in.

**Windows (untested):** use the Windows build of OSS CAD Suite (run `start.bat` or `environment.bat`).
openFPGALoader needs the JTAG interface (interface 0) switched to WinUSB with
[Zadig](https://zadig.akeo.ie/). Leave interface 1 on the FTDI VCP driver, because that one is the
UART (a COM port). Pass `--port COMx` to the host scripts.

## Build, load, test

```bash
make -C fpga            # bitstream -> fpga/build/top.fs (~20 s)
make -C fpga prog       # load into SRAM (lost on power cycle)
make -C fpga flash      # write to onboard flash. This replaces the factory LiteX demo
make -C fpga sim        # cocotb sim of wrapper + protocol (~1 min)
make -C fpga/sim TT_TOP=tt_um_emu_selftest     # sim with the sequential self-test DUT

python3 host/tt_emu.py --list
python3 host/tt_emu.py --ping                  # auto-detects the port
python3 host/test_example.py                   # 10k vectors, step mode + run mode at div 8,4,2,1
```
To check cycle accuracy on hardware, load the sequential self-test DUT:
```bash
make -C fpga clean && make -C fpga prog TT_TOP=tt_um_emu_selftest EXTRA_SRCS=$PWD/fpga/sim/tt_um_emu_selftest.v
python3 host/test_example.py --top tt_um_emu_selftest
```

### Verified on our board (2026-10-17)
- PING over `/dev/ttyUSB1` works.
- Template example design: 10,000 step-mode vectors pass, and 10,000 run-mode vectors pass at
  div 8, 4, 2 and 1 (1 = 13.5 MHz DUT clock).
- Self-test DUT (registered output and cycle counter): 20,000 vectors pass, cycle-exact, at div 8 and 1.
- A payload containing the BL616 escape bytes `18 03 0D` gets through unharmed.
- Resources with the example design: about 4% of LUTs, 20 of 46 BSRAM.

## Pinout used

| Signal | FPGA pin | Source |
|---|---|---|
| 27 MHz clock | 4 | Sipeed `uart/src/top.cst`, LiteX board file |
| UART RX (PC → FPGA) | 70 | same |
| UART TX (FPGA → PC) | 69 | same |
| LED0..5 (active low) | 15..20 | Sipeed `led/src/*.cst` |

LED0 is the heartbeat, LED1 means a command is busy, and LED2 means UART bytes arrived recently.
LED1 looks dim during tests because it flickers once per command.

## UART speed

The default is **1 Mbaud** (`BAUD` in the Makefile, `--baud` on the host scripts), which moves about
83 kB/s. On our board, 1.5 and 3 Mbaud dropped bytes during long readbacks, a BL616 limit.
A DUT clock run is not limited by the UART: it plays from the on-chip RAM at up to 13.5 MHz.

## Pointing it at a new `tt_um` top

Nothing to change: the Makefile reads `top_module` and `source_files` from `info.yaml`.
To override for one build, use `make -C fpga TT_TOP=tt_um_other EXTRA_SRCS=...`.
For host tests, add a cycle model to `host/dut_models.py`.

## Troubleshooting

- **The board enumerates as `0403:6010` "SIPEED USB Debugger".** Interface 0 (`ttyUSB0`) is JTAG
  and interface 1 (`ttyUSB1`) is the UART.
- **UART completely silent, in both directions, while JTAG still works.** We saw this once.
  The BL616 bridge was stuck, and **unplugging and replugging the board** fixed it.
  If the factory LiteX image answers `litex>` on Enter, the bridge is fine.
- **The BL616 has its own console on `ttyUSB1`.**
  - Ctrl-X, Ctrl-C, Enter opens it (`TangNano20K />` prompt).
  - `choose uart` returns to FPGA pass-through.
  - `uart_info` shows the baud rate the PC set.
- After `make prog`, `ttyUSB0` loses its ACL for a moment (openFPGALoader detaches the driver).
  `ttyUSB1` is not affected.
