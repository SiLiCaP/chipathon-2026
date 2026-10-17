# Interface table

Owner: Control (Spec 3). Every port, width and latency of the blocks wired together in
the chip. Ask the Control owner before changing anything here; the change goes into
all five specs.

```
ui_in, uio_in ──► io_in_capture ──► ntt_core ──► io_out_pack ──► uo_out
                    (I/O)            (Control)      (I/O)
                                 ├─ modq_mul         (Multiplier)
                                 ├─ modq_addsub x2   (Adders: Pre ±, Post ±)
                                 ├─ modq_acc x2      (Adders: c0, c1)
                                 └─ zeta_rom, ntt_ctrl, ntt_sched, ntt_twiddle, ntt_delay (Control)
```

All blocks: one clock `clk`, synchronous active-low reset `rst_n`, coefficients 12 bits
unsigned in 0..3328 on every port.

## Latencies

| Block | Latency | Set where | Value now |
| --- | --- | --- | --- |
| `io_in_capture` | pins → `_q` outputs | `timing.py` `L_CAP` | 1 |
| `modq_mul` | `start` → `p_valid` | `ntt_core` parameter `L_MUL`, `timing.py` `L_MUL` | 3 |
| `modq_addsub` Pre ± | `REG_OUT` | `ntt_core` parameter `PRE_LAT`, `timing.py` `PRE_LAT` | 0 |
| `modq_addsub` Post ± | `REG_OUT` | `ntt_core` parameter `POST_LAT`, `timing.py` `POST_LAT` | 0 |
| `modq_acc` | `ld`/`add` → `acc` | fixed by spec | 1 |
| `io_out_pack` | `pair_valid` → first byte | `timing.py` `L_PACK` | 1 |
| `ntt_core` own stages | see `schedule.md` | | |

`ntt_core` supports `L_MUL` from 2 to 6 (the basemul m1 → m2 loop-back has 6 clocks; at 6,
m1 is forwarded from `p_out` on its own clock) and `PRE_LAT`/`POST_LAT` of 0 or 1.
`make -C test/core all-variants` runs all four `PRE_LAT`/`POST_LAT` settings with the
multiplier in `src/` (latency 3), and `L_MUL` = 2, 4, 5, 6 with a behavioral test
multiplier (`test/core/modq_mul_lat.v`). CI runs it. If a multiplier's latency is not 3, set
`L_MUL` on the `ntt_core` instance in the shell and in `test/timing.py`; the simulation
assertion `p_valid != schedule` fires if they disagree.

## `ntt_core` (Control)

| Port | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `clk`, `rst_n` | in | 1 each | Clock and synchronous reset |
| `coeff_q` | in | 12 | Captured coefficient, from `io_in_capture` |
| `valid_q` | in | 1 | Captured valid |
| `sof_q` | in | 1 | High only when valid and sof were both high |
| `op_q` | in | 2 | Captured op. Read only when `sof_q` is high |
| `r0` | out | 12 | First result of a pair |
| `r1` | out | 12 | Second result of a pair |
| `pair_valid` | out | 1 | High for one clock when `r0` and `r1` are valid. At least 3 clocks apart. Never high in a `flush` clock |
| `flush` | out | 1 | High for one clock when a running job is cancelled (sof while running). Goes to `io_out_pack` and to `modq_mul.flush` |

Parameters: `L_MUL` (4), `PRE_LAT` (0), `POST_LAT` (0).

## `io_in_capture` (I/O, Spec 5)

| Port | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `clk`, `rst_n` | in | 1 each | |
| `ui_in` | in | 8 | Pins |
| `uio_in` | in | 8 | Pins |
| `coeff_q` | out | 12 | `{uio_in[3:0], ui_in[7:0]}`, registered |
| `valid_q` | out | 1 | `uio_in[4]`, registered |
| `sof_q` | out | 1 | `uio_in[5] & uio_in[4]`, registered |
| `op_q` | out | 2 | `uio_in[7:6]`, registered every clock |

Latency 1.

## `io_out_pack` (I/O, Spec 5)

| Port | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `clk`, `rst_n` | in | 1 each | |
| `pair_valid` | in | 1 | Registers `r0`, `r1` |
| `r0`, `r1` | in | 12 each | Result pair |
| `flush` | in | 1 | Drops pending bytes; `uo_byte` = 0x00 from the next clock |
| `uo_byte` | out | 8 | `r0[7:0]`, `{r1[3:0], r0[11:8]}`, `r1[11:4]` on the 3 clocks after `pair_valid`, else 0x00 |

Latency 1 to the first byte.

## `modq_mul` (Multiplier, Spec 2)

| Port | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `clk`, `rst_n` | in | 1 each | |
| `start` | in | 1 | One clock; `a_in`, `b_in` sampled on this clock only |
| `a_in`, `b_in` | in | 12 each | Operands, 0..3328 |
| `flush` | in | 1 | Drops every operation in flight; `p_valid` low from the next clock |
| `p_out` | out | 12 | (2197 × a × b) mod 3329 (three K-RED rounds), holds until the next `p_valid` |
| `p_valid` | out | 1 | One clock per result, exactly `L_MUL` clocks after `start` |
| `ready` | out | 1 | Always high (pipelined). Not used by `ntt_core` |

What `ntt_core` relies on:

- The block accepts a `start` on every clock. `ntt_core` starts it at most once every
  3 clocks, except across a cancel: the old job's last start can be 1 clock before `flush`
  and the new job's first start 1 clock after it. `ntt_core` never raises `start` in the
  `flush` clock.
- `ntt_core` does not rely on `p_out` holding: it stores the values it needs later.
- The factor 2197 is cancelled by the constants `ntt_core` feeds in (ζ′, γ′, 304), see
  `zeta_rom` below. The multiplier never sees the mode.

## `modq_addsub` (Adders, Spec 4), two instances

| Port | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `clk`, `rst_n` | in | 1 each | Unused when `REG_OUT` = 0 |
| `x`, `y` | in | 12 each | Operands |
| `sum` | out | 12 | (x + y) mod 3329 |
| `diff` | out | 12 | (x − y) mod 3329 |

| Instance | x | y | Used |
| --- | --- | --- | --- |
| `u_pre` (Pre ±, inverse) | b (operand register R0) | a (R1) | `sum` = a + b to the bypass line, `diff` = b − a to the multiplier |
| `u_post` (Post ±, forward) | a (bypass line output) | t (`p_out`) | `sum` = a', `diff` = b' |

## `modq_acc` (Adders, Spec 4), two instances

| Port | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `clk`, `rst_n` | in | 1 each | Reset clears `acc` |
| `ld` | in | 1 | acc ← y (wins over `add`) |
| `add` | in | 1 | acc ← (acc + y) mod 3329 |
| `y` | in | 12 | `p_out` |
| `acc` | out | 12 | Registered |

| Instance | `ld` | `add` | Result |
| --- | --- | --- | --- |
| `u_acc0` | m3 = f0 × g0 | m2 = m1 × γ | c0 |
| `u_acc1` | m4 = f0 × g1 | m5 = f1 × g0 | c1 |

c0 loads m3 and adds m2 (Spec 4 lists m2 then m3). The order changed because m3 now runs
before m2 (see `schedule.md`); the sum is the same.

## `zeta_rom` (Control)

| Port | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `addr` | in | 7 | k |
| `data` | out | 12 | ROM′[k] = 17^BitRev7(k) × 2197⁻¹ mod 3329 (2197⁻¹ = 944), combinational |

The ROM stores the scaled twiddles because `modq_mul` returns 2197 × a × b: b × ζ′ comes
out as b × ζ. Generated by `scripts/gen_tables.py`, checked entry by entry against the
reference model's scaled table (`test/core` `test_zeta_rom`). γ′ is not stored:
γ′_i = ROM′[64 + (i >> 1)], or 3329 minus it for odd i. `tables/` has all four tables
(`zeta`, `gamma`, `zeta_scaled`, `gamma_scaled`).

Constants `ntt_core` feeds to the multiplier's b input:

| Mode | Constant | Pins show |
| --- | --- | --- |
| Forward, inverse | ζ′ = ζ × 2197⁻¹ | Plain FIPS 203 results |
| Basemul (pass 2) | γ′ = γ × 2197⁻¹ | 2197 × the plain basemul |
| Scale | 304 = 3303 × 2197⁻² | c × 3303 × 2197⁻¹; in the full flow the 2197 from basemul cancels |

## Integrating the owners' blocks

The files in `src/` marked `PLACEHOLDER` (`modq_mul.v`, `modq_addsub.v`, `modq_acc.v`)
follow Specs 2 and 4 so the chip builds and the tests run end to end. To integrate, copy the
owner's file over the placeholder with the same name. Then set `L_MUL`, `PRE_LAT` and
`POST_LAT` on the `ntt_core` instance in `tt_um_NTT_Core.v` and in `test/timing.py`, and run
`make -C test/mul exhaustive`, `make -C test` and `make -C test/core all-variants`.
