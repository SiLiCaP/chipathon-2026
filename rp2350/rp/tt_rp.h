// SPDX-License-Identifier: Apache-2.0
// RP2350 side of the chip: pins, project mux, reset, and the PIO + DMA transport.
//
// Two test environments, chosen at build time (cmake -DTT_TARGET=...):
//   dbv3         the Tiny Tapeout demo board v3 (RP2350B) with the real chip            (default)
//   pico2w_tang  a Raspberry Pi Pico 2 W (RP2350A) wired to a Tang Nano 20K that runs the chip's
//                gate netlist: the same RP2350 core, firmware and PIO lockstep, the chip replaced by
//                the FPGA. Wiring: tt-viktor-chiptor docs/PICO_TANG_RIG.md.
#ifndef TT_RP_H
#define TT_RP_H

#include <stdbool.h>
#include <stdint.h>

#include "tt_io.h"

#if defined(TT_TARGET_PICO2W_TANG)
// Pico 2 W: GPIO 0-22 and 26-28 reach the header (23-25 and 29 belong to the wireless chip), 26 pins for
// 26 signals. The PIO needs the 16 chip inputs as one OUT window and the 8 outputs as one IN window, and
// 0-22 holds only 23 pins, so the OUT window starts at GPIO 26 and wraps: 26 27 28 (29 30 31 unused) 0-12.
// The firmware spreads each 16-bit pin word over those 19 bits (tt_rp.c pio_word()).
#define TT_TARGET_NAME "Pico 2 W + Tang Nano 20K rig"
#define TT_HAS_MUX 0
#define TT_PIN_LED (-1)         // the Pico 2 W LED hangs off the wireless chip: not used
#define TT_PIN_RST_N 22
#define TT_PIN_CLK 21
#define TT_PIN_OUT_BASE 26      // PIO OUT window, 19 bits: uio[5..7] on GPIO 26-28, then ui_in[0..7], uio[0..4] on GPIO 0-12
#define TT_N_OUT_PIO 19
#define TT_PIN_UO_OUT0 13       // uo_out[7:0] = GPIO 13-20
#define TT_PIO_GPIO_BASE 0
#define TT_PACKAGE_QFN60 1      // RP2350A
#else
// Tiny Tapeout demo board v3 (RP2350B), pins from the TT MicroPython SDK (ttboard/pins/gpio_map_dbv3.py)
#define TT_TARGET_DBV3 1
#define TT_TARGET_NAME "Tiny Tapeout demo board v3"
#define TT_HAS_MUX 1
#define TT_PIN_CTRL_ENA 0
#define TT_PIN_CTRL_SEL_RST_N 1
#define TT_PIN_CTRL_SEL_INC 2
#define TT_PIN_LED 11
#define TT_PIN_RST_N 14
#define TT_PIN_CLK 16
#define TT_PIN_OUT_BASE 17      // ui_in[7:0] = GPIO 17-24, uio[7:0] = GPIO 25-32: one 16-bit word as is
#define TT_N_OUT_PIO 16
#define TT_PIN_UO_OUT0 33       // uo_out[7:0] = GPIO 33-40
#define TT_PIO_GPIO_BASE 16
#define TT_PACKAGE_QFN60 0      // RP2350B
#endif

int tt_rp_init(void);                     // 0, or -1 if this firmware was built for the other board (pins untouched)
int tt_rp_select(int addr);               // select project `addr` on the TT mux (demo board; the rig has none)
uint32_t tt_rp_set_clock(uint32_t hz);    // returns the clock actually set
uint32_t tt_rp_clock_hz(void);
void tt_rp_clock_phases(uint32_t *div, uint32_t *low, uint32_t *high);   // in system clocks

int tt_rp_run(const uint16_t *words, uint32_t n, uint8_t *uo);           // lockstep words, one sample each
uint32_t tt_rp_last_conv_cyc(void);       // cycles the last run spent reformatting words (rig only)
int tt_rp_reset(uint32_t cycles);
uint8_t tt_rp_read_uo(void);              // uo_out right now (clock is stopped low)

tt_io_t tt_rp_io(void);                   // the transport for hash_chip.c

#endif
