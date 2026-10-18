// SPDX-License-Identifier: Apache-2.0
// Operation timing log: every chip batch (one NTT layer, one basemul job, one scale job) and every call,
// timed in CPU cycles of the host running the harness, so the host's real cost can be separated from the
// chip's clock-bound streaming time and the result re-predicted at any chip clock.
//
//   prep   build the input words (CPU)
//   run    transport: PIO + DMA streaming on the RP2350, or USB + FPGA on a PC; wall time
//   conv   the part of run a transport spends reformatting words (only the Pico 2 W + Tang Nano rig: its
//          PIO pin window has a gap; the demo board needs no conversion)
//   post   unpack, range check, write back (CPU)
//   call   the whole call, entry to return (adds the per-call work around the batches)
//
// Prediction for a chip clock f: keep every measured CPU part, drop conv, and replace the streaming time
// (words / chip clock now) with words / f. The DMA/PIO start and finish overhead measured on the rig stays.
#ifndef OPLOG_H
#define OPLOG_H

#include <stdint.h>

// Supplied by the platform (rp/main.c, pc/pc_main.c)
uint32_t plat_cycles(void);        // free-running cycle counter (wraps; deltas only)
uint32_t plat_cycles_hz(void);     // its rate: the CPU clock on the RP2350, 1e9 (ns) on a PC
uint32_t plat_chip_hz(void);       // the chip clock right now, 0 if unknown (software chip)

enum { OPLOG_BATCH = 0, OPLOG_CALL = 1 };

typedef struct {
    uint8_t kind, op, batch, skew;
    uint16_t units;
    uint32_t words;             // chip clocks streamed (call: of the whole call)
    uint32_t prep, run, conv, post;
    uint32_t total;             // call only
    uint32_t chip_hz;
} oplog_rec_t;

#define OPLOG_N 1024            // ring (~36 kB); the oldest records are overwritten

void oplog_batch(int op, int batch, int units, uint32_t words, uint32_t prep, uint32_t run, uint32_t conv,
                 uint32_t post, int skew);
void oplog_call(int op, uint32_t words, uint32_t total);
void oplog_clear(void);
uint32_t oplog_count(void);

// Print every record as CSV (header first), for tools/rp_log.py in tt-viktor-chiptor.
void oplog_csv(void);
// Per mode: measured time per call split into its parts, and the predicted time per call at target_hz.
void oplog_summary(uint32_t target_hz);

#endif
