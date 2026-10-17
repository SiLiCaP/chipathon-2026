# Spec 1: Pi Harness and Demo

Oct 17, 2026 · @A

## Your job

You build everything on the RP2350 side and the demo. You also own the reference model and the test vectors that the other four people test against.

- **In scope:** the PIO program, DMA, clock counting, job building, unpacking, the reference model, vector files and the demo.
- **Out of scope:** anything inside the chip.
- **Start with a software chip.** Write a model of the chip that follows the same pin protocol and fixed delays. Your harness can then be finished and tested before any RTL exists.

The handoff named an RP2040 or an FPGA as the host. This plan assumes an RP2350 and proposes the PIO and DMA split. Neither is fixed by the handoff.

## Chip protocol you must drive

The chip has no back-pressure and no output-valid flag, so you drive and read it on exact clocks.

| Pins | Function |
| --- | --- |
| `ui_in[7:0]`, `uio[3:0]` | 12-bit coefficient: bits 7:0, then bits 11:8 |
| `uio[4]` | valid |
| `uio[5]` | sof: with valid, the first coefficient of a job |
| `uio[7:6]` | op: 00 forward NTT, 01 inverse NTT, 10 basemul, 11 scale. Read on the sof clock only |
| `uo_out[7:0]` | Result byte at a fixed delay |

Rules:

- A job starts on a clock where valid and sof are both high. A new sof in the middle of a job cancels it and starts a new one.
- Every coefficient is 0..3328. Never send 3329 or more.
- The chip counts butterflies and picks constants itself. You only send coefficients in the right order.
- A job ends by itself after its last result.

Order of inputs for each mode:

| Mode | Order | Inputs | Bytes back |
| --- | --- | --- | --- |
| Forward NTT | Pairs, b first then a, where b = f\[j + len\] and a = f\[j\] | 1792 | 2688 |
| Inverse NTT | Same order as forward | 1792 | 2688 |
| Basemul | g1, f1, g0, f0 for each piece | 512 | 384 |
| Scale | Pairs c, c′ on consecutive clocks, one pair every 3 clocks | 256 | 384 |

What the output bytes mean. The chip multiplier returns 2197 × the product (2197 = 13³), so forward and inverse NTT outputs are plain, but basemul bytes carry 2197 × the plain basemul, and scale returns c × 3303 × 2197⁻¹ mod 3329. Run the whole flow (forward, forward, basemul, inverse, scale) and the final answer is correct. Do not compare basemul or scale output alone against the plain reference. Compare it against your chip-level model.

How an NTT job runs end to end:

- One job covers all 7 layers (1792 inputs = 7 layers × 256). The chip has no storage, so each layer streams in and its results come back before the next layer can use them.
- Send layer k + 1 only once the results it needs have returned. Keep `valid` low in between. The chip keeps its counters while it waits.
- Start layer by layer. Overlapping layers is an optimization for later.
- The ≈2700 clock figure is streaming time only. Expect extra clocks for the waits between layers.

**Hashing (Oct 18, 4x2 build).** The chip also has a Keccak-f[1600] sponge. A word with valid high and coefficient bits 11:8 = 0xF is a hash command: op 00 absorbs the data byte (bits 7:0) into the next state byte, 01 squeezes the next state byte onto `uo_out` 2 clocks later (1 capture + 1 core), 10 permutes (busy 368 clocks) and 11 clears the state (busy 8 clocks); permute and clear rewind the byte pointer. There is no busy flag: wait the busy clocks before the next hash command. The host does the SHA-3 / SHAKE padding and absorbs and squeezes whole 8-byte lanes. Hash bytes are ORed onto `uo_out`, so a hash job and an NTT job may share the pins as long as their output bytes never fall on the same clock (`test/test_hash.py` interleaves them).

To unpack results, three bytes b0, b1, b2 make two coefficients:

```
c0 = b0 | ((b1 & 0x0F) << 8)
c1 = (b1 >> 4) | (b2 << 4)
```

Example: bytes 0x23, 0x61, 0x45 give c0 = 0x123 and c1 = 0x456. Treat any result of 3329 or more as a hardware fault.

## Host design

The PIO owns the clocked pins, DMA moves the words, and firmware only orders the data. This split is a proposal. Change it if the PIO program needs something else.

- **Clock and pins in lockstep.** One PIO state machine drives `clk` and, on every chip clock, puts a 16-bit word on the input pins (12 coefficient bits, valid, sof, 2 op bits) and samples the 8 output pins. Every PIO tick is one chip clock, so clock counting is exact.
- **Input words.** One word per chip clock, including idle clocks with valid low. Use a repeat count or pre-built buffers so idle clocks do not double the DMA traffic.
- **Output samples.** Capture `uo_out` on every clock into SRAM by DMA. Firmware then picks out the real bytes at known clock positions and ignores the rest. A sampled 0x00 only means something at a scheduled position.
- **Start slow.** Run the chip clock slow (for example about 1 MHz, as a setting) until it works, then raise it. The chip is designed to meet 50 MHz at the slow corner (Spec 3, Timing closure). Bring it up at 50 MHz or below; the board's clock insertion delay (up to 10 ns) means the PIO sampling phase must be calibrated on the real board before going faster.
- **Library API.** Four calls, each taking and returning 256 coefficients: forward, inverse, basemul and scale. Every call checks that the outputs are below 3329.
- **Reset.** The RP2350 also drives `rst_n`. Hold it low for several clocks at start-up and after any fault.
- **Errors.** The chip has no timeout hook. If a result is out of range, abort with a new sof and report a fault.

Timing constants never appear in the code. They come from one file, described in the timing section.

## Reference model and test vectors

Your reference model is the truth for all five specs. It follows FIPS 203 Algorithms 9, 10, 11 and 12 exactly, plus the final multiply by 3303. You also write a chip-level model that mimics the multiplier exactly: three K-RED rounds (K-RED(x) = 13 × (x & 255) − (x >> 8)) and one final correction, so every product comes out as 2197 × the plain product mod 3329. Use scaled constants in that model: ROM′ = ROM × 2197⁻¹ and γ′ likewise, with 2197⁻¹ = 944, and the scale constant 304 = 3303 × 2197⁻². All pin-level vectors come from this model.

- **Language.** Python first, for vectors. Add a C version later if the RP2350 self-test needs it.
- **ROM and γ.** ROM\[k\] = 17^BitRev7(k) mod 3329 and γ\_i = 17^(2·BitRev7(i)+1) mod 3329. Export all 128 of each as files, in two versions: plain and scaled by 2197⁻¹ mod 3329 (the chip stores the scaled one). The Control owner checks `zeta_rom.v` against them.
- **Stimulus and expected output at pin level.** For each mode write `stim_<mode>.hex` (one 16-bit input word per chip clock, idle clocks included, following the Control owner's schedule) and `exp_<mode>.hex` (expected `uo_out` per clock, once the delays are known).
- **Unit-level files.** Small files for the multiplier (a, b, product), the adders (x, y, sum, diff, and accumulate sequences) and the packer (r0, r1, three bytes). The owners also run their own exhaustive tests.
- **Vector sets.** Random polynomials (10 seeds), all zeros, all 3328, the ramp f\[i\] = i, a single 1 at several positions, and cancel tests with a new sof at several points mid-job.
- **One folder** with a README that says how each file is produced and what format it has.

Known answers to reproduce. I ran a reference with the same algorithms. Forward NTT, then inverse NTT, then the 3303 scale returns the input. Forward, basemul, inverse and scale on two random polynomials matches a schoolbook product mod X^256 + 1.

| Check | Value |
| --- | --- |
| ROM\[1\], ROM\[127\] | 1729, 2154 |
| γ for pieces 0, 1, 127 | 17, 3312, 1175 |
| 128 × 3303 mod 3329 | 1 |
| Forward NTT of f\[i\] = i, outputs 0 to 7 | 2429, 2845, 425, 795, 1865, 1356, 624, 31 |
| Forward NTT of f\[i\] = i, outputs 248 to 255 | 2883, 149, 2594, 3105, 2502, 2134, 2717, 2303 |
| Inverse NTT of that, before the scale, outputs 0 to 3 | 0, 128, 256, 384 |
| After the 3303 scale, outputs 0 to 3 | 0, 1, 2, 3 |
| Scaled ROM′\[0\], \[1\], \[64\], \[127\] | 944, 966, 2732, 2686 |
| Scaled γ′ for pieces 0, 1, 127; scale constant | 2732, 597, 643; 304 |
| Basemul with f0 = 5, f1 = 7, g0 = 11, g1 = 13, γ = 17: plain answer, then chip pins | plain c0 = 1602, c1 = 142; pins c0 = 841, c1 = 2377 |
| Scale mode, input then output | 2197 → 3303, 3259 → 316, 1132 → 26 |

## Demo

One demo that uses all four modes: multiply two polynomials mod X^256 + 1 on the chip, then check the answer against a schoolbook multiply on the RP2350 CPU.

1. Make two random polynomials f and g with coefficients 0..3328.
2. Forward NTT of f, then forward NTT of g, on the chip.
3. Basemul of the two results on the chip.
4. Inverse NTT of that result on the chip.
5. Scale on the chip (constant 304, which nets out to 3303 once the 2197 from basemul is removed).
6. Multiply f and g on the CPU with the schoolbook method (negacyclic, mod 3329).
7. Compare. Print PASS or FAIL, the chip clock count for each step, and the CPU time.

Expected chip clocks, streaming only: three NTT jobs at about 2688 each, basemul about 640 and scale about 384, so about 9,090 in total, plus the waits between NTT layers. Print the measured count next to this estimate.

Two more parts:

- **Software chip first.** Run the demo against your software chip model before any RTL exists, then against Verilog simulation by replaying the vector files, then on hardware.
- **Stretch goal.** A matrix-vector product for k = 2, using the chip for every NTT and basemul and the CPU for the sums across polynomials. This is the shape of the work inside ML-KEM, and it shows how much of the time goes to basemul.

The demo needs no SHAKE, sampling or encoding.

## Timing parameters to fill in

All output delays are assumptions until RTL has been simulated. Keep every timing number in one file (`timing.h` and `timing.py`) so a change is made once.

| Constant | Meaning | Value now | Source of the final value |
| --- | --- | --- | --- |
| `D_F` | Clocks from a forward pair's first input to its first byte | 6 | Control owner's `host_timing.md` |
| `D_I` | Same, inverse | 8 | Same |
| `D_B` | Same, basemul | 12 | Same |
| `D_S` | Same, scale (from the first coefficient of a pair) | 7 | Same |
| `P_F`, `P_I` | Clocks per butterfly | 3 | Handoff |
| `P_B` | Clocks per basemul piece | 5 (g1, f1, g0, f0, then one idle clock) | Spec 3 schedule |
| `P_S` | Clocks per scale pair | 3 (c, c′, then one idle clock) | Spec 3 schedule |
| Input offsets | Clock offset of every input within a unit | see the schedule | Control owner's `schedule.md` |
| `CHIP_CLK` | Chip clock setting | slow at first | You |

Rule: no other file may hardcode a delay or a period.

## Deliverables and done when

**Hand over:**

- The Python reference model (NTT, inverse NTT, basemul, scale, ByteEncode12 packing, the ROM and γ tables)
- Test vector files for every block owner, in the formats they ask for
- A software chip that mimics the pins and the fixed output delay
- Harness firmware for the RP2350 (PIO, DMA, host ordering and unpacking)
- The demo: a full polynomial multiply on the chip, compared with schoolbook

**Done when:**

- The reference model passes every worked vector in the other four specs.
- The software chip runs the whole demo with the constants in `timing.py`.
- Real hardware or an FPGA run matches the software chip bit for bit.

**Do not:** hardcode a delay or a period outside `timing.h` and `timing.py`.

## Shared rules for all five specs

- **One chip, one clock.** Everything runs on `clk`. Reset is `rst_n`, active low. Synchronous logic only, no latches, no generated clocks.
- **Numbers.** Every coefficient is 12 bits, unsigned, always in 0..3328 on every port.
- **Ports are frozen.** Tell the Control owner before changing a port name or width.
- **Latency is reported.** The Control owner sends you `host_timing.md`; load it into `timing.h` and `timing.py`.
- **Reference.** Your Python model is the source of truth for everyone else. Keep it simple and readable.
- **Sharing.** These docs are private. Share them with your teammates.
