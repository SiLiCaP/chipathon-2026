"""Check every wire of the Pico 2 W + Tang Nano rig before running the chip (docs/PICO_TANG_RIG.md).

    make -C fpga prog TARGET=pico GL=0 TT_TOP=tt_um_rig_pinchk EXTRA_SRCS=$PWD/fpga/rig_pinchk.v
    python3 tools/rig_pincheck.py --port /dev/ttyACM0 [--mhz 1 5 13.5]

The FPGA runs fpga/rig_pinchk.v: each rising clock latches ui_in or uio_in (alternating, starting with ui_in
after reset) onto uo_out. The PC drives words through the Pico's firmware (TTE1 over USB: LOAD, RUN, READ_CAP,
RESET), so the Pico's real PIO lockstep moves every bit over the wires. Walking ones and walking zeros on
both input bytes test each of the 16 input wires and the 8 output wires on their own, and catch opens,
shorts to a neighbour, and swaps. Clock and reset are checked by their effect. Every failure is reported as
the jumper to look at: chip signal, Pico pin, Tang Nano header position.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(ROOT, "host"), os.path.join(ROOT, "fpga")]
from pico_rig_pins import PICO_PIN, WIRES  # noqa: E402
from tt_emu import TTEmu  # noqa: E402

WIRE = {w[0]: w for w in WIRES}


def where(sig):
    s, gp, pos, pin, d, _ = WIRE[sig]
    return f"{s:10} Pico GP{gp:<2} (pin {PICO_PIN[gp]:2}) <-> Tang {pos:3} (FPGA pin {pin})"


def patterns():
    """Bytes that make every bit differ from every other bit at least once (walking 1, walking 0, extremes)."""
    out = [0x00, 0xFF, 0x55, 0xAA]
    out += [1 << i for i in range(8)]
    out += [0xFF ^ (1 << i) for i in range(8)]
    return out


def run(emu, words):
    """Reset, then play the words; return the uo_out sample of every clock (before its rising edge)."""
    emu.set_inputs(0, 0)
    emu.reset(4)
    return [c[0] for c in emu.run_stream(words + [0])]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--mhz", type=float, nargs="+", default=[1.0, 5.0, 13.5],
                    help="chip clocks to test at (SET_DIV: 13.5 MHz / d, the nearest the Pico can make)")
    a = ap.parse_args()
    emu = TTEmu(a.port)
    print(f"{a.port}: magic {emu.ping().decode()}")
    pats = patterns()
    bad_any = False
    for mhz in a.mhz:
        d = max(1, round(13.5 / mhz))
        emu.set_div(d)
        # words: (ui, uio) pairs; clock 2k carries ui = p, clock 2k+1 carries uio = p, with the other byte
        # holding the complement so a short between a ui wire and a uio wire shows up too
        words = []
        for p_ in pats:
            words.append(((~p_ & 0xFF) << 8) | p_)        # even clock: ui_in = p   (uio = ~p)
            words.append((p_ << 8) | (~p_ & 0xFF))        # odd clock:  uio_in = p  (ui = ~p)
        got = run(emu, words)
        exp = [0] + [w & 0xFF if k % 2 == 0 else w >> 8 for k, w in enumerate(words)]
        # sample k+1 = byte latched at edge k
        bits_in = {"ui_in": [0] * 8, "uio": [0] * 8}
        out_bits = [0] * 8
        for k in range(len(words)):
            e, g = exp[k + 1], got[k + 1]
            diff = e ^ g
            if diff:
                name = "ui_in" if k % 2 == 0 else "uio"
                for i in range(8):
                    if diff >> i & 1:
                        bits_in[name][i] += 1
                        out_bits[i] += 1
        print(f"\n{13.5 / d:5.2f} MHz: {len(words)} clocks")
        bad = False
        # a bit wrong for both ui and uio at the same position points at the output wire, else the input wire
        for i in range(8):
            ui_bad, uio_bad = bits_in["ui_in"][i], bits_in["uio"][i]
            if ui_bad and uio_bad:
                print(f"  BAD  {where(f'uo_out[{i}]')}   (wrong for both input bytes)")
                bad = True
            elif ui_bad:
                print(f"  BAD  {where(f'ui_in[{i}]')}   ({ui_bad} patterns)")
                bad = True
            elif uio_bad:
                print(f"  BAD  {where(f'uio[{i}]')}   ({uio_bad} patterns)")
                bad = True
        # reset: hold rst_n low while clocking nonzero words -> the output must stay 0
        emu.set_inputs(0xFF, 0xFF)
        emu.reset(8)
        stuck = emu.step(0)[0]
        if stuck != 0:
            print(f"  BAD  {where('rst_n')}   (uo_out = 0x{stuck:02x} after reset with inputs 0xFF, expected 0)")
            bad = True
        # clock: the output must follow the alternation, which only a working clock produces
        if got[1:3] == [0, 0] and all(g == 0 for g in got):
            print(f"  BAD  {where('clk')}   (no output ever changed: no clock, or every output wire open)")
            bad = True
        if not bad:
            print("  all 26 wires OK: 8 ui_in, 8 uio, 8 uo_out, clk, rst_n")
        bad_any |= bad
    print("\nPASS" if not bad_any else "\nFAIL: fix the wires above (docs/PICO_TANG_RIG.md table), then rerun")
    sys.exit(1 if bad_any else 0)


if __name__ == "__main__":
    main()
