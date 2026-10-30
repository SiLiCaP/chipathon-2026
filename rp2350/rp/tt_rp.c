// SPDX-License-Identifier: Apache-2.0
// RP2350 side of the chip. See tt_rp.h and tt_lockstep.pio.
#include "tt_rp.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/structs/m33.h"
#include "hardware/structs/sysinfo.h"
#include "pico/stdlib.h"
#include "tt_lockstep.pio.h"

#if defined(TT_TARGET_DBV3) && (NUM_BANK0_GPIOS != 48 || !PICO_PIO_USE_GPIO_BASE)
#error "the demo board's chip pins are GPIO 14-40: build for the RP2350B (TT_TARGET=dbv3, PICO_BOARD=tt_demoboard_v3)"
#endif
#if defined(TT_TARGET_PICO2W_TANG) && NUM_BANK0_GPIOS != 30
#error "the Pico 2 W rig is an RP2350A (TT_TARGET=pico2w_tang, PICO_BOARD=pico2_w)"
#endif

#define N_IN 8                            // uo_out
#define TTE_MAX 8192                      // longest run (the TTE1 bridge's RAM)

#if TT_PIO_GPIO_BASE == 16
// demo board: clk on 16, the 16 input pins on 17-32: one word is the pin pattern itself
#define MASK_OUT ((((1ull << TT_N_OUT_PIO) - 1) << TT_PIN_OUT_BASE) | (1ull << TT_PIN_CLK))
#define MASK_IN_PINS (((1ull << TT_N_OUT_PIO) - 1) << TT_PIN_OUT_BASE)
#else
// rig: GPIO 0-12 and 26-28 (29-31 lie inside the OUT window but stay with SIO / the wireless chip:
// their PIO output is never connected to a pad)
#define MASK_IN_PINS ((0x1FFFull << 0) | (0x7ull << 26))
#define MASK_OUT (MASK_IN_PINS | (1ull << TT_PIN_CLK))
static uint32_t stage[TTE_MAX];           // words spread over the 19-bit OUT window
#endif

static PIO pio = pio0;
static const uint sm = 0;
static int offset = -1;
static int dma_tx, dma_rx;
static uint16_t instr[2];
static pio_program_t prog;
static uint32_t ph_div = 1, ph_low = 75, ph_high = 75, clock_hz, conv_cyc;

// {uio, ui_in} pin word -> the PIO OUT window bits
static inline uint32_t pio_word(uint16_t w) {
#if TT_PIO_GPIO_BASE == 16
    return w;
#else
    return ((uint32_t)(w >> 13) & 7u) | ((uint32_t)(w & 0x1FFF) << 6);   // uio[7:5] -> GPIO 26-28; rest -> 0-12
#endif
}

// (Re)load the program with the current phase lengths. The state machine idles with clk low.
static void sm_load(void) {
    pio_sm_set_enabled(pio, sm, false);
    if (offset >= 0) pio_remove_program(pio, &prog, (uint)offset);
    instr[0] = (uint16_t)((tt_lockstep_program_instructions[0] & ~0x1Fu) | (TT_N_OUT_PIO & 0x1F))   // OUT bit count
               | (uint16_t)((ph_low - 1) << 8);                                                   // 4-bit delay
    instr[1] = tt_lockstep_program_instructions[1] | (uint16_t)((ph_high - 1) << 8);
    prog = tt_lockstep_program;
    prog.instructions = instr;
    offset = pio_add_program(pio, &prog);
    pio_sm_config c = tt_lockstep_program_get_default_config((uint)offset);
    sm_config_set_out_pins(&c, TT_PIN_OUT_BASE, TT_N_OUT_PIO);
    sm_config_set_in_pins(&c, TT_PIN_UO_OUT0);
    sm_config_set_in_pin_count(&c, N_IN);
    sm_config_set_sideset_pins(&c, TT_PIN_CLK);
    sm_config_set_out_shift(&c, true, true, TT_N_OUT_PIO);   // one word per FIFO entry
    sm_config_set_in_shift(&c, false, true, 8);               // one sample per FIFO entry, in bits 7:0
    sm_config_set_clkdiv_int_frac8(&c, ph_div, 0);            // integer divider: no clock jitter
    pio_sm_init(pio, sm, (uint)offset, &c);
    pio_sm_set_pins_with_mask64(pio, sm, 0, MASK_OUT);        // clk low, inputs idle
    pio_sm_set_pindirs_with_mask64(pio, sm, MASK_OUT, MASK_OUT);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);                        // stalls on the first OUT until words arrive
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

int tt_rp_init(void) {
    // the CPU cycle counter (timing the transport)
    m33_hw->demcr |= M33_DEMCR_TRCENA_BITS;
    m33_hw->dwt_ctrl |= M33_DWT_CTRL_CYCCNTENA_BITS;
    // Built for the other board? Its pin map would drive pins the FPGA (or the TT chip) drives: stop here.
    if ((sysinfo_hw->package_sel & 1u) != TT_PACKAGE_QFN60) return -1;   // datasheet: 0 = QFN80, 1 = QFN60
#if TT_HAS_MUX
    const uint ctrl[] = {TT_PIN_CTRL_ENA, TT_PIN_CTRL_SEL_RST_N, TT_PIN_CTRL_SEL_INC};
    for (unsigned i = 0; i < sizeof ctrl / sizeof ctrl[0]; i++) {
        gpio_init(ctrl[i]);
        gpio_set_dir(ctrl[i], GPIO_OUT);
        gpio_put(ctrl[i], 0);                         // mux disabled
    }
#endif
#if TT_PIN_LED >= 0
    gpio_init(TT_PIN_LED);
    gpio_set_dir(TT_PIN_LED, GPIO_OUT);
#endif
    gpio_init(TT_PIN_RST_N);
    gpio_set_dir(TT_PIN_RST_N, GPIO_OUT);
    gpio_put(TT_PIN_RST_N, 0);                        // chip in reset
    for (uint p = 0; p < N_IN; p++) {                 // uo_out: plain inputs (also clears pad isolation)
        gpio_init(TT_PIN_UO_OUT0 + p);
        gpio_set_dir(TT_PIN_UO_OUT0 + p, GPIO_IN);
        gpio_disable_pulls(TT_PIN_UO_OUT0 + p);
    }
#if TT_PIO_GPIO_BASE
    pio_set_gpio_base(pio, TT_PIO_GPIO_BASE);         // the demo board's PIO addresses GPIO 16-47
#endif
    for (uint p = 0; p < 48; p++) {
        if (!((MASK_OUT >> p) & 1)) continue;
        pio_gpio_init(pio, p);
#if TT_PIO_GPIO_BASE == 16
        gpio_set_slew_rate(p, GPIO_SLEW_RATE_FAST);   // short traces on the board
        gpio_set_drive_strength(p, GPIO_DRIVE_STRENGTH_8MA);
#else
        gpio_set_slew_rate(p, GPIO_SLEW_RATE_SLOW);   // jumper wires: gentler edges ring less
        gpio_set_drive_strength(p, GPIO_DRIVE_STRENGTH_4MA);
#endif
    }
    pio_sm_claim(pio, sm);
    dma_tx = dma_claim_unused_channel(true);
    dma_rx = dma_claim_unused_channel(true);
    tt_rp_set_clock(1000000);                         // Spec 1: start slow, raise once it works
    return 0;
}

int tt_rp_run(const uint16_t *words, uint32_t n, uint8_t *uo) {
    conv_cyc = 0;
    if (n == 0) return 0;
    if (n > TTE_MAX) return -1;
    dma_channel_config rc = dma_channel_get_default_config((uint)dma_rx);
    channel_config_set_transfer_data_size(&rc, DMA_SIZE_8);
    channel_config_set_read_increment(&rc, false);
    channel_config_set_write_increment(&rc, true);
    channel_config_set_dreq(&rc, pio_get_dreq(pio, sm, false));
    dma_channel_config tc = dma_channel_get_default_config((uint)dma_tx);
    channel_config_set_read_increment(&tc, true);
    channel_config_set_write_increment(&tc, false);
    channel_config_set_dreq(&tc, pio_get_dreq(pio, sm, true));
#if TT_PIO_GPIO_BASE == 16
    channel_config_set_transfer_data_size(&tc, DMA_SIZE_16);   // the words go to the pins as they are
    const void *src = words;
#else
    const uint32_t t0 = m33_hw->dwt_cyccnt;
    for (uint32_t i = 0; i < n; i++) stage[i] = pio_word(words[i]);
    conv_cyc = m33_hw->dwt_cyccnt - t0;
    channel_config_set_transfer_data_size(&tc, DMA_SIZE_32);
    const void *src = stage;
#endif
    dma_channel_configure((uint)dma_rx, &rc, uo, &pio->rxf[sm], n, true);
    dma_channel_configure((uint)dma_tx, &tc, &pio->txf[sm], src, n, true);
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

uint32_t tt_rp_last_conv_cyc(void) { return conv_cyc; }

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
#if TT_HAS_MUX
    // While the mux switches, the previous project may drive uio: keep our 16 input pins as inputs.
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_set_pindirs_with_mask64(pio, sm, 0, MASK_IN_PINS);
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
#else
    (void)addr;                                       // the rig wires the FPGA straight to the pins
#endif
    return tt_rp_reset(10);
}

static int io_run(tt_io_t *io, const uint16_t *w, uint32_t n, uint8_t *uo) {
    int err = tt_rp_run(w, n, uo);
    io->conv_cyc = conv_cyc;
    return err;
}
static int io_reset(tt_io_t *io, uint32_t cycles) { (void)io; return tt_rp_reset(cycles); }

tt_io_t tt_rp_io(void) { return (tt_io_t){.run = io_run, .reset = io_reset, .name = "chip via PIO + DMA (" TT_TARGET_NAME ")"}; }
