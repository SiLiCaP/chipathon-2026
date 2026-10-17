// SPDX-License-Identifier: Apache-2.0
// RP2350 side of the Tiny Tapeout demo board v3. See tt_rp.h and tt_lockstep.pio.
#include "tt_rp.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "tt_lockstep.pio.h"

#if NUM_BANK0_GPIOS != 48 || !PICO_PIO_USE_GPIO_BASE
#error "the demo board's chip pins are GPIO 14-40: build for the RP2350B (PICO_BOARD=tt_demoboard_v3)"
#endif

#define N_OUT 16                          // ui_in + uio
#define N_IN 8                            // uo_out
#define MASK_OUT (((1ull << (N_OUT + 1)) - 1) << TT_PIN_CLK)   // clk + 16 input pins

static PIO pio = pio0;
static const uint sm = 0;
static int offset = -1;
static int dma_tx, dma_rx;
static uint16_t instr[2];
static pio_program_t prog;
static uint32_t ph_div = 1, ph_low = 75, ph_high = 75, clock_hz;

// (Re)load the program with the current phase lengths. The state machine idles with clk low.
static void sm_load(void) {
    pio_sm_set_enabled(pio, sm, false);
    if (offset >= 0) pio_remove_program(pio, &prog, (uint)offset);
    instr[0] = tt_lockstep_program_instructions[0] | (uint16_t)((ph_low - 1) << 8);    // 4-bit delay field
    instr[1] = tt_lockstep_program_instructions[1] | (uint16_t)((ph_high - 1) << 8);
    prog = tt_lockstep_program;
    prog.instructions = instr;
    offset = pio_add_program(pio, &prog);
    pio_sm_config c = tt_lockstep_program_get_default_config((uint)offset);
    sm_config_set_out_pins(&c, TT_PIN_UI_IN0, N_OUT);
    sm_config_set_in_pins(&c, TT_PIN_UO_OUT0);
    sm_config_set_in_pin_count(&c, N_IN);
    sm_config_set_sideset_pins(&c, TT_PIN_CLK);
    sm_config_set_out_shift(&c, true, true, 16);      // one 16-bit word per FIFO entry
    sm_config_set_in_shift(&c, false, true, 8);       // one sample per FIFO entry, in bits 7:0
    sm_config_set_clkdiv_int_frac8(&c, ph_div, 0);    // integer divider: no clock jitter
    pio_sm_init(pio, sm, (uint)offset, &c);
    pio_sm_set_pins_with_mask64(pio, sm, 0, MASK_OUT);          // clk low, inputs idle
    pio_sm_set_pindirs_with_mask64(pio, sm, MASK_OUT, MASK_OUT);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);                // stalls on the first OUT until words arrive
}

uint32_t tt_rp_set_clock(uint32_t hz) {
    const uint32_t sys = clock_get_hz(clk_sys);
    if (hz == 0) hz = 1;
    uint32_t total = (sys + hz / 2) / hz;             // system clocks per chip clock
    if (total < 2) total = 2;
    uint32_t div = (total + 31) / 32;                 // each phase is 1-16 PIO cycles
    if (div > 65535) div = 65535;
    uint32_t per = (total + div / 2) / div;
    if (per < 2) per = 2;
    if (per > 32) per = 32;
    // The longer phase goes high: inputs change at the falling edge, so the high time is the hold margin
    // the chip gets against its own clock insertion delay (up to ~10 ns on Tiny Tapeout).
    ph_high = (per + 1) / 2;
    ph_low = per - ph_high;
    ph_div = div;
    sm_load();
    clock_hz = sys / (div * per);
    return clock_hz;
}

uint32_t tt_rp_clock_hz(void) { return clock_hz; }

void tt_rp_clock_phases(uint32_t *div, uint32_t *low, uint32_t *high) {
    *div = ph_div; *low = ph_low; *high = ph_high;
}

void tt_rp_init(void) {
    const uint ctrl[] = {TT_PIN_CTRL_ENA, TT_PIN_CTRL_SEL_RST_N, TT_PIN_CTRL_SEL_INC, TT_PIN_RST_N, TT_PIN_LED};
    for (unsigned i = 0; i < sizeof ctrl / sizeof ctrl[0]; i++) {
        gpio_init(ctrl[i]);
        gpio_set_dir(ctrl[i], GPIO_OUT);
        gpio_put(ctrl[i], 0);                         // mux disabled, chip in reset
    }
    for (uint p = 0; p < N_IN; p++) {                 // uo_out: plain inputs (also clears pad isolation)
        gpio_init(TT_PIN_UO_OUT0 + p);
        gpio_set_dir(TT_PIN_UO_OUT0 + p, GPIO_IN);
        gpio_disable_pulls(TT_PIN_UO_OUT0 + p);
    }
    pio_set_gpio_base(pio, 16);                       // this PIO addresses GPIO 16-47
    for (uint p = TT_PIN_CLK; p < TT_PIN_UI_IN0 + N_OUT; p++) {
        pio_gpio_init(pio, p);
        gpio_set_slew_rate(p, GPIO_SLEW_RATE_FAST);
        gpio_set_drive_strength(p, GPIO_DRIVE_STRENGTH_8MA);
    }
    pio_sm_claim(pio, sm);
    dma_tx = dma_claim_unused_channel(true);
    dma_rx = dma_claim_unused_channel(true);
    tt_rp_set_clock(1000000);                         // Spec 1: start slow, raise once it works
}

int tt_rp_run(const uint16_t *words, uint32_t n, uint8_t *uo) {
    if (n == 0) return 0;
    dma_channel_config rc = dma_channel_get_default_config((uint)dma_rx);
    channel_config_set_transfer_data_size(&rc, DMA_SIZE_8);
    channel_config_set_read_increment(&rc, false);
    channel_config_set_write_increment(&rc, true);
    channel_config_set_dreq(&rc, pio_get_dreq(pio, sm, false));
    dma_channel_configure((uint)dma_rx, &rc, uo, &pio->rxf[sm], n, true);
    dma_channel_config tc = dma_channel_get_default_config((uint)dma_tx);
    channel_config_set_transfer_data_size(&tc, DMA_SIZE_16);
    channel_config_set_read_increment(&tc, true);
    channel_config_set_write_increment(&tc, false);
    channel_config_set_dreq(&tc, pio_get_dreq(pio, sm, true));
    dma_channel_configure((uint)dma_tx, &tc, &pio->txf[sm], words, n, true);
    // n clocks at clock_hz, with 4x margin plus 20 ms for the bus
    const absolute_time_t deadline = make_timeout_time_us((uint64_t)n * 4000000u / clock_hz + 20000);
    while (dma_channel_is_busy((uint)dma_rx)) {
        if (time_reached(deadline)) {
            dma_channel_abort((uint)dma_tx);
            dma_channel_abort((uint)dma_rx);
            sm_load();                                // back to a clean state, clk low
            return -1;
        }
        tight_loop_contents();
    }
    return 0;
}

int tt_rp_reset(uint32_t cycles) {
    static const uint16_t idle[64] = {0};
    static uint8_t sink[64];
    gpio_put(TT_PIN_RST_N, 0);
    int err = 0;
    for (uint32_t done = 0; done < cycles && !err; done += 64)
        err = tt_rp_run(idle, cycles - done < 64 ? cycles - done : 64, sink);
    gpio_put(TT_PIN_RST_N, 1);                        // released with clk low, like the FPGA emulator
    return err;
}

uint8_t tt_rp_read_uo(void) { return (uint8_t)(gpio_get_all64() >> TT_PIN_UO_OUT0); }

int tt_rp_select(int addr) {
    // While the mux switches, the previous project may drive uio: keep our 16 input pins as inputs.
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_set_pindirs_with_mask64(pio, sm, 0, MASK_OUT & ~(1ull << TT_PIN_CLK));
    gpio_put(TT_PIN_RST_N, 0);
    gpio_put(TT_PIN_CTRL_SEL_INC, 0);
    gpio_put(TT_PIN_CTRL_SEL_RST_N, 0);
    gpio_put(TT_PIN_CTRL_ENA, 0);
    sleep_ms(10);
    gpio_put(TT_PIN_CTRL_SEL_RST_N, 1);
    sleep_ms(10);
    for (int i = 0; i < addr; i++) {                  // one pulse per project address step
        gpio_put(TT_PIN_CTRL_SEL_INC, 1);
        sleep_ms(1);
        gpio_put(TT_PIN_CTRL_SEL_INC, 0);
        sleep_ms(1);
    }
    gpio_put(TT_PIN_CTRL_ENA, 1);
    sm_load();                                        // clean restart: pins driven again, clk low
    return tt_rp_reset(10);
}

static int io_run(tt_io_t *io, const uint16_t *w, uint32_t n, uint8_t *uo) { (void)io; return tt_rp_run(w, n, uo); }
static int io_reset(tt_io_t *io, uint32_t cycles) { (void)io; return tt_rp_reset(cycles); }

tt_io_t tt_rp_io(void) { return (tt_io_t){.run = io_run, .reset = io_reset, .name = "TT chip (PIO + DMA)"}; }
