// SPDX-License-Identifier: Apache-2.0
// Operation timing log. See oplog.h.
#include "oplog.h"

#include <stdio.h>
#include <string.h>

static oplog_rec_t ring[OPLOG_N];
static uint32_t head, count;

static const char *const OP_NAME[4] = {"forward", "inverse", "basemul", "scale"};

static void put(const oplog_rec_t *r) {
    ring[head] = *r;
    head = (head + 1) % OPLOG_N;
    if (count < OPLOG_N) count++;
}

void oplog_batch(int op, int batch, int units, uint32_t words, uint32_t prep, uint32_t run, uint32_t conv,
                 uint32_t post, int skew) {
    oplog_rec_t r = {.kind = OPLOG_BATCH, .op = (uint8_t)op, .batch = (uint8_t)batch, .skew = (uint8_t)skew,
                     .units = (uint16_t)units, .words = words, .prep = prep, .run = run, .conv = conv,
                     .post = post, .chip_hz = plat_chip_hz()};
    put(&r);
}

void oplog_call(int op, uint32_t words, uint32_t total) {
    oplog_rec_t r = {.kind = OPLOG_CALL, .op = (uint8_t)op, .words = words, .total = total,
                     .chip_hz = plat_chip_hz()};
    put(&r);
}

void oplog_clear(void) { head = count = 0; }
uint32_t oplog_count(void) { return count; }

static const oplog_rec_t *at(uint32_t i) { return &ring[(head + OPLOG_N - count + i) % OPLOG_N]; }

void oplog_csv(void) {
    printf("kind,op,batch,units,words,prep_cyc,run_cyc,conv_cyc,post_cyc,total_cyc,chip_hz,cpu_hz,skew\n");
    const uint32_t hz = plat_cycles_hz();
    for (uint32_t i = 0; i < count; i++) {
        const oplog_rec_t *r = at(i);
        printf("%s,%s,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%u\n", r->kind ? "call" : "batch", OP_NAME[r->op & 3],
               r->batch, r->units, (unsigned long)r->words, (unsigned long)r->prep, (unsigned long)r->run,
               (unsigned long)r->conv, (unsigned long)r->post, (unsigned long)r->total, (unsigned long)r->chip_hz,
               (unsigned long)hz, r->skew);
    }
}

void oplog_summary(uint32_t target_hz) {
    const double cyc_us = 1e6 / plat_cycles_hz();
    struct {
        uint32_t calls, batches;
        double words, total, prep, run, conv, post, stream, ovh;
    } s[4];
    memset(s, 0, sizeof s);
    // A call record closes the batches before it. Once the ring has wrapped, the oldest call may have lost
    // some of its batches: start after the first call record.
    uint32_t first = 0;
    if (count == OPLOG_N)
        while (first < count && at(first)->kind != OPLOG_CALL) first++;
    if (count == OPLOG_N) first++;
    for (uint32_t i = first; i < count; i++) {
        const oplog_rec_t *r = at(i);
        const int op = r->op & 3;
        if (r->kind == OPLOG_CALL) {
            s[op].calls++;
            s[op].total += r->total;
            s[op].words += r->words;
            continue;
        }
        s[op].batches++;
        s[op].prep += r->prep;
        s[op].run += r->run;
        s[op].conv += r->conv;
        s[op].post += r->post;
        if (r->chip_hz) {
            double stream = (double)r->words * plat_cycles_hz() / r->chip_hz;   // clock-bound part, cycles
            double ovh = (double)r->run - r->conv - stream;                     // DMA/PIO (or USB) overhead
            s[op].stream += stream;
            s[op].ovh += ovh > 0 ? ovh : 0;
        }
    }
    printf("per call, us        calls  clocks    total    prep     run   (stream  overhead  conv)   post  other"
           "  -> at %.1f MHz\n", target_hz / 1e6);
    for (int op = 0; op < 4; op++) {
        if (!s[op].calls) continue;
        const double n = s[op].calls;
        const double other = (s[op].total - s[op].prep - s[op].run - s[op].post) / n;   // call-level CPU work
        const double pred = (s[op].prep + s[op].post + s[op].ovh) / n + other + (s[op].words / n) * plat_cycles_hz() / target_hz;
        printf("  %-16s %6lu %7.0f %8.1f %7.1f %7.1f  (%7.1f %8.1f %5.1f) %6.1f %6.1f  -> %8.1f\n", OP_NAME[op],
               (unsigned long)s[op].calls, s[op].words / n, s[op].total / n * cyc_us, s[op].prep / n * cyc_us,
               s[op].run / n * cyc_us, s[op].stream / n * cyc_us, s[op].ovh / n * cyc_us, s[op].conv / n * cyc_us,
               s[op].post / n * cyc_us, other * cyc_us, pred * cyc_us);
    }
    printf("prediction = measured CPU work (prep, post, other) + measured transfer overhead + clocks / %.1f MHz;"
           " conv (rig only) dropped\n", target_hz / 1e6);
}
