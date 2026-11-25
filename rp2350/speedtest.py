#!/usr/bin/env python3
"""Speed test of the hash chip, run by the RP2350 firmware and saved here as JSON + Markdown.

    python3 rp2350/speedtest.py                       # board on the first RP2350 serial port
    python3 rp2350/speedtest.py --select 123          # TT demo board: select our project first (its mux address)
    python3 rp2350/speedtest.py --max-mhz 50          # don't try clocks above 50 MHz
    python3 rp2350/speedtest.py --sys 250             # start the RP2350 at 250 MHz: chip clocks up to 125 MHz
    python3 rp2350/speedtest.py --no-overclock        # never raise the RP2350's clock
    python3 rp2350/speedtest.py --bench-only --clk 25000000   # throughput at one clock, no sweep

Needs the firmware from rp2350/ on the board (tt_hash_dbv3.uf2 on the demo board, tt_hash_pico2w_tang.uf2 on the
Pico 2 W + Tang Nano rig) and pyserial. What the firmware does (core/hash_bench.c):
  1. wire speed: at every chip clock it can make (1 MHz up to max) and every sample skew (0-2), six known-answer
     jobs that use every pin and output path (bursts, packets + on-chip padding, CBD; SampleNTT and the valid / busy
     pins in framing mode 1), three rounds. The highest clock that passes, and the highest whose skew also passes
     one step lower (the one it uses), are the board's wire speed. If even the fastest clock the RP2350 can make
     passes (likely on the real chip's PCB, with short traces), this script raises the RP2350 to 200 and then
     250 MHz (half of it is the fastest chip clock) and sweeps again, so a fast board shows its real limit.
  2. throughput at that clock: the 32 hash calls of one ML-KEM-512 key exchange, timed with the RP2350's cycle
     counter, once on the chip (the firmware as written: building the pin words, PIO + DMA, picking the bytes out)
     and once in software on the same RP2350 (portable C SHA-3 + CBD + SampleNTT); plus absorb and squeeze rates.
     Every chip result is compared with the software one.
Results: rp2350/results/speedtest_<date>_<board>.json and .md (the directory is made if needed).
"""
import argparse
import datetime
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def find_port():
    from serial.tools import list_ports
    for p in list_ports.comports():
        if p.vid == 0x2E8A:                       # Raspberry Pi (RP2350 USB serial)
            return p.device
    sys.exit("no RP2350 serial port found: plug the board in, or give --port")


class Board:
    def __init__(self, port):
        import serial
        self.ser = serial.Serial(port, 115200, timeout=0.2)
        self.port = port
        time.sleep(0.3)
        self.ser.reset_input_buffer()

    def cmd(self, line, until=None, timeout=60.0, quiet=0.8):
        """Send a text command; return its output. Stops at a line matching `until`, or after `quiet` seconds
        without output (commands like status), or at `timeout`."""
        self.ser.write((line + "\r").encode())
        out, last, t0 = "", time.time(), time.time()
        while time.time() - t0 < timeout:
            chunk = self.ser.read(4096).decode(errors="replace")
            if chunk:
                out += chunk
                last = time.time()
                if until and re.search(until, out, re.M):
                    time.sleep(0.2)
                    out += self.ser.read(4096).decode(errors="replace")
                    return out
            elif not until and time.time() - last > quiet:
                return out
        if until:
            raise TimeoutError(f"`{line}` did not finish within {timeout:.0f} s; last output:\n{out[-800:]}")
        return out


def kv(line):
    return {k: (float(v) if re.fullmatch(r"-?[0-9.]+", v) else v) for k, v in re.findall(r"(\w+)=(\S+)", line)}


def parse(text):
    res = {"sweep": [], "bench": {}, "speedtest": {}}
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("SWEEP "):
            res["sweep"].append(kv(line))
        elif line.startswith("BENCH "):
            d = kv(line)
            res["bench"][d.pop("workload")] = d
        elif line.startswith("SPEEDTEST "):
            d = kv(line)
            res["speedtest"].update(d)
            for sw in res["sweep"]:                     # this run's sweep lines: the RP2350 clock they ran at
                sw.setdefault("sys_hz", d.get("sys_hz", 0))
    return res


def markdown(meta, res):
    m = [f"# Hash chip speed test: {meta['board']} ({meta['date']})", "",
         f"Port `{meta['port']}`. Firmware status at the start:", "", "```", meta["status"].strip(), "```", ""]
    if res["sweep"]:
        m += ["## Wire speed", "",
              "Six known-answer jobs (bursts, packets + on-chip padding, CBD; SampleNTT and the valid / busy pins in "
              "mode 1), three rounds, at each clock and sample skew.", "",
              "| RP2350 | Chip clock | skew 0 | skew 1 | skew 2 | skew 3 |", "|---|---|---|---|---|---|"]
        for s in res["sweep"]:
            m.append(f"| {s.get('sys_hz', 0) / 1e6:.0f} MHz | {s['clock_hz'] / 1e6:.2f} MHz | "
                     + " | ".join("pass" if s.get(f"skew{k}") else "–" for k in range(4)) + " |")
        st = res["speedtest"]
        if "max_pass_hz" in st:
            m += ["", f"Highest passing clock **{st['max_pass_hz'] / 1e6:.2f} MHz**; tried up to "
                  f"{st['tried_up_to_hz'] / 1e6:.2f} MHz. Throughput measured at **{st['recommended_hz'] / 1e6:.2f} MHz**, "
                  f"skew {int(st['recommended_skew'])} (its skew also passes one step lower).", ""]
            if st.get("host_limited"):
                m += ["**The fastest clock tried still passed**: the board is faster than the RP2350 can clock it at "
                      f"{st.get('sys_hz', 0) / 1e6:.0f} MHz.", ""]
    b = res["bench"]
    if "mlkem512_exchange" in b:
        e = b["mlkem512_exchange"]
        m += ["## Throughput", "",
              f"ML-KEM-512, the 32 hash calls of one key exchange, chip clock {e['clock_hz'] / 1e6:.2f} MHz. "
              "Times from the RP2350's cycle counter. *With the chip* is the firmware as written (pin words, PIO + DMA, "
              "unpacking); *chip alone* is the chip's clocks at this clock.", "",
              "| | RP2350 software | RP2350 + chip | speed-up | chip clocks | chip alone |", "|---|---|---|---|---|---|"]
        for op in ("keygen", "encaps", "decaps"):
            if op in b:
                o = b[op]
                m.append(f"| {op} | {o['sw_us']:.0f} µs | {o['chip_us']:.0f} µs | {o['sw_us'] / o['chip_us']:.2f}× | "
                         f"{int(o['chip_clocks']):,} | {o['chip_clocks'] / o['clock_hz'] * 1e6:.0f} µs |"
                         if o["clock_hz"] else f"| {op} | {o['sw_us']:.0f} µs | {o['chip_us']:.0f} µs | | | |")
        m.append(f"| **exchange** | **{e['sw_us']:.0f} µs** | **{e['chip_us']:.0f} µs** | **{e['speedup']:.2f}×** | "
                 f"{int(e['chip_clocks']):,} | {e['chip_only_us']:.0f} µs |")
        m += ["", f"Errors: {int(e['errors'])} (every chip result is compared with the software one).", ""]
        if "breakdown" in b:
            d = b["breakdown"]
            conv = d.get("conv_us", 0)
            m += [f"Where the {e['chip_us']:.0f} µs with the chip go: {e['chip_only_us']:.0f} µs of chip clocks, "
                  f"{conv:.0f} µs reshuffling words onto the Pico rig's split pins (none on the demo board), "
                  f"{d['run_us'] - e['chip_only_us'] - conv:.0f} µs of other transport work (DMA setup, splitting the samples), "
                  f"{d['driver_us']:.0f} µs in the driver (building and checking the pin words).", ""]
        styles = [("newer commands, framing mode 0, SampleNTT on the RP2350 (the default)", ""),
                  ("every newer command, framing mode 1 (SampleNTT on chip)", "_mode1"),
                  ("the October 2026 way: host padding, a word per byte, a burst word per block", "_legacy")]
        if any("mlkem512_exchange" + k in b for _, k in styles[1:]):
            m += ["### Three ways to drive the same chip", "",
                  "Same chip, same workload, same clock, every result checked. Framing mode 1 adds the `valid` and "
                  "`busy` pins, so the RP2350 samples 16 bits per clock instead of 8.", "",
                  "| Driven | chip clocks | driver | transport | RP2350 + chip | vs software |", "|---|---|---|---|---|---|"]
            for name, k in styles:
                if "mlkem512_exchange" + k in b:
                    x, d = b["mlkem512_exchange" + k], b.get("breakdown" + k, {})
                    m.append(f"| {name} | {int(x['chip_clocks']):,} | {d.get('driver_us', 0):.0f} µs | "
                             f"{d.get('run_us', 0):.0f} µs | {x['chip_us']:.0f} µs | {x['speedup']:.2f}× |")
            m.append("")
        rows = [(k, b[k]) for k in ("absorb", "squeeze") if k in b]
        if rows:
            m += ["| Rate | bytes | software | with the chip | chip alone |", "|---|---|---|---|---|"]
            for k, r in rows:
                alone = r["bytes"] / (r["chip_clocks"] / r["clock_hz"]) / 1e6 if r["clock_hz"] else 0
                m.append(f"| {k} | {int(r['bytes'])} | {r['bytes'] / r['sw_us']:.2f} MB/s | {r['bytes'] / r['chip_us']:.2f} MB/s | "
                         f"{alone:.2f} MB/s |")
    return "\n".join(m) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="the RP2350's serial port (default: the first one found)")
    ap.add_argument("--select", type=int, help="TT demo board: our project's mux address")
    ap.add_argument("--sys", type=int, help="RP2350 system clock in MHz first (150 default; 200, 250 allow faster chip clocks)")
    ap.add_argument("--max-mhz", type=int, default=0, help="highest chip clock to try (default: all the RP2350 can make)")
    ap.add_argument("--no-overclock", action="store_true", help="don't raise the RP2350 above its clock when every clock passes")
    ap.add_argument("--no-legacy", action="store_true", help="skip `bench mode1` and `bench legacy` (the same workload driven other ways)")
    ap.add_argument("--bench-only", action="store_true", help="no wire-speed sweep, throughput at --clk only")
    ap.add_argument("--clk", type=int, help="chip clock in Hz for --bench-only (default: the board's current one)")
    ap.add_argument("--out", default=os.path.join(HERE, "results"), help="where the JSON and Markdown go")
    a = ap.parse_args()
    board = Board(a.port or find_port())
    status = board.cmd("status")
    if "WRONG BOARD" in status:
        sys.exit("the firmware on this board was built for the other board:\n" + status)
    name = "pico2w_tang" if "Pico 2 W" in status else "demo_board" if "demo board" in status.lower() else "rp2350"
    print(status.strip())
    if a.sys:
        print(board.cmd(f"sys {a.sys}").strip())
    if a.select is not None:
        print(board.cmd(f"select {a.select}").strip())
    if a.bench_only:
        if a.clk:
            print(board.cmd(f"clk {a.clk}").strip())
        text = board.cmd("bench", until=r"^bench: (PASS|FAIL)", timeout=120)
    else:
        run = lambda: board.cmd(f"speedtest {a.max_mhz}" if a.max_mhz else "speedtest", until=r"^SPEEDTEST result=",
                                timeout=1200)
        text = board.cmd("status") + run()
        # every clock passed: the board is faster than this RP2350 clock can test. Raise it and sweep again.
        for sys_mhz in (200, 250):
            st = parse(text)["speedtest"]
            if a.no_overclock or a.max_mhz or not st.get("host_limited") or st.get("sys_hz", 0) >= sys_mhz * 1e6:
                break
            print(f"every clock passed at RP2350 {st['sys_hz'] / 1e6:.0f} MHz: raising it to {sys_mhz} MHz and sweeping again")
            text += board.cmd(f"sys {sys_mhz}") + board.cmd("status") + run()
    if not a.no_legacy:     # the same workload driven two other ways, at the same clock: what each choice costs
        text += board.cmd("bench mode1", until=r"^bench mode1: (PASS|FAIL)", timeout=180)
        text += board.cmd("bench legacy", until=r"^bench legacy: (PASS|FAIL)", timeout=180)
    print(text.strip())
    res = parse(text)
    meta = {"board": name, "port": board.port, "date": datetime.datetime.now().isoformat(timespec="seconds"),
            "status": status, "args": vars(a)}
    os.makedirs(a.out, exist_ok=True)
    stem = os.path.join(a.out, f"speedtest_{datetime.datetime.now():%Y%m%d_%H%M%S}_{name}")
    with open(stem + ".json", "w") as fh:
        json.dump({"meta": meta, "results": res, "log": text}, fh, indent=1)
    with open(stem + ".md", "w") as fh:
        fh.write(markdown(meta, res))
    print(f"\nsaved {stem}.json and {stem}.md")
    ex = {k: v for k, v in res["bench"].items() if k.startswith("mlkem512_exchange")}
    for k, e in sorted(ex.items(), key=lambda kv: kv[1]["chip_us"]):
        style = {"": "newer commands, mode 0", "_mode1": "every command, mode 1",
                 "_legacy": "October way"}.get(k[len("mlkem512_exchange"):], k)
        print(f"ML-KEM-512 exchange hashing ({style}): software {e['sw_us']:.0f} us, with the chip {e['chip_us']:.0f} us "
              f"({e['speedup']:.2f}x) at {e['clock_hz'] / 1e6:.2f} MHz; errors {int(e['errors'])}")
    ok = res["speedtest"].get("result", "pass") == "pass" and ex and not any(e["errors"] for e in ex.values())
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
