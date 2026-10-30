// SPDX-License-Identifier: Apache-2.0
// PC build of the RP2350 harness: the same core (job building, unpacking, tests, demo) against
//   --sw           the software chip (default), or
//   --port DEV     a Tang Nano 20K running the chip RTL or its gate netlist (TTE1 protocol, fpga/PROTOCOL.md
//                  in tt-viktor-chiptor), so the C harness is proven on real logic before silicon exists.
//
//   make -C rp2350/pc && rp2350/pc/ntt_host [--sw | --port /dev/ttyUSB1] all | selftest | demo [seed] |
//                                     fuzz [n] | matvec [seed] | ci | bench [n] | cal
//   --log summary|csv   after the command, print the operation log (oplog.h). On a PC the times are this PC's,
//                       and with --port the transfer overhead is USB: use the RP2350 log for silicon estimates.
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "../core/app.h"
#include "../core/ci_app.h"
#include "../core/swchip.h"

static uint32_t chip_hz;

uint64_t plat_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
uint32_t plat_chip_hz(void) { return chip_hz; }
uint32_t plat_cycles(void) {           // nanoseconds stand in for cycles on a PC
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec);
}
uint32_t plat_cycles_hz(void) { return 1000000000u; }
uint32_t plat_rand32(void) { return (uint32_t)random(); }

// ---------------- TTE1 transport (Tang Nano emulator) ----------------
enum { OP_PING = 0x00, OP_RESET = 0x01, OP_SET_IN = 0x02, OP_SET_DIV = 0x04, OP_LOAD = 0x10, OP_RUN = 0x11,
       OP_READ_CAP = 0x12 };
#define TTE_DEPTH 8192

static int fd = -1;

static int wr(const uint8_t *b, size_t n) {
    while (n) {
        ssize_t k = write(fd, b, n);
        if (k <= 0) return -1;
        b += k; n -= (size_t)k;
    }
    return 0;
}

static int rd(uint8_t *b, size_t n) {          // reads exactly n bytes or fails after 3 s of silence
    while (n) {
        fd_set s;
        FD_ZERO(&s); FD_SET(fd, &s);
        struct timeval tv = {3, 0};
        if (select(fd + 1, &s, NULL, NULL, &tv) <= 0) return -1;
        ssize_t k = read(fd, b, n);
        if (k <= 0) return -1;
        b += k; n -= (size_t)k;
    }
    return 0;
}

static int xfer(const uint8_t *cmd, size_t n, uint8_t *resp, size_t want) {
    uint8_t hdr[2];
    if (wr(cmd, n) || rd(hdr, 2) || hdr[0] != 0xA5 || hdr[1] != 0) return -1;
    return want ? rd(resp, want) : 0;
}

static void le16(uint8_t *p, uint32_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }

static int tte_run(tt_io_t *io, const uint16_t *words, uint32_t n, uint8_t *uo) {
    (void)io;
    static uint8_t buf[5 + 2 * TTE_DEPTH], cap[3 * TTE_DEPTH];
    for (uint32_t off = 0; off < n; off += TTE_DEPTH) {
        uint32_t m = n - off < TTE_DEPTH ? n - off : TTE_DEPTH;
        buf[0] = OP_LOAD; le16(buf + 1, 0); le16(buf + 3, m);
        for (uint32_t i = 0; i < m; i++) le16(buf + 5 + 2 * i, words[off + i]);
        if (xfer(buf, 5 + 2 * m, NULL, 0)) return -1;
        buf[0] = OP_RUN; le16(buf + 1, m);
        if (xfer(buf, 3, NULL, 0)) return -1;
        buf[0] = OP_READ_CAP; le16(buf + 1, 0); le16(buf + 3, m);
        if (xfer(buf, 5, cap, 3 * m)) return -1;
        for (uint32_t i = 0; i < m; i++) uo[off + i] = cap[3 * i];   // uo_out, uio_out, uio_oe
    }
    return 0;
}

static int tte_reset(tt_io_t *io, uint32_t cycles) {
    (void)io;
    uint8_t b[5] = {OP_SET_IN, 0, 0};
    if (xfer(b, 3, NULL, 0)) return -1;
    b[0] = OP_RESET; le16(b + 1, cycles);
    return xfer(b, 3, NULL, 0);
}

static int tte_open(const char *dev, uint32_t div) {
    fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) { fprintf(stderr, "%s: %s\n", dev, strerror(errno)); return -1; }
    struct termios t;
    tcgetattr(fd, &t);
    cfmakeraw(&t);
    cfsetispeed(&t, B1000000);
    cfsetospeed(&t, B1000000);
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &t);
    usleep(200000);                     // let the FPGA drop any half command (155 ms timeout), then flush
    tcflush(fd, TCIOFLUSH);
    uint8_t ping = OP_PING, magic[4];
    if (xfer(&ping, 1, magic, 4) || memcmp(magic, "TTE1", 4)) { fprintf(stderr, "%s: no TTE1 device\n", dev); return -1; }
    uint8_t d[2] = {OP_SET_DIV, (uint8_t)div};
    if (xfer(d, 2, NULL, 0)) return -1;
    chip_hz = 27000000u / (2 * (div ? div : 1));
    return 0;
}

// ---------------- main ----------------
int main(int argc, char **argv) {
    static ntt_chip_t chip;
    static swchip_t sw;
    tt_io_t io;
    const char *port = NULL;
    uint32_t div = 1;
    const char *log = NULL;
    int a = 1;
    for (; a < argc && argv[a][0] == '-'; a++) {
        if (!strcmp(argv[a], "--port") && a + 1 < argc) port = argv[++a];
        else if (!strcmp(argv[a], "--div") && a + 1 < argc) div = (uint32_t)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--sw")) port = NULL;
        else if (!strcmp(argv[a], "--log") && a + 1 < argc) log = argv[++a];
        else { fprintf(stderr, "unknown option %s\n", argv[a]); return 2; }
    }
    if (port) {
        if (tte_open(port, div)) return 2;
        io = (tt_io_t){.run = tte_run, .reset = tte_reset, .name = "Tang Nano (TTE1)"};
    } else {
        swchip_init(&sw);
        io = swchip_io(&sw);
        chip_hz = 50000000;             // report chip times as if at 50 MHz
    }
    ntt_chip_init(&chip, &io);
    ntt_chip_reset(&chip);
    const char *cmd = a < argc ? argv[a] : "all";
    uint32_t arg = a + 1 < argc ? (uint32_t)strtoul(argv[a + 1], NULL, 0) : 0;
    int fail = 0;
    if (!strcmp(cmd, "selftest")) fail = app_selftest(&chip);
    else if (!strcmp(cmd, "demo")) fail = app_demo(&chip, arg ? arg : 1);
    else if (!strcmp(cmd, "fuzz")) fail = app_fuzz(&chip, arg ? arg : 10, 1);
    else if (!strcmp(cmd, "matvec")) fail = app_matvec(&chip, arg ? arg : 1);
    else if (!strcmp(cmd, "bench")) fail = app_bench(&chip, arg ? arg : 5, 50000000);
    else if (!strcmp(cmd, "cal")) fail = app_calibrate(&chip);
    else if (!strcmp(cmd, "ci")) fail = app_ci(&chip);
    else if (!strcmp(cmd, "all")) {
        fail |= app_selftest(&chip);
        fail |= app_demo(&chip, 1);
        fail |= app_fuzz(&chip, 10, 1);
        fail |= app_matvec(&chip, 1);
    } else { fprintf(stderr, "unknown command %s\n", cmd); return 2; }
    if (log && !strcmp(log, "csv")) oplog_csv();
    else if (log) oplog_summary(50000000);
    if (!port && sw.violations) {
        printf("software chip saw %lu protocol violations, last: %s\n", (unsigned long)sw.violations, sw.last_violation);
        fail = 1;
    }
    printf("%s: %s, %llu chip clocks in all\n", cmd, fail ? "FAIL" : "PASS", (unsigned long long)chip.clocks_total);
    return fail ? 1 : 0;
}
