// SPDX-License-Identifier: Apache-2.0
// Tiny Tapeout demo board v3: RP2350B (48 GPIOs). Pin roles are in rp/tt_rp.h.

// pico_cmake_set PICO_PLATFORM=rp2350

#ifndef _BOARDS_TT_DEMOBOARD_V3_H
#define _BOARDS_TT_DEMOBOARD_V3_H

#define PICO_RP2350A 0                      // the B package: GPIO 0-47
#define PICO_DEFAULT_LED_PIN 11

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
// pico_cmake_set_default PICO_FLASH_SIZE_BYTES = (2 * 1024 * 1024)
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)   // the firmware is far smaller; any larger flash works
#endif
#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64
#endif
// pico_cmake_set_default PICO_RP2350_A2_SUPPORTED = 1
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
