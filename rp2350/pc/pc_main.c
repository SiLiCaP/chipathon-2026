// SPDX-License-Identifier: Apache-2.0
// PC build of the RP2350 harness: the same core (job building, unpacking, tests, CI check) against
//   --sw           the software chip (default), or
//   --port DEV     a Tang Nano 20K running the chip RTL or its gate netlist (TTE1 protocol, fpga/PROTOCOL.md
//                  in tt-viktor-chiptor), so the C harness is proven on real logic before silicon exists.
//
//   make -C rp2350/pc && rp2350/pc/hash_host [--sw | --port /dev/ttyUSB1] all | selftest | fuzz [n] | ci | cal
//                  | misuse (the software chip flags each command rule of docs/info.md)
//   --dump-words FILE   write every pin word played and every uo_out and uio[1:0] sampled (HWD2 file,
//                       test/test_driver_stream.py replays it through the RTL and compares the samples clock for clock)
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
#include "../core/hash_bench.h"
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

static int tte_run(tt_io_t *io, const uint16_t *words, uint32_t n, uint8_t *uo, uint8_t *aux) {
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
        for (uint32_t i = 0; i < m; i++) {                           // uo_out, uio_out, uio_oe
            uo[off + i] = cap[3 * i];
            if (aux) aux[off + i] = cap[3 * i + 1] & cap[3 * i + 2] & 3;
        }
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

// The emulator's uio inputs are a register behind the UART, not pins: nothing to let go of.
static int tte_release(tt_io_t *io, int release) { (void)io; (void)release; return 0; }

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
// HWD2 file: "HWD2", u32 n, n x u16 words, n x u8 uo_out samples, n x u8 uio[1:0] samples (what the chip drives
// there in framing mode 1, else 0), little endian. A clock stopped between transport calls is invisible to the
// chip, so one stream.
static FILE *dump;
static uint32_t dump_n;
static tt_io_t *dump_inner;
static tt_io_t dump_io;
static uint16_t *dump_w;
static uint8_t *dump_u, *dump_a;
static size_t dump_cap;

static int dump_run(tt_io_t *io, const uint16_t *words, uint32_t n, uint8_t *uo, uint8_t *aux) {
    (void)io;
    if (dump_n + n > dump_cap) {
        dump_cap = (dump_n + n) * 2;
        dump_w = realloc(dump_w, dump_cap * sizeof *dump_w);
        dump_u = realloc(dump_u, dump_cap);
        dump_a = realloc(dump_a, dump_cap);
        if (!dump_w || !dump_u || !dump_a) return -1;
    }
    // the chip's pins are recorded on every clock, whether the driver looks at them or not
    if (dump_inner->run(dump_inner, words, n, uo, dump_a + dump_n)) return -1;
    if (aux) memcpy(aux, dump_a + dump_n, n);
    memcpy(dump_w + dump_n, words, n * sizeof *words);
    memcpy(dump_u + dump_n, uo, n);
    dump_n += n;
    return 0;
}

static int dump_release(tt_io_t *io, int release) {
    (void)io;
    return dump_inner->uio_release(dump_inner, release);
}

static int dump_reset(tt_io_t *io, uint32_t cycles) {
    (void)io;
    if (dump_n) { fprintf(stderr, "--dump-words: a reset after the first clocks cannot be replayed\n"); return -1; }
    return dump_inner->reset(dump_inner, cycles);
}

static void dump_finish(void) {
    fwrite("HWD2", 1, 4, dump);
    uint8_t h[4] = {dump_n & 255, (dump_n >> 8) & 255, (dump_n >> 16) & 255, dump_n >> 24};
    fwrite(h, 1, 4, dump);
    for (uint32_t i = 0; i < dump_n; i++) { uint8_t b[2] = {dump_w[i] & 255, dump_w[i] >> 8}; fwrite(b, 1, 2, dump); }
    fwrite(dump_u, 1, dump_n, dump);
    fwrite(dump_a, 1, dump_n, dump);
    fclose(dump);
}

// ---------------- misuse: the software chip flags each command rule of docs/info.md ----------------
// The chip does not guard against these (a word during Permute / Clear still moves the byte pointer and the
// samplers, Burst n = 0 streams 256 bytes, the word after Mode is read in the old framing). The driver never sends
// them; the software chip must flag them, or `make test` would not notice a driver that does.
static uint16_t mw[600];
static uint32_t mn;
static void mput(uint16_t w) { mw[mn++] = w; }
static void midle(uint32_t k) { while (k--) mput(0); }

static int misuse_case(const char *what, int expect) {
    static hash_swchip_t s;
    static uint8_t uo[600];
    hash_swchip_init(&s);
    tt_io_t io = hash_swchip_io(&s);
    io.run(&io, mw, mn, uo, NULL);
    const int ok = expect ? s.violations != 0 : s.violations == 0;
    printf("  %-44s %s%s%s\n", what, ok ? "ok" : "FAIL", s.violations ? ": " : "", s.violations ? s.last_violation : "");
    mn = 0;
    return !ok;
}

static int app_misuse(void) {
    int fail = 0;
    printf("misuse: the software chip flags every command rule\n");
    mput(HC_WORD(HC_CLEAR, 0)); midle(HC_CLEAR_BUSY);                  // legal: no violation
    mput(HC_WORD(HC_ABSORB, 0x41)); mput(HC_WORD(HC_PERMUTE, 0)); midle(HC_PERM_BUSY);
    mput(HC_WORD(HC_BURST, 1)); midle(7 + HC_SQ_DELAY + 4);
    fail |= misuse_case("legal: clear, absorb, permute, burst", 0);
    mput(HC_WORD(HC_PERMUTE, 0)); midle(10); mput(HC_WORD(HC_ABSORB, 0x41)); midle(HC_PERM_BUSY);
    fail |= misuse_case("absorb during permute", 1);
    mput(HC_WORD(HC_PERMUTE, 0)); midle(10); mput(HC_WORD(HC_CLEAR, 0)); midle(HC_PERM_BUSY);
    fail |= misuse_case("clear during permute", 1);
    mput(HC_WORD(HC_BURST, 0)); midle(300);
    fail |= misuse_case("burst n = 0", 1);
    mput(HC_WORD(HC_BURST, 2)); midle(4); mput(HC_WORD(HC_SQUEEZE, 0)); midle(20);
    fail |= misuse_case("squeeze during a burst", 1);
    mput(HC_WORD(HC_EXT, HC_EXT_MODE | 1)); mput(HC_WORD1(HC_ABSORB, 0x41)); midle(4);
    fail |= misuse_case("a word right after mode", 1);
    printf("misuse: %s\n", fail ? "FAIL" : "PASS");
    return fail;
}

// ---------------- main ----------------
// the PC's clock for the benchmark: nanoseconds, low 32 bits (deltas only); the chip clock is the transport's
static uint32_t pc_cycles(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec);
}
static const bench_plat_t pc_plat = {.cycles = pc_cycles, .cyc_hz = 1000000000u, .host = "PC"};

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
        io = (tt_io_t){.run = tte_run, .reset = tte_reset, .uio_release = tte_release, .name = "Tang Nano (TTE1)"};
    } else {
        hash_swchip_init(&sw);
        io = hash_swchip_io(&sw);
    }
    if (dump_path) {
        dump = fopen(dump_path, "wb");
        if (!dump) { perror(dump_path); return 2; }
        dump_inner = &io;
        dump_io = (tt_io_t){.run = dump_run, .reset = dump_reset, .uio_release = dump_release, .name = io.name};
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
    else if (!strcmp(cmd, "bench") && !strcmp(argv[argc - 1], "compute")) fail = app_bench_compute(&chip, &pc_plat);
    else if (!strcmp(cmd, "bench"))
        fail = app_bench_style(&chip, &pc_plat, !strcmp(argv[argc - 1], "legacy") ? BENCH_LEGACY
                                                 : !strcmp(argv[argc - 1], "mode1") ? BENCH_MODE1 : BENCH_MODE0);
    else if (!strcmp(cmd, "speedtest")) fail = app_speedtest(&chip, &pc_plat, 0);
    else if (!strcmp(cmd, "misuse")) fail = app_misuse();
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
