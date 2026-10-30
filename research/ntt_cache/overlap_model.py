"""ML-KEM-512 on RP2350 + chip: list-scheduling model with overlap (option 1: firmware only, no area).

    python3 research/ntt_cache/overlap_model.py   (copied from research/sha3 in the team repo)

Each operation is a task graph (FIPS 203, k = 2). Resources: the RP2350 CPU (one M33 core) and the chip.
Hash calls run on the chip's Keccak core (ring core, K = 8). NTT-type jobs run either in software on the CPU
or on the chip's NTT core; the scheduler picks whichever finishes first when the job becomes ready.

Inputs and how sure they are:
  cited    pqm4 m4fspeed totals and hashing share per op; NTT 5,992 / iNTT 5,491 / basemul 1,613 cycles
           (ePrint 2022/112); same cycle counts assumed on the M33 at 150 MHz (as in Viktor's model)
  measured chip clocks: Keccak 368 per permutation (RTL); NTT 2,751, iNTT 2,765, basemul 655, scale 394
           per job (docs/host_timing.md, pin-level test)
  ASSUMED  split of the remaining CPU work (sampling, CBD, add, encode, compress) by the weights in REST_W,
           scaled so that each op's software-only total matches pqm4;
           CPU packing cost per chip NTT unit: "today" = Viktor's PIO+DMA numbers (12 cycles per butterfly,
           14 per basemul piece, 8 per scale pair); "PIO flags" = 3 / 4 / 2 (PIO inserts valid/op bits,
           DMA streams straight from the polynomial arrays) -- to be confirmed in firmware;
           2 us host setup per hash call
  CHIP_SPLIT: False = the chip runs one job at a time; True = NTT jobs may use the pins while the Keccak
           core permutes (both cores busy, pins shared) -- an optimistic bound
"""
F_RP, F_CHIP = 150e6, 50e6
US = 1e-6
from mlkem_model import chip_clocks, RP, SETUP_S  # noqa: E402

SW = {"ntt": 5992 / F_RP, "intt": 5491 / F_RP, "bm": 1613 / F_RP}
CHIP = {"ntt": 2751 / F_CHIP, "intt": (2765 + 394) / F_CHIP, "bm": 655 / F_CHIP}
UNITS = {"ntt": (896, "bf"), "intt": (896 + 128, "mix"), "bm": (128, "piece")}
PACK = {"today": {"bf": 12, "piece": 14, "scale": 8}, "PIO flags": {"bf": 3, "piece": 4, "scale": 2}}


def pack_s(kind, mode):
    p = PACK[mode]
    if kind == "ntt":
        return 896 * p["bf"] / F_RP
    if kind == "intt":
        return (896 * p["bf"] + 128 * p["scale"]) / F_RP
    return 128 * p["piece"] / F_RP


# ---- task graphs: (name, kind, deps). kind: hash:<call>, cpu:<weight-key>, ntt/intt/bm ----
def keygen():
    t = [("G", "hash:G", [])]
    for i in range(4):
        t += [(f"xof{i}", "hash:XOF", ["G"]), (f"parse{i}", "cpu:parse", [f"xof{i}"])]
    for i in range(4):
        t += [(f"prf{i}", "hash:PRF192", ["G"]), (f"cbd{i}", "cpu:cbd", [f"prf{i}"]),
              (f"ntt{i}", "ntt", [f"cbd{i}"])]
    for i in range(2):
        for j in range(2):
            t += [(f"bm{i}{j}", "bm", [f"parse{2*i+j}", f"ntt{j}"])]
        t += [(f"add{i}", "cpu:add", [f"bm{i}0", f"bm{i}1", f"ntt{2+i}"])]
    t += [("enc", "cpu:encode", ["add0", "add1"]), ("H", "hash:H", ["enc"]), ("encdk", "cpu:encode", ["enc"])]
    return t


def encrypt(prefix, after):
    p = prefix
    t = []
    for i in range(4):
        t += [(f"{p}xof{i}", "hash:XOF", after["rho"]), (f"{p}parse{i}", "cpu:parse", [f"{p}xof{i}"])]
    for i, eta in enumerate([192, 192, 128, 128, 128]):
        t += [(f"{p}prf{i}", f"hash:PRF{eta}", after["G"]), (f"{p}cbd{i}", "cpu:cbd", [f"{p}prf{i}"])]
    for i in range(2):
        t += [(f"{p}ntt{i}", "ntt", [f"{p}cbd{i}"])]
    for i in range(2):              # u = iNTT(A^T r) + e1
        for j in range(2):
            t += [(f"{p}bmA{i}{j}", "bm", [f"{p}parse{2*j+i}", f"{p}ntt{j}"])]
        t += [(f"{p}intu{i}", "intt", [f"{p}bmA{i}0", f"{p}bmA{i}1"]),
              (f"{p}addu{i}", "cpu:add", [f"{p}intu{i}", f"{p}cbd{2+i}"])]
    for j in range(2):              # v = iNTT(t^T r) + e2 + m
        t += [(f"{p}bmt{j}", "bm", [f"{p}ntt{j}"] + after["t"])]
    t += [(f"{p}intv", "intt", [f"{p}bmt0", f"{p}bmt1"]),
          (f"{p}addv", "cpu:add", [f"{p}intv", f"{p}cbd4"]),
          (f"{p}comp", "cpu:compress", [f"{p}addu0", f"{p}addu1", f"{p}addv"])]
    return t


def encaps():
    t = [("dec", "cpu:decode", []), ("H", "hash:H", []), ("G", "hash:G64", ["H"])]
    return t + encrypt("", {"rho": ["dec"], "G": ["G"], "t": ["dec"]})


def decaps():
    t = [("dec", "cpu:decode", []), ("J", "hash:J", [])]
    for i in range(2):
        t += [(f"nttu{i}", "ntt", ["dec"]), (f"bms{i}", "bm", [f"nttu{i}"])]
    t += [("intm", "intt", ["bms0", "bms1"]), ("subm", "cpu:compress", ["intm"]), ("G", "hash:G64", ["subm"])]
    t += encrypt("re_", {"rho": ["dec"], "G": ["G"], "t": ["dec"]})
    t += [("cmp", "cpu:compare", ["re_comp", "J"])]
    return t


GRAPHS = {"keygen": keygen, "encaps": encaps, "decaps": decaps}
REST_W = {"parse": 3, "cbd": 2, "add": 1, "encode": 4, "decode": 4, "compress": 4, "compare": 2}  # ASSUMED

def hash_time(tag):
    from mlkem_model import G, H_EK, J, XOF, PRF3, PRF2
    call = {"G": G(33), "G64": G(64), "XOF": XOF, "PRF192": PRF3, "PRF128": PRF2, "H": H_EK, "J": J}[tag]
    return chip_clocks(call, 8) / F_CHIP


def hash_perms(tag):
    from mlkem_model import G, H_EK, J, XOF, PRF3, PRF2, perms
    return perms({"G": G(33), "G64": G(64), "XOF": XOF, "PRF192": PRF3, "PRF128": PRF2, "H": H_EK, "J": J}[tag])


def cpu_weights(op, graph):
    total, hashing = RP[op]
    arith = sum(SW[k] for _, k, _ in graph if k in SW)
    rest = total - hashing - arith
    w = sum(REST_W[k.split(":")[1]] for _, k, _ in graph if k.startswith("cpu:"))
    return rest / w


def schedule(op, mode, arith_policy, chip_split, cores=1):
    g = GRAPHS[op]()
    unit = cpu_weights(op, g)
    deps = {n: d for n, _, d in g}
    kind = {n: k for n, k, _ in g}
    # critical-path priority with software durations
    def dur_sw(n):
        k = kind[n]
        ci = min(range(cores), key=lambda i: cpus[i])
        free["cpu"] = cpus[ci]
        if k.startswith("hash:"):
            return hash_time(k[5:])
        if k.startswith("cpu:"):
            return REST_W[k[4:]] * unit
        return SW[k]
    succ = {n: [m for m in deps if n in deps[m]] for n in deps}
    memo = {}
    def tail(n):
        if n not in memo:
            memo[n] = dur_sw(n) + max((tail(m) for m in succ[n]), default=0)
        return memo[n]
    free = {"cpu": 0.0, "keccak": 0.0, "pins": 0.0}
    cpus = [0.0] * cores
    done = {}
    pending = set(deps)
    while pending:
        ready = [n for n in sorted(pending) if all(d in done for d in deps[n])]   # sorted: deterministic ties
        n = max(ready, key=tail)
        t0 = max((done[d] for d in deps[n]), default=0.0)
        k = kind[n]
        ci = min(range(cores), key=lambda i: cpus[i])
        free["cpu"] = cpus[ci]
        if k.startswith("hash:") and arith_policy == "nochip":
            s = max(t0, free["cpu"])
            free["cpu"] = done[n] = s + hash_perms(k[5:]) * RP[op][1] / {"keygen": 27, "encaps": 26, "decaps": 26}[op]
        elif k.startswith("hash:"):
            dt = hash_time(k[5:])
            res = ["keccak", "pins"] if not chip_split else ["keccak"]
            s = max([t0, free["cpu"]] + [free[r] for r in res])
            free["cpu"] = s + SETUP_S
            for r in res:
                free[r] = s + dt
            if chip_split:   # pins only for the I/O part (about a third of a hash call at K = 8)
                free["pins"] = max(free["pins"], s + dt * 0.35)
            done[n] = s + dt
        elif k.startswith("cpu:"):
            s = max(t0, free["cpu"])
            free["cpu"] = done[n] = s + REST_W[k[4:]] * unit
        else:
            s_sw = max(t0, free["cpu"]); f_sw = s_sw + SW[k]
            res = ["pins"] if chip_split else ["keccak", "pins"]
            s_ch = max([t0, free["cpu"]] + [free[r] for r in res])
            f_ch = s_ch + max(CHIP[k], pack_s(k, mode))
            use_chip = arith_policy == "chip" or (arith_policy == "best" and f_ch < f_sw)
            if use_chip:
                free["cpu"] = s_ch + pack_s(k, mode)
                for r in res:
                    free[r] = s_ch + CHIP[k]
                done[n] = f_ch
            else:
                free["cpu"] = done[n] = f_sw
        cpus[ci] = free["cpu"]
        pending.remove(n)
    return max(done.values())


def main():
    base = sum(RP[op][0] for op in RP)
    print(f"RP2350 alone: {base*1e3:.2f} ms per exchange\n")
    rows = [
        ("hash on chip, NTT in software", "today", "sw", False),
        ("+ NTT on chip, packing today", "today", "chip", False),
        ("+ NTT where faster, packing today", "today", "best", False),
        ("+ NTT where faster, PIO flags", "PIO flags", "best", False),
        ("+ NTT on chip, PIO flags", "PIO flags", "chip", False),
        ("+ NTT during permutations (optimistic)", "PIO flags", "best", True),
    ]
    print(f"{'scenario (overlap scheduled)':48} {'keygen':>8} {'encaps':>8} {'decaps':>8} {'exch.':>8} {'vs RP':>6}")
    rows += [("2 cores: hash on chip, NTT in software", "today", "sw", False, 2),
             ("2 cores: + NTT on chip where faster, PIO flags", "PIO flags", "best", False, 2),
             ("2 cores: all on CPU (no chip), for reference", "today", "nochip", False, 2)]
    for row in rows:
        label, mode, pol, split = row[:4]
        cores = row[4] if len(row) > 4 else 1
        ts = [schedule(op, mode, pol, split, cores) for op in GRAPHS]
        print(f"{label:48} " + " ".join(f"{t*1e3:6.2f}ms" for t in ts) + f" {sum(ts)*1e3:6.2f}ms {base/sum(ts):5.2f}x")


if __name__ == "__main__":
    main()
