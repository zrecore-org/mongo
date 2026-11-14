#!/usr/bin/env python3
"""Compare each mutant's scored rows with the clean pipeline's: a row is changed when (verdict, volume status) differs. usage: mutant_report.py <WORK> <clean score dir> <MUT>..."""
import csv, os, re, sys
from collections import Counter
work, clean = sys.argv[1], sys.argv[2]
def load(d, n):
    return {(r[0], r[1]): r for r in list(csv.reader(open(os.path.join(d, n + ".tsv")), delimiter="\t"))[1:]}
def key(r):
    m = re.search(r"volume=(\w+)", r[6]); return (r[3], m.group(1) if m else "-")
for m in sys.argv[3:]:
    tot = Counter(); ex = []
    for n in ("g1", "off", "thin"):
        c, x = load(clean, n), load(os.path.join(work, f"mut_{m}"), n)
        ch = [k for k in c if k in x and key(c[k]) != key(x[k])]
        tot[n] = len(ch); ex += [f"{n}:{k[0]}.{k[1]} {key(c[k])} -> {key(x[k])}" for k in ch[:3]]
        tot[n + "_FAIL"] = sum(1 for v in x.values() if v[3] == "FAIL")
    print(f"mutant {m}: changed rows g1 {tot['g1']}, offset {tot['off']}, thin {tot['thin']}; FAIL rows g1 {tot['g1_FAIL']}, offset {tot['off_FAIL']}, thin {tot['thin_FAIL']}; "
          f"{'CAUGHT' if tot['g1'] + tot['off'] + tot['thin'] > 0 else 'NOT CAUGHT'}")
    for e in ex[:6]: print("   e.g. " + e)
