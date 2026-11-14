#!/usr/bin/env python3 -I
"""tables.py <tsv> [...]: per (op, family) list the parameter values (k, or j for ratio, or t for offsets) whose verdict is not OK, as ranges."""
import csv, sys
from collections import defaultdict
def rng(xs):
    xs = sorted(xs); out = []; s = p = xs[0]
    for x in xs[1:]:
        if x == p + 1: p = x; continue
        out.append((s, p)); s = p = x
    out.append((s, p)); return ",".join(f"{a}" if a == b else f"{a}..{b}" for a, b in out)
for path in sys.argv[1:]:
    rows = list(csv.DictReader(open(path), delimiter="\t"))
    print("==", path.split("/")[-1], {v: sum(r["verdict"] == v for r in rows) for v in sorted({r["verdict"] for r in rows})})
    tot = defaultdict(int); bad = defaultdict(list)
    for r in rows:
        key = (r["family"], r["op"]); par = int(r["extra"]) if r["family"] == "ratio" else int(r["k"])
        lab = f"k{r['k']}:j" if r["family"] == "ratio" else ""
        tot[key] += 1
        if r["verdict"] != "OK": bad[key].append((int(r["k"]), par, r["verdict"], r["detail"][:30]))
    for (fam, op) in sorted(tot):
        b = bad.get((fam, op), [])
        if not b: print(f"  {fam:12s} {op:12s} {tot[(fam,op)]:3d} runs: all OK"); continue
        if fam == "ratio":
            byk = defaultdict(list)
            for k, j, v, d in b: byk[k].append(j)
            desc = "; ".join(f"k={k}: j in {rng(js)}" for k, js in sorted(byk.items()))
        else: desc = "k/t in " + rng([p for _, p, _, _ in b])
        print(f"  {fam:12s} {op:12s} {tot[(fam,op)]:3d} runs, {len(b):3d} not OK ({','.join(sorted({v for _,_,v,_ in b}))}; {b[0][3]}): {desc}")
