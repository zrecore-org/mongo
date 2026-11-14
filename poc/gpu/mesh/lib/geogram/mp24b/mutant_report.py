#!/usr/bin/env python3
"""Compare each mutant's scored rows with the clean pipeline's: a row is changed when (verdict, volume status) differs. Reports, per mutant and per family, the changed rows, the FAIL rows and the classes (f2) that catch it.
usage: mutant_report.py <WORK> <clean score dir> <MUT>..."""
import csv, os, re, sys
from collections import Counter
work, clean = sys.argv[1], sys.argv[2]
FAM = ("g1", "off", "thin", "set3", "set5", "f2")
def load(d, n):
    return {(r[0], r[1]): r for r in list(csv.reader(open(os.path.join(d, n + ".tsv")), delimiter="\t"))[1:]}
def key(r):
    m = re.search(r"volume=(\w+)", r[6]); return (r[3], m.group(1) if m else "-")
def cls(n, cid):
    p = cid.split("_"); return p[0] if n == "g1" else p[1] if len(p) > 1 else cid
for m in sys.argv[3:]:
    changed = {}; fails = {}; ex = []; fcl = Counter()
    for n in FAM:
        c, x = load(clean, n), load(os.path.join(work, f"mut_{m}"), n)
        ch = [k for k in c if k in x and key(c[k]) != key(x[k])]
        changed[n] = len(ch); fails[n] = sum(1 for v in x.values() if v[3] == "FAIL")
        ex += [f"{n}:{k[0]}.{k[1]} {key(c[k])} -> {key(x[k])}" for k in ch[:2]]
        if n == "f2":
            for k in ch: fcl[cls("f2", k[0])] += 1
    caught = [n for n in FAM if changed[n] > 0]
    print(f"mutant {m}: changed rows " + ", ".join(f"{n} {changed[n]}" for n in FAM) + "; FAIL rows " + ", ".join(f"{n} {fails[n]}" for n in FAM) +
          f"; {'CAUGHT by ' + '/'.join(caught) if caught else 'NOT CAUGHT'}" + (f"; f2 classes changed: {dict(fcl)}" if fcl else ""))
    for e in ex[:6]: print("   e.g. " + e)
