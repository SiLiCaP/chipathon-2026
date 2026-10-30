# Tang Nano emulator: host protocol (v1, magic `TTE1`)

Binary over the BL616 USB-UART, 1 Mbaud 8N1 by default (Makefile `BAUD`). Multi-byte fields are little-endian.
Every command starts with a 1-byte opcode. Every response starts with `A5` and a status byte
(`00` = OK, `01` = unknown opcode).

| Op | Name | Payload | Response after `A5 00` |
|---|---|---|---|
| `00` | PING | none | 4 bytes `54 54 45 31` ("TTE1") |
| `01` | RESET | u16 cycles | none. Holds `rst_n` low for `cycles` DUT clocks, then releases it with the clock low |
| `02` | SET_IN | u8 ui_in, u8 uio_in | none |
| `03` | STEP | u16 n | u8 uo_out, u8 uio_out, u8 uio_oe, sampled after the last of `n` clocks (`n = 0`: sample without clocking) |
| `04` | SET_DIV | u8 d | none. Run-mode DUT clock = 27 MHz / (2·d); `0` counts as `1` |
| `10` | LOAD | u16 addr, u16 len, len × u16 | none. Writes `{uio_in, ui_in}` words into the stimulus RAM |
| `11` | RUN | u16 len | none (sent when done). Plays stim[0..len-1] and writes cap[0..len-1] |
| `12` | READ_CAP | u16 addr, u16 len | len × 3 bytes: uo_out, uio_out, uio_oe |

Both RAMs hold 8192 words. Addresses wrap at 8192, and `len` for RUN must be ≤ 8192.

## Timing (identical in STEP, RESET and RUN)

- One DUT period lasts 2·d system clocks: high for d, then low for d. STEP and RESET use the current d too.
- Inputs change on the **falling** edge of the DUT clock.
- Outputs are sampled on the system clock edge that raises the DUT clock, i.e. **just before**
  the DUT's rising edge.
- So for a RUN, `cap[i] = f(stim[i], state after i rising edges)`, the same convention as cocotb
  ("set inputs, read outputs, then clock") and as `host/dut_models.py`.
- After a RUN the clock stops low and the inputs keep holding `stim[len-1]`.
  `TTEmu.run_stream()` chains RAM-sized chunks this way. A synchronous DUT can't tell the
  difference, because pausing the clock is invisible to it.

## Robustness

- If the host stops partway through a command for about 155 ms, the FPGA drops it and goes back to idle.
  `TTEmu.resync()` waits 200 ms and flushes, so the host can always recover.
- The BL616 also has its own console on the same port (see README). Binary payloads that contain
  `18 03 0D` were tested and do not trigger it.
