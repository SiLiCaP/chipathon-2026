![](../../workflows/gds/badge.svg) ![](../../workflows/docs/badge.svg) ![](../../workflows/test/badge.svg) ![](../../workflows/fpga/badge.svg)

# Specs and tools
Specs for the five parts of the chip (the source of truth for every port and timing):
- [Spec 1: Pi harness and demo](specs/Spec%201%20Pi%20Harness%20and%20Demo.md)
- [Spec 2: Multiplier](specs/Spec%202%20Multiplier.md)
- [Spec 3: Control loop and integration](specs/Spec%203%20Control%20Loop%20and%20Integration.md)
- [Spec 4: Adders](specs/Spec%204%20Adders.md)
- [Spec 5: I/O](specs/Spec%205%20I%20O.md)

Control and integration: [ntt_core](docs/control.md), [interface table](docs/interface.md),
[per-mode schedule](docs/schedule.md), [host timing](docs/host_timing.md).
Adders: [modq_addsub and modq_acc](docs/adders.md).
RP2350 harness and demo (Spec 1): [rp2350/](rp2350/README.md), firmware for the demo board (PIO + DMA) plus the same C code on a PC against a software chip or the Tang Nano FPGA.
Why SHA-3 sets ML-KEM's speed: [docs/sha3_report.md](docs/sha3_report.md). Tile options: [docs/architecture_options.md](docs/architecture_options.md).

[Local CI](local-ci/README.md): `local-ci/run` runs the GitHub workflows (test, docs, gds,
precheck, gate-level test) on your machine with Docker and shows the results on a local page.

# Chipathon links
Tasks:
https://tinytapeout.com/

Siliwiz:
https://app.siliwiz.com/

Tinytapout library:
https://tinytapeout.github.io/tinytapeout-sky-26c/datasheet.pdf


# The project
https://wokwi.com/projects/354858054593504257

# Tiny Tapeout Verilog Project Template

- [Read the documentation for project](docs/info.md)

## What is Tiny Tapeout?

Tiny Tapeout is an educational project that aims to make it easier and cheaper than ever to get your digital and analog designs manufactured on a real chip.

To learn more and get started, visit https://tinytapeout.com.

## Set up your Verilog project

1. Add your Verilog files to the `src` folder.
2. Edit the [info.yaml](info.yaml) and update information about your project, paying special attention to the `source_files` and `top_module` properties. If you are upgrading an existing Tiny Tapeout project, check out our [online info.yaml migration tool](https://tinytapeout.github.io/tt-yaml-upgrade-tool/).
3. Edit [docs/info.md](docs/info.md) and add a description of your project.
4. Adapt the testbench to your design. See [test/README.md](test/README.md) for more information.

The GitHub action will automatically build the ASIC files using [LibreLane](https://www.zerotoasiccourse.com/terminology/librelane/).

## Enable GitHub actions to build the results page

- [Enabling GitHub Pages](https://tinytapeout.com/faq/#my-github-action-is-failing-on-the-pages-part)

## Resources

- [FAQ](https://tinytapeout.com/faq/)
- [Digital design lessons](https://tinytapeout.com/digital_design/)
- [Learn how semiconductors work](https://tinytapeout.com/siliwiz/)
- [Join the community](https://tinytapeout.com/discord)
- [Build your design locally](https://www.tinytapeout.com/guides/local-hardening/)

## What next?

- [Submit your design to the next shuttle](https://app.tinytapeout.com/).
- Edit [this README](README.md) and explain your design, how it works, and how to test it.
- Share your project on your social network of choice:
  - LinkedIn [#tinytapeout](https://www.linkedin.com/search/results/content/?keywords=%23tinytapeout) [@TinyTapeout](https://www.linkedin.com/company/100708654/)
  - Mastodon [#tinytapeout](https://chaos.social/tags/tinytapeout) [@matthewvenn](https://chaos.social/@matthewvenn)
  - X (formerly Twitter) [#tinytapeout](https://twitter.com/hashtag/tinytapeout) [@tinytapeout](https://twitter.com/tinytapeout)
  - Bluesky [@tinytapeout.com](https://bsky.app/profile/tinytapeout.com)
