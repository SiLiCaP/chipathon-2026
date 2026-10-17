// SPDX-License-Identifier: Apache-2.0
// RP2350 side of the Tiny Tapeout demo board v3: project mux, reset, and the PIO + DMA transport.
#ifndef TT_RP_H
#define TT_RP_H

#include <stdbool.h>
#include <stdint.h>

#include "ntt_chip.h"

// Demo board v3 (RP2350B) pins, from the official TT MicroPython SDK (ttboard/pins/gpio_map_dbv3.py)
#define TT_PIN_CTRL_ENA 0
#define TT_PIN_CTRL_SEL_RST_N 1
#define TT_PIN_CTRL_SEL_INC 2
#define TT_PIN_LED 11
#define TT_PIN_RST_N 14
#define TT_PIN_CLK 16
#define TT_PIN_UI_IN0 17        // ui_in[7:0] = GPIO 17-24, uio[7:0] = GPIO 25-32: one 16-bit word
#define TT_PIN_UO_OUT0 33       // uo_out[7:0] = GPIO 33-40

void tt_rp_init(void);                    // pins, PIO program, DMA channels; chip clock 1 MHz, chip in reset
int tt_rp_select(int addr);               // select project `addr` on the TT mux (inputs safe meanwhile)
uint32_t tt_rp_set_clock(uint32_t hz);    // returns the clock actually set
uint32_t tt_rp_clock_hz(void);
void tt_rp_clock_phases(uint32_t *div, uint32_t *low, uint32_t *high);   // in system clocks

int tt_rp_run(const uint16_t *words, uint32_t n, uint8_t *uo);           // lockstep words, one sample each
int tt_rp_reset(uint32_t cycles);
uint8_t tt_rp_read_uo(void);              // uo_out right now (clock is stopped low)

tt_io_t tt_rp_io(void);                   // the transport for ntt_chip.c

#endif
