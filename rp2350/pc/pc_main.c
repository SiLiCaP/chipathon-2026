// SPDX-License-Identifier: Apache-2.0
// PC build of the RP2350 harness: the same core (job building, unpacking, tests, CI check) against
//   --sw           the software chip (default), or
//   --port DEV     a Tang Nano 20K running the chip RTL or its gate netlist (TTE1 protocol, fpga/PROTOCOL.md
//                  in tt-viktor-chiptor), so the C harness is proven on real logic before silicon exists.
//
//   make -C rp2350/pc && rp2350/pc/hash_host [--sw | --port /dev/ttyUSB1] all | selftest | fuzz [n] | ci | cal
//   --dump-words FILE   write every pin word played and every uo_out sampled (HWD1 file, test/test_driver_stream.py
//                       replays it through the RTL and compares the samples clock for clock)
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef B1000000
#define B1000000 1000000       // macOS has no named 1 Mbaud constant; its speed_t is the number
#endif

#include "../core/ci_app.h"
#include "../core/hash_app.h"
#include "../core/hash_swchip.h"

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
    return 0;
}

// ---------------- word dump ----------------
// HWD1 file: "HWD1", u32 n, n x u16 words, n x u8 uo_out samples (little endian). Appended to as the chip runs, the
// header is patched at the end. A clock stopped between transport calls is invisible to the chip, so one stream.
static FILE *dump;
static uint32_t dump_n;
static tt_io_t *dump_inner;
static tt_io_t dump_io;
static uint16_t *dump_w;
static uint8_t *dump_u;
static size_t dump_cap;

static int dump_run(tt_io_t *io, const uint16_t *words, uint32_t n, uint8_t *uo) {
    (void)io;
    if (dump_inner->run(dump_inner, words, n, uo)) return -1;
    if (dump_n + n > dump_cap) {
        dump_cap = (dump_n + n) * 2;
        dump_w = realloc(dump_w, dump_cap * sizeof *dump_w);
        dump_u = realloc(dump_u, dump_cap);
        if (!dump_w || !dump_u) return -1;
    }
    memcpy(dump_w + dump_n, words, n * sizeof *words);
    memcpy(dump_u + dump_n, uo, n);
    dump_n += n;
    return 0;
}

static int dump_reset(tt_io_t *io, uint32_t cycles) {
    (void)io;
    if (dump_n) { fprintf(stderr, "--dump-words: a reset after the first clocks cannot be replayed\n"); return -1; }
    return dump_inner->reset(dump_inner, cycles);
}

static void dump_finish(void) {
    fwrite("HWD1", 1, 4, dump);
    uint8_t h[4] = {dump_n & 255, (dump_n >> 8) & 255, (dump_n >> 16) & 255, dump_n >> 24};
    fwrite(h, 1, 4, dump);
    for (uint32_t i = 0; i < dump_n; i++) { uint8_t b[2] = {dump_w[i] & 255, dump_w[i] >> 8}; fwrite(b, 1, 2, dump); }
    fwrite(dump_u, 1, dump_n, dump);
    fclose(dump);
}

// ---------------- main ----------------
int main(int argc, char **argv) {
    static hash_chip_t chip;
    static hash_swchip_t sw;
    tt_io_t io;
    const char *port = NULL, *dump_path = NULL;
    uint32_t div = 1;
    int a = 1;
    for (; a < argc && argv[a][0] == '-'; a++) {
        if (!strcmp(argv[a], "--port") && a + 1 < argc) port = argv[++a];
        else if (!strcmp(argv[a], "--div") && a + 1 < argc) div = (uint32_t)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--sw")) port = NULL;
        else if (!strcmp(argv[a], "--dump-words") && a + 1 < argc) dump_path = argv[++a];
        else { fprintf(stderr, "unknown option %s\n", argv[a]); return 2; }
    }
    if (port) {
        if (tte_open(port, div)) return 2;
        io = (tt_io_t){.run = tte_run, .reset = tte_reset, .name = "Tang Nano (TTE1)"};
    } else {
        hash_swchip_init(&sw);
        io = hash_swchip_io(&sw);
    }
    if (dump_path) {
        dump = fopen(dump_path, "wb");
        if (!dump) { perror(dump_path); return 2; }
        dump_inner = &io;
        dump_io = (tt_io_t){.run = dump_run, .reset = dump_reset, .name = io.name};
        hash_chip_init(&chip, &dump_io);
    } else {
        hash_chip_init(&chip, &io);
    }
    hash_chip_reset(&chip);
    const char *cmd = a < argc ? argv[a] : "all";
    uint32_t arg = a + 1 < argc ? (uint32_t)strtoul(argv[a + 1], NULL, 0) : 0;
    int fail = 0;
    if (!strcmp(cmd, "selftest")) fail = app_selftest(&chip);
    else if (!strcmp(cmd, "fuzz")) fail = app_fuzz(&chip, arg ? arg : 10, 1);
    else if (!strcmp(cmd, "cal")) fail = app_calibrate(&chip);
    else if (!strcmp(cmd, "ci")) fail = app_ci(&chip);
    else if (!strcmp(cmd, "all")) {
        fail |= app_selftest(&chip);
        fail |= app_fuzz(&chip, 10, 1);
    } else { fprintf(stderr, "unknown command %s\n", cmd); return 2; }
    if (dump) dump_finish();
    if (!port && sw.violations) {
        printf("software chip saw %lu protocol violations, last: %s\n", (unsigned long)sw.violations, sw.last_violation);
        fail = 1;
    }
    printf("%s: %s, %llu chip clocks in all\n", cmd, fail ? "FAIL" : "PASS", (unsigned long long)chip.clocks_total);
    return fail ? 1 : 0;
}
