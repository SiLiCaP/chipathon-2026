# Re-hardening the hash_io busy fix (Nov 25-26, 2026)

**Status: not merged (decided Nov 26, 2026).** The RTL fix stays on branch `fix/hash-io-busy`. With the chip's
config it does not route; the one config that routes lowers the hold margin (0.14 ns), which the team does not
want on silicon. `main` keeps the old RTL (option 2 below): the misuse rules are in the datasheet
([info.md](../info.md)), the firmware obeys them, and its software chip model reports any break of them.

Still relevant for `main`: section 2. The current layout routes with almost no slack in routing resources, and
small netlist changes lose it.

## 1. The RTL change

A review found three problems in `src/hash_io.v`:

1. **A command while the core is busy changed the adapter's state.** The core ignored the word, but the adapter
   still acted on it. Examples: ABSORB moved the byte pointer, CLEAR restarted the CBD / SampleNTT sampler,
   SQUEEZE asked for a byte.
2. **BURST / BURST_CBD with n = 0 streamed 256 bytes.**
3. **The word right after MODE is read in the old framing.**

The fixes, in commits `27ed880` and `7936cea`:

```verilog
wire hv    = cmd_valid & ~(c_busy | bursting | raw_on | fin_on | ap_on);
wire start = hv & ((cmd == BURST) | (cmd == BURST_CBD)) & (din[4:0] != 5'd0);   // n = 0: a no-op
```

- **The SampleNTT tail is not in the gate.** That is the up to 3 clocks after a SampleNTT burst while its last
  bytes leave. A first version gated it too, and test_sample_ntt failed: the firmware and testbench send the next
  PERMUTE there.
- **The MODE rule is documented, not changed.** The firmware and the testbench already send 3 idle words after
  MODE.
- **The software chip model matches.** `rp2350/core/hash_swchip.c` treats n = 0 as a no-op.

**Verification of the RTL:**

| Check | Result |
|---|---|
| RTL suite, K = 8 | 13/13 |
| New misuse tests: `test_busy_absorb`, `test_busy_clear`, `test_busy_squeeze`, `test_burst_zero` | Pass; all 4 fail on the old RTL |
| PC firmware tests (selftest, fuzz, golden) | Pass |
| `make -C test rp2350`, the driver's pin streams through the RTL | 325,848 + 57,727 clocks identical |
| Synthesis | 4,586 cells, 53,493 µm² (old: 4,610 cells, 53,595 µm²) |

## 2. The problem: the fixed RTL does not route at the default hold margin

Every build used LibreLane through `tt_tool.py --harden`, 3x2 tiles, K = 8, the chip's `src/config.json` plus the
one change listed. "Overflow" is the global router's total overflow; detailed routing needs it near 0.

| Run | RTL | Change from the chip's config | GR overflow | GR wire (µm) | Hold buffers | Outcome |
|---|---|---|---|---|---|---|
| `main_3x2` | old (main) | none | **1** | 389,456 | 1,416 | **Clean.** 0 DRC / LVS / antenna, 74.1 % |
| `exp_old` | old, rebuilt tonight | none | **1** | 389,456 | 1,416 | Identical to the build above: the tools are deterministic |
| `exp_cbusy` | busy gate only | none | 4,948 | 471,497 | 1,422 | Congested |
| `exp_n0` | n = 0 only | none | 1,852 | 451,549 | 1,415 | Congested |
| `local_main_3x2` | fix | none | 3,947 | 470,221 | 1,420 | Detailed routing stuck at about 46,600 violations for over an hour; stopped |
| `local_main_d72` | fix | density 72 % | 4,810 | 481,488 | 1,421 | Congested |
| `fix_d74` | fix | density 74 % | 4,064 | 477,783 | 1,423 | Congested |
| `fix_d76` | fix | density 76 % | 4,971 | 469,213 | 1,416 | Detailed routing went up: 61k to 71k violations |
| `fix_d77` | fix | density 77 % | 4,773 | 473,967 | 1,419 | Detailed routing went up: 68k to 81k violations |
| `local_main_d78` | fix | density 78 % | 9,418 | 524,275 | 1,416 | Congested |
| `local_main_d80` | fix | density 80 % | 3,821 | 459,560 | 1,420 | Congested |
| `fix_d70` | fix | density 70 % | 2,945 | 452,715 | 1,415 | The least congested; paused in detailed routing |
| `fix_nondr` | fix | `CTS_APPLY_NDR: "none"` | 3,896 | 475,596 | 1,420 | Congested; detailed routing went up, 48k to 58k |
| `fix_pad1` | fix | `GPL_CELL_PADDING: 1` | 3,947 | 470,221 | 1,420 | Bit-identical to no change |
| `fix_dpad1` | fix | `DPL_CELL_PADDING: 1` | 3,947 | 470,221 | 1,420 | Bit-identical to no change |
| `fix_dpad1_d70` | fix | `DPL_CELL_PADDING: 1`, density 70 % | 2,945 | 452,715 | 1,415 | Identical to `fix_d70` |
| `fix_rovf` | fix | `PL_ROUTABILITY_OVERFLOW_THRESHOLD: 0.3` | 3,947 | 470,221 | 1,420 | Bit-identical to no change |
| `fix_tdrv` | fix | `PL_TIMING_DRIVEN: true` | 14,686 | 587,397 | 1,408 | The worst |
| `fix_grthold` | fix | hold repair after global routing (`RUN_POST_CTS_RESIZER_TIMING: false`, `RUN_POST_GRT_RESIZER_TIMING: true`) | 0, then **3,964** after its hold repair | 310k, then 468k | about 1,400 | Congested once the buffers go in; paused |
| `fix_grthold_d70` | fix | the same, density 70 % | 0, then **2,807** | 308k, then 448k | about 1,400 | The same; paused |
| **`fix_h14`** | fix | **hold uncertainty 0.14 ns** (custom SDC) | **0** | **301,509** | **11** | **Clean:** see section 3 |

**What the runs show:**

- **The two RTL changes are not the problem in themselves.** Even the 5-bit `n != 0` compare alone congests the
  layout. The old RTL's clean route was a lucky layout. Almost any netlist change loses it.
- **The roughly 1,420 hold buffers are the problem.** They come from TT's default hold uncertainty of 0.25 ns, and
  most likely sit on the keccak ring's direct flop-to-flop shift paths.
  - The post-CTS repair inserts them into a placement that is already full. In the new builds, placement wire
    length jumps from about 161k to 297k µm in that step; in the old build it went from 181k to 230k µm.
  - Global routing then congests, mostly around the gated clock nets: 18 of them lost their wide spacing (NDR),
    against 3 in the old build.
- **Moving the hold repair after global routing does not help.** The same buffers congest the routed design instead.
- **Without most of the hold buffers the chip routes easily:** 48.6 % routing usage, against 61 % for the old chip.

## 3. The build that routes: `fix_h14`

The chip's `config.json` plus `PNR_SDC_FILE` and `SIGNOFF_SDC_FILE` set to a copy of TT's base SDC with
`set_clock_uncertainty -hold 0.14`. Setup uncertainty is unchanged. This is the custom SDC the team used before
Nov 23, 2026.

| Sign-off metric | Old chip (main, 0.25 ns) | `fix_h14` (0.14 ns) |
|---|---|---|
| Magic DRC / LVS / antenna | 0 / 0 / 0 | 0 / 0 / 0 |
| Detailed-routing violations | 0 | 0 |
| Utilization | 74.1 % | **61.4 %** |
| Hold buffers | 1,416 | **11** |
| Setup worst slack, ss 100 °C 1.60 V | +6.35 ns | +6.09 ns |
| Hold worst slack, all corners (against its own uncertainty) | +0.036 ns | +0.019 ns |
| Hold worst slack, ff -40 °C 1.95 V | +0.040 ns | +0.025 ns |
| Hold worst slack, tt 25 °C 1.80 V | +0.196 ns | +0.128 ns |
| Hold margin beyond zero, worst corner (uncertainty + slack) | **about 0.29 ns** | **about 0.16 ns** |
| Max-slew / max-cap warnings | 2,047 / 7 | 1,841 / 10 |
| Harden time | about 20 min | 15.4 min |

**The trade-off:**
- The fixed RTL gains the misuse protection.
- The worst-case hold margin on silicon goes from about 0.29 ns to about 0.16 ns, still positive in every corner.
- The paths that got the 1,400 buffers are same-edge flop-to-flop paths, where the skew is already in the
  post-CTS timing. The 0.25 ns default also covers jitter, which a same-edge hold check does not see.

## 4. Options

1. **The fix with the 0.14 ns hold SDC.** It routes, it is clean, and it has more free area. The cost is less hold
   margin than TT's default.
2. **Keep main as it is.** Ship the old RTL and document the three misuse rules: no hash words while busy, no
   BURST with n = 0, an idle word after MODE. The firmware already obeys all three, and the checks above show it.
3. **Keep looking for a default-margin route.** Possible levers:
   - restructure the ring's shift paths so they don't need hold buffers;
   - try other flow settings.

   The three paused runs (`fix_d70`, `fix_grthold`, `fix_grthold_d70`) resume at 08:15 on Nov 26, but every sign
   so far says they stay congested.

## 5. Reproducing

- **The script:** `tools/tile_exp.py` in tt-viktor-chiptor. Each experiment is a git ref of chipathon-2026 plus
  config edits; the names in the table are its experiment names.
- **The builds:** in `~/tools/work/exp/<name>/runs/wokwi/` on the build PC. Global routing is step
  `39-openroad-globalrouting`, the post-CTS resizer `37-openroad-resizertimingpostcts`, sign-off `final/metrics.csv`.
- **CI warning:** pushing this branch makes the host CI harden it with the chip's current config. Expect that
  harden to congest, as in section 2.
