Hi! We're hardening a 3x2 project for TTSKY26d (an NTT core plus a Keccak/SHA-3 core for ML-KEM) and have two questions about non-default flow settings before we commit to them.

**1. Integrated clock gates.** The Keccak core rotates its state through shift rings, and each ring is clocked through its own `sky130_fd_sc_hd__dlclkp` cell (47 of them). Everything else runs on the single `clk`. CTS builds through the gates, and gate-level sim and precheck pass. Is clock gating with `dlclkp` OK on TT, or is there a rule or known issue against it?

**2. Custom SDC with a smaller hold uncertainty.** The state is about 1,600 flip-flops in direct Q→D shift hops. The default SDC uses 0.25 ns uncertainty for setup and for hold. With that, the resizer adds a hold buffer on almost every hop (1,400+ buffers) and the design doesn't route in 3x2. So we use the default SDC with one extra line, set via `PNR_SDC_FILE` and `SIGNOFF_SDC_FILE`:

```
set_clock_uncertainty -hold 0.14 $clocks
```

Setup stays at 0.25 ns and the 5 % timing derate is unchanged. Hold margins are `PL_RESIZER_HOLD_SLACK_MARGIN` 0.03 and `GRT_RESIZER_HOLD_SLACK_MARGIN` 0.02. In our signed-off test layout, the worst hold path at ff/−40 °C/1.95 V has about 0.16 ns of slack with no uncertainty applied (+0.016 ns with the 0.14 applied).

- Is a custom signoff SDC accepted for tapeout?
- Does anything later in the TT flow re-time the project with the default constraints (chip-level STA, for example)?
- Does 0.14 ns sound reasonable for same-edge hold on sky130, or do you know of silicon results that argue for more?

We also use a `SYNTH_EXCLUDED_CELL_FILE` that allows the small `_1` cells, and we exclude the `clkdlybuf4s*` delay cells through `EXTRA_EXCLUDED_CELLS`. Let us know if either of those is a problem.

Thanks!

---

Answer (TT support, Oct 18):

> You can use sky130_fd_sc_hd__dlclkp cells if you'd like.
> You can use any SDC you like.
> Submissions are based on the GDS, we almost never modify them.
> I don't know of any silicon measurement that would argue for either more/less.
>
> (We very rarely have to modify submitted GDS, almost exclusively analog ones that fail an obscure DRC rule not in the OSS decks, and we get in touch if that's needed.)
