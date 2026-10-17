# Spec 5: I/O

Oct 17, 2026 · @A

## Your job

You own everything at the pins: turning the input pins into clean signals, packing results into bytes on the output pins, and the Tiny Tapeout top-level shell. You never decide when something happens. The Control owner does.

- **In scope:** input capture, the output packer, the `tt_um_*` shell, and the pin names in the Tiny Tapeout docs.
- **Out of scope:** counters, constants, arithmetic, job sequencing, and the host side of the pins.
- **Why it matters:** the pin protocol is the contract with the RP2350. A wrong bit order here breaks every mode at once.

The chip uses 24 user pins: 12 data in, 4 control in, 8 data out. All `uio` pins are inputs.

## Pin map

The chip uses 24 user pins plus the standard Tiny Tapeout clock and reset.

| Pin | Dir | Function |
| --- | --- | --- |
| `ui_in[7:0]` | in | Coefficient bits 7:0 |
| `uio[3:0]` | in | Coefficient bits 11:8 |
| `uio[4]` | in | valid: the data pins hold a real coefficient this clock |
| `uio[5]` | in | sof: with valid, the first coefficient of a job. Restarts all counters |
| `uio[7:6]` | in | op: 00 forward NTT, 01 inverse NTT, 10 basemul, 11 scale. Read only on the sof clock |
| `uo_out[7:0]` | out | Result byte at a fixed delay. No output-valid flag |
| `clk`, `rst_n` | in | Tiny Tapeout standard. Not counted in the 24 |

Rules for the shell:

- Tie `uio_oe` to `8'h00` and `uio_out` to `8'h00`. Every `uio` pin is an input.
- `ena` is not used.
- The host drives `clk` and the data pins together, so the inputs are synchronous to `clk`. No synchronizer chain.

## Input capture

One register stage turns the pins into clean signals that stay aligned with each other. Module name: `io_in_capture`.

| Port | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `clk`, `rst_n` | in | 1 each | Clock and synchronous reset |
| `ui_in` | in | 8 | From the pins |
| `uio_in` | in | 8 | From the pins |
| `coeff_q` | out | 12 | `{uio_in[3:0], ui_in[7:0]}`, registered |
| `valid_q` | out | 1 | `uio_in[4]`, registered |
| `sof_q` | out | 1 | `uio_in[5] & uio_in[4]`, registered. High only when valid and sof were both high |
| `op_q` | out | 2 | `uio_in[7:6]`, registered every clock. Control reads it only when `sof_q` is high |

Rules:

- Latency is exactly 1 clock. A pin value at clock t shows on the `_q` outputs at clock t + 1. Report this number to the Control owner.
- Do not gate `coeff_q` with `valid_q`. When valid is low the data is a don't-care.
- No other logic and no falling-edge sampling.

Pin example for the first coefficient of an inverse NTT job, value 1000 (0x3E8): `ui_in` = 0xE8 and `uio_in` = 0x73, which is op 01, sof 1, valid 1 and coefficient bits 11:8 = 0x3. A later coefficient, 2000 (0x7D0), with valid only: `ui_in` = 0xD0 and `uio_in` = 0x17.

## Output packer

The packer turns a pair of 12-bit results into three bytes on three consecutive clocks, at a fixed delay, with no valid flag. Module name: `io_out_pack`.

| Port | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `clk`, `rst_n` | in | 1 each | Clock and synchronous reset |
| `pair_valid` | in | 1 | High for one clock when `r0` and `r1` hold a result pair |
| `r0` | in | 12 | First result, 0..3328 |
| `r1` | in | 12 | Second result, 0..3328 |
| `flush` | in | 1 | Drops any bytes still to be sent. `uo_byte` is 0x00 from the next clock |
| `uo_byte` | out | 8 | Goes to `uo_out[7:0]` |

Byte order matches ML-KEM's ByteEncode12:

| Clock after `pair_valid` | `uo_byte` |
| --- | --- |
| +1 | `r0[7:0]` |
| +2 | `{r1[3:0], r0[11:8]}` |
| +3 | `r1[11:4]` |
| later | 0x00 until the next pair |

Register `r0` and `r1` on the `pair_valid` clock. Latency to the first byte is exactly 1 clock. Report it to the Control owner.

Examples:

| r0 | r1 | Bytes |
| --- | --- | --- |
| 0x123 | 0x456 | 0x23, 0x61, 0x45 |
| 1 | 2 | 0x01, 0x20, 0x00 |
| 3328 | 1 | 0x00, 0x1D, 0x00 |
| 3328 | 3328 | 0x00, 0x0D, 0xD0 |

I checked these against a bit-by-bit little-endian encoding of the two 12-bit values.

Timing contract: Control sends `pair_valid` at least 3 clocks apart. In the NTT modes it is exactly every 3 clocks, so the byte stream has no gaps. Add a simulation assertion that fails if two `pair_valid` pulses come closer than 3 clocks. In basemul and scale there are gaps, and `uo_byte` is 0x00 in them. Control does the pairing for scale, so you only ever see pairs.

## Tiny Tapeout shell and pin documentation

You write the top-level module and the pin docs. The shell wires your two blocks to the Control owner's `ntt_core`.

- **Module.** `tt_um_<project name>` with the Tiny Tapeout port list: `ui_in[7:0]`, `uo_out[7:0]`, `uio_in[7:0]`, `uio_out[7:0]`, `uio_oe[7:0]`, `ena`, `clk`, `rst_n`.
- **Wiring.** `ui_in` and `uio_in` go into `io_in_capture`. Its outputs `coeff_q`, `valid_q`, `sof_q` and `op_q` go to `ntt_core`. From `ntt_core`, `r0`, `r1`, `pair_valid` and `flush` go to `io_out_pack`, whose `uo_byte` drives `uo_out`.
- **Unused.** `uio_out` = 0, `uio_oe` = 0, and `ena` is left unused. Mark unused signals so lint stays clean.
- **Pin names.** Fill in the pinout section of the Tiny Tapeout template's `info.yaml` and the pin table in `docs/info.md`. Suggested names: `coeff[0]` to `coeff[7]` on `ui[0]` to `ui[7]`; `coeff[8]` to `coeff[11]` on `uio[0]` to `uio[3]`; `valid` on `uio[4]`; `sof` on `uio[5]`; `op[0]` and `op[1]` on `uio[6]` and `uio[7]`; `result[0]` to `result[7]` on `uo[0]` to `uo[7]`.
- **Until `ntt_core` exists,** use a stub that ties `r0`, `r1`, `pair_valid` and `flush` to 0, so you can build and lint your part alone.

## Tests and test vectors

- **Capture.** Drive random pin patterns for thousands of clocks. Each `_q` output must equal the pins from the previous clock. `sof_q` must be high only when both valid and sof were high.
- **Packer.** Random pairs with values 0..3328. Check the three bytes against a reference ByteEncode12 (bit by bit, little-endian), and check that they appear exactly 1, 2 and 3 clocks after `pair_valid`. `uo_byte` is 0x00 between pairs.
- **Back to back.** Pairs exactly 3 clocks apart give a continuous byte stream with no gaps.
- **Assertion.** Fails if two `pair_valid` pulses come less than 3 clocks apart.
- **Flush.** Flush in the middle of a burst. The remaining bytes are dropped and `uo_byte` is 0x00 from the next clock.
- **Reset.** Reset in the middle of a burst.
- **Shell.** Lint it with the same flow the Tiny Tapeout template uses. Check that `uio_oe` and `uio_out` are 0 for every input.

Vectors are in the sections above: the two pin words (0xE8 with 0x73, and 0xD0 with 0x17) and the four byte examples. The Pi owner's reference model supplies larger files.

## Deliverables and done when

**Hand over:**

- `io_in_capture.v`, `io_out_pack.v` and the top-level shell `tt_um_*.v`
- `info.yaml` with the pin names and the project description
- A testbench with the tests above, runnable with one command
- A short README: latency of each block (1 clock each), and the exact pin list

**Done when:**

- The capture, packer and flush tests pass.
- The shell lints cleanly and every `uio` is an input.
- The Control owner has connected `ntt_core` inside the shell without changing your ports.
- The Pi owner's software chip drives the same pin words you document.

**Do not:** decode `op` or count coefficients. That belongs to the Control owner.

## Shared rules for all five specs

- **One chip, one clock.** Everything runs on `clk`. Reset is `rst_n`, active low. Synchronous logic only, no latches, no generated clocks.
- **Numbers.** Every coefficient is 12 bits, unsigned, always in 0..3328 on every port.
- **Ports are frozen.** Tell the Control owner before changing a port name or width.
- **Latency is reported.** State the clocks each block takes in your README.
- **Reference.** The Pi owner's Python model is the source of truth.
- **Code.** Plain Verilog-2001 or SystemVerilog the Tiny Tapeout flow accepts, one module per file.
- **Sharing.** These docs are private. Share them with your teammates.
