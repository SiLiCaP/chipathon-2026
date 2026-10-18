"""HPWL per net class from a placed DEF: which nets eat the routing."""
import re, sys, collections
txt = open(sys.argv[1]).read()
comp = {}
for m in re.finditer(r"^\s*- (\S+) (\S+) \+ (?:PLACED|FIXED) \( (-?\d+) (-?\d+) \)", txt, re.M):
    comp[m.group(1)] = (int(m.group(3)), int(m.group(4)), m.group(2))
nets = txt[txt.index("\nNETS"):txt.index("END NETS")]
cls = collections.Counter(); cnt = collections.Counter(); fan = collections.Counter()
for blk in nets.split(";")[:-1]:
    m = re.search(r"- (\S+)", blk)
    if not m: continue
    name = m.group(1)
    pts = [comp[c][:2] for c, _ in re.findall(r"\( (\S+) (\S+) \)", blk) if c in comp]
    if len(pts) < 2: continue
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    hp = (max(xs) - min(xs) + max(ys) - min(ys)) / 1000
    n = name.replace("\\", "")
    if "u_hash" in n or n.startswith("_"):
        # classify by driver cell type for anonymous nets
        drv = None
        c0 = re.findall(r"\( (\S+) (\S+) \)", blk)
        for c, pin in c0:
            if pin in ("Q", "X", "Y", "GCLK") and c in comp: drv = comp[c][2].split("__")[-1]; break
        key = "drv:" + re.sub(r"_\d+$", "", drv or "?")
    else:
        key = re.sub(r"\[.*", "", n.split(".")[0])
    cls[key] += hp; cnt[key] += 1; fan[key] += len(pts)
tot = sum(cls.values())
print(f"total HPWL {tot/1000:.0f} mm over {sum(cnt.values())} nets")
for k, v in cls.most_common(15):
    print(f"{v/1000:8.1f} mm {100*v/tot:5.1f}%  {cnt[k]:5d} nets  avg fanout {fan[k]/cnt[k]:4.1f}  {k}")
