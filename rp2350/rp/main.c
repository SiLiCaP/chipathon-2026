// SPDX-License-Identifier: Apache-2.0
// RP2350 firmware for the NTT chip (Spec 1 harness and demo), for the Tiny Tapeout demo board v3 or for the
// Pico 2 W + Tang Nano 20K rig (TT_TARGET, see rp/tt_rp.h).
//
// Over USB serial it offers two interfaces on the same port; the first byte of each command decides:
//   text commands  (type `help`): selftest, demo, fuzz, matvec, bench, cal, sweep, clk, select, ...
//   TTE1 binary    the Tang Nano emulator protocol (tt-viktor-chiptor fpga/PROTOCOL.md): PING, RESET,
//                  SET_IN, STEP, SET_DIV, LOAD, RUN, READ_CAP. Its opcodes are non-printable bytes, so they
//                  never clash with typed text. The PC tools written for the FPGA (host/tt_emu.py,
//                  host/test_ntt.py, host/showcase.py) then drive the real chip through this board.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "hardware/clocks.h"
#include "hardware/structs/m33.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "swchip.h"
#include "tt_rp.h"

#ifndef TT_PROJECT_ADDR
#define TT_PROJECT_ADDR -1       // set at build time (cmake -DTT_PROJECT_ADDR=n) or with `select n`
#endif

static ntt_chip_t chip;
static swchip_t sw;
static tt_io_t io_hw, io_sw;
static int use_sw;
static int wrong_board;                // firmware built for the other board: pins left alone, software chip only
static int project = TT_PROJECT_ADDR;

uint64_t plat_time_us(void) { return time_us_64(); }
uint32_t plat_chip_hz(void) { return use_sw ? 0 : tt_rp_clock_hz(); }
uint32_t plat_cycles(void) { return m33_hw->dwt_cyccnt; }
uint32_t plat_cycles_hz(void) { return clock_get_hz(clk_sys); }
uint32_t plat_rand32(void) { return get_rand_32(); }

// ---------------- TTE1 binary protocol ----------------
#define TTE_DEPTH 8192
static uint16_t stim[TTE_DEPTH];
static uint8_t cap[TTE_DEPTH];
static uint16_t cur_in;

static bool is_opcode(int c) { return c <= 0x04 || (c >= 0x10 && c <= 0x12); }

static int rd8(void) { return getchar_timeout_us(155000); }      // drop a half command after 155 ms, like the FPGA

static int rd16(void) {
    int lo = rd8(), hi;
    if (lo < 0 || (hi = rd8()) < 0) return -1;
    return lo | (hi << 8);
}

static void reply(uint8_t status) { putchar_raw(0xA5); putchar_raw(status); }

static uint8_t current_uo(void) {
    if (!use_sw) return tt_rp_read_uo();
    return sw.out_clk[sw.clk & 63] == sw.clk ? sw.out[sw.clk & 63] : 0;
}

static int run_repeat(uint16_t w, uint32_t n) {                 // n clocks of one input word
    static uint16_t buf[256];
    static uint8_t sink[256];
    for (int i = 0; i < 256; i++) buf[i] = w;
    for (uint32_t done = 0; done < n; done += 256)
        if (chip.io->run(chip.io, buf, n - done < 256 ? n - done : 256, sink)) return -1;
    return 0;
}

static void tte_command(int op) {
    int a, n;
    switch (op) {
    case 0x00: reply(0); printf("TTE1"); break;                                // PING
    case 0x01:                                                                  // RESET u16
        if ((n = rd16()) < 0) return;
        if (use_sw) chip.io->reset(chip.io, (uint32_t)n);
        else { gpio_put(TT_PIN_RST_N, 0); run_repeat(cur_in, (uint32_t)n); gpio_put(TT_PIN_RST_N, 1); }
        reply(0);
        break;
    case 0x02:                                                                  // SET_IN ui uio
        if ((a = rd8()) < 0 || (n = rd8()) < 0) return;
        cur_in = (uint16_t)(a | (n << 8));
        reply(0);
        break;
    case 0x03:                                                                  // STEP u16
        if ((n = rd16()) < 0) return;
        run_repeat(cur_in, (uint32_t)n);
        reply(0); putchar_raw(current_uo()); putchar_raw(0); putchar_raw(0);
        break;
    case 0x04:                                                                  // SET_DIV d: 13.5 MHz / d
        if ((n = rd8()) < 0) return;
        if (!use_sw) tt_rp_set_clock(27000000u / (2u * (n ? (uint32_t)n : 1u)));
        reply(0);
        break;
    case 0x10:                                                                  // LOAD addr len words
        if ((a = rd16()) < 0 || (n = rd16()) < 0) return;
        for (int i = 0; i < n; i++) {
            int w = rd16();
            if (w < 0) return;
            stim[(a + i) % TTE_DEPTH] = (uint16_t)w;
        }
        reply(0);
        break;
    case 0x11:                                                                  // RUN len
        if ((n = rd16()) < 0) return;
        if (n > TTE_DEPTH) n = TTE_DEPTH;
        reply(chip.io->run(chip.io, stim, (uint32_t)n, cap) ? 2 : 0);
        if (n) cur_in = stim[n - 1];                                            // inputs keep the last word
        break;
    case 0x12:                                                                  // READ_CAP addr len
        if ((a = rd16()) < 0 || (n = rd16()) < 0) return;
        reply(0);
        for (int i = 0; i < n; i++) { putchar_raw(cap[(a + i) % TTE_DEPTH]); putchar_raw(0); putchar_raw(0); }
        break;
    default: reply(1);
    }
    stdio_flush();
}

// ---------------- text commands ----------------
static void status(void) {
    uint32_t div, lo, hi;
    tt_rp_clock_phases(&div, &lo, &hi);
    printf("NTT chip harness on the %s, RP2350 at %lu MHz\n", TT_TARGET_NAME, (unsigned long)(clock_get_hz(clk_sys) / 1000000));
    if (wrong_board)
        printf("  WRONG BOARD: this firmware is for the %s (RP2350%s); the pins are left alone, only `sw` works\n",
               TT_TARGET_NAME, TT_PACKAGE_QFN60 ? "A" : "B");
    printf("  transport : %s\n", chip.io->name);
#if TT_HAS_MUX
    printf("  project   : %d%s\n", project, project < 0 ? " (not selected: `select <address>`)" : "");
#endif
    printf("  chip clock: %lu Hz (PIO div %lu, low %lu + high %lu cycles)\n", (unsigned long)tt_rp_clock_hz(),
           (unsigned long)div, (unsigned long)lo, (unsigned long)hi);
    printf("  skew %d, calls %lu, faults %lu, clocks %llu, uo_out now 0x%02x, log %lu records\n", chip.skew,
           (unsigned long)chip.calls, (unsigned long)chip.faults, (unsigned long long)chip.clocks_total,
           wrong_board ? 0 : current_uo(), (unsigned long)oplog_count());
    if (use_sw && sw.violations)
        printf("  software chip: %lu protocol violations, last: %s\n", (unsigned long)sw.violations, sw.last_violation);
}

static void sweep(void) {
    static const uint32_t mhz10[] = {10, 50, 100, 150, 200, 250, 300, 375, 500, 600, 750};   // MHz x 10
    static uint16_t f[NTT_N], h[NTT_N];
    static const uint16_t lo[8] = {2429, 2845, 425, 795, 1865, 1356, 624, 31};
    const uint32_t keep = tt_rp_clock_hz();
    const int keep_skew = chip.skew;
    printf("clock sweep: forward NTT of the ramp at each clock and sample skew\n  %10s  skew 0  skew 1  skew 2\n", "clock");
    for (unsigned i = 0; i < sizeof mhz10 / sizeof mhz10[0]; i++) {
        uint32_t hz = tt_rp_set_clock(mhz10[i] * 100000u);
        printf("  %7.2f MHz", hz / 1e6);
        for (int k = 0; k <= 2; k++) {
            chip.skew = k;
            int ok = 1;
            for (int r = 0; r < 3 && ok; r++) {
                for (int j = 0; j < NTT_N; j++) f[j] = (uint16_t)j;
                ok = ntt_chip_forward(&chip, f, h) == NTT_OK && !memcmp(h, lo, sizeof lo);
            }
            printf("  %6s", ok ? "pass" : "-");
        }
        printf("\n");
    }
    chip.skew = keep_skew;
    tt_rp_set_clock(keep);
    printf("back at %lu Hz, skew %d. Run at a clock where skew 0 or 1 passes with margin on both sides.\n",
           (unsigned long)tt_rp_clock_hz(), chip.skew);
}

static void help(void) {
    printf("commands:\n"
           "  status              board, clock, transport, counters\n"
           "  select <addr>       select our project on the TT mux (demo board; on the rig: just a reset)\n"
           "  clk <hz>            chip clock (starts at 1 MHz; integer PIO divider, long high phase)\n"
           "  sys <mhz>           RP2350 system clock (150 default; 200 gives 50 MHz with even phases)\n"
           "  reset               rst_n low for 10 clocks\n"
           "  hw | sw             use the real chip, or the software chip (works on any RP2350 board)\n"
           "  selftest            reference + chip known answers (Spec 1 table)\n"
           "  demo [seed]         Spec 1 demo: f * g on the chip vs schoolbook, clocks per step\n"
           "  fuzz [n]            all four modes vs the chip-level model: edge vectors + n random\n"
           "  matvec [seed]       stretch goal: t = A s for k = 2\n"
           "  bench [n]           time the four calls; where the time goes; predicted at 50 MHz\n"
           "  log [csv|clear]     operation log: per-call summary, every batch as CSV, or clear it\n"
           "  predict <hz>        the logged calls re-predicted at chip clock <hz> (e.g. 50000000)\n"
           "  cal                 find the sample skew at the current clock\n"
           "  sweep               known answers across clocks and skews (bring-up)\n"
           "  all                 selftest, demo, fuzz, matvec\n"
           "binary TTE1 commands are accepted on the same port (host/tt_emu.py --port /dev/ttyACM0)\n");
}

static void command(char *line) {
    char *cmd = strtok(line, " \t");
    char *arg = strtok(NULL, " \t");
    const uint32_t n = arg ? (uint32_t)strtoul(arg, NULL, 0) : 0;
    if (!cmd) return;
    if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) help();
    else if (!strcmp(cmd, "status")) status();
    else if (!strcmp(cmd, "log")) {
        if (arg && !strcmp(arg, "csv")) oplog_csv();
        else if (arg && !strcmp(arg, "clear")) { oplog_clear(); printf("log cleared\n"); }
        else oplog_summary(50000000);
    } else if (!strcmp(cmd, "predict") && arg) oplog_summary(n);
    else if (wrong_board && strcmp(cmd, "sw") && strcmp(cmd, "selftest") && strcmp(cmd, "demo") && strcmp(cmd, "fuzz") &&
             strcmp(cmd, "matvec") && strcmp(cmd, "all") && strcmp(cmd, "bench"))
        printf("wrong board for this firmware: see `status`\n");
    else if (!strcmp(cmd, "select") && arg) {
        project = (int)n;
        printf("select %d: %s\n", project, tt_rp_select(project) ? "reset failed" : "done");
    } else if (!strcmp(cmd, "clk") && arg) printf("chip clock %lu Hz\n", (unsigned long)tt_rp_set_clock(n));
    else if (!strcmp(cmd, "sys") && arg) {
        uint32_t keep = tt_rp_clock_hz();
        if (!set_sys_clock_khz(n * 1000, false)) printf("sys %lu MHz not possible\n", (unsigned long)n);
        tt_rp_set_clock(keep);
        oplog_clear();                                // the log's cycles are in units of the old clock
        printf("RP2350 at %lu MHz, chip clock %lu Hz (log cleared)\n", (unsigned long)(clock_get_hz(clk_sys) / 1000000),
               (unsigned long)tt_rp_clock_hz());
    } else if (!strcmp(cmd, "reset")) printf("reset: %s\n", ntt_chip_reset(&chip) ? "failed" : "done");
    else if (!strcmp(cmd, "hw")) {
        if (wrong_board) printf("wrong board for this firmware: see `status`\n");
        else { use_sw = 0; chip.io = &io_hw; printf("transport: %s\n", chip.io->name); }
    }
    else if (!strcmp(cmd, "sw")) { use_sw = 1; chip.io = &io_sw; printf("transport: %s\n", chip.io->name); }
    else if (!strcmp(cmd, "selftest")) app_selftest(&chip);
    else if (!strcmp(cmd, "demo")) app_demo(&chip, n ? n : 1);
    else if (!strcmp(cmd, "fuzz")) app_fuzz(&chip, n ? n : 10, get_rand_32());
    else if (!strcmp(cmd, "matvec")) app_matvec(&chip, n ? n : 1);
    else if (!strcmp(cmd, "bench")) app_bench(&chip, n ? n : 20, 50000000);
    else if (!strcmp(cmd, "cal")) app_calibrate(&chip);
    else if (!strcmp(cmd, "sweep")) { if (use_sw) printf("sweep needs the real chip (hw)\n"); else sweep(); }
    else if (!strcmp(cmd, "all")) {
        int fail = app_selftest(&chip) | app_demo(&chip, 1) | app_fuzz(&chip, 10, 1) | app_matvec(&chip, 1);
        printf("all: %s\n", fail ? "FAIL" : "PASS");
    } else printf("unknown command '%s' (try help)\n", cmd);
}

int main(void) {
    stdio_init_all();
    wrong_board = tt_rp_init() != 0;
    swchip_init(&sw);
    io_sw = swchip_io(&sw);
    io_hw = tt_rp_io();
    ntt_chip_init(&chip, wrong_board ? &io_sw : &io_hw);
    use_sw = wrong_board;
#if TT_HAS_MUX
    if (project >= 0 && !wrong_board) tt_rp_select(project);
#else
    if (!wrong_board) tt_rp_reset(10);
#endif
    char line[96];
    int len = 0;
    uint64_t blink = 0;
    for (;;) {
        int ch = getchar_timeout_us(50000);
        if (time_us_64() - blink > 500000) {          // heartbeat
            blink = time_us_64();
#if TT_PIN_LED >= 0
            if (!wrong_board) gpio_xor_mask64(1ull << TT_PIN_LED);
#endif
        }
        if (ch < 0) continue;
        if (len == 0 && is_opcode(ch)) { tte_command(ch); continue; }
        if (ch == '\r' || ch == '\n') {
            if (len) {
                line[len] = 0;
                printf("\n");
                command(line);
                printf("> ");
            }
            len = 0;
        } else if ((ch == 8 || ch == 127) && len) {
            len--;
            printf("\b \b");
        } else if (ch >= 32 && len < (int)sizeof line - 1) {
            line[len++] = (char)ch;
            putchar(ch);
        }
    }
}
