#!/usr/bin/env python3
"""MP18 step 2 probe: reverse the vertex index order of both operands (geometry and triangle order unchanged).
usage: revvert_probe.py <touch_corpus> <outcorpus> <label>"""
import csv, os, struct, sys
src, dst, label = sys.argv[1:4]
os.makedirs(dst + "/adversarial", exist_ok=True)
rows = list(csv.reader(open(src + "/manifest.tsv"), delimiter="\t")); keep = [rows[0]]
for r in rows[1:]:
    if "contact=" + label not in r[2]: continue
    for s in "AB":
        d = open(f"{src}/adversarial/{r[0]}_{s}.msh", "rb").read(); _, nv, nt = struct.unpack_from("<3I", d, 0)
        v = struct.unpack_from("<%dd" % (3*nv), d, 12); t = struct.unpack_from("<%dI" % (3*nt), d, 12 + 24*nv)
        V = [v[3*i:3*i+3] for i in range(nv)][::-1]; T = [nv - 1 - i for i in t]
        open(f"{dst}/adversarial/{r[0]}_{s}.msh", "wb").write(struct.pack("<3I", 0x3148534d, nv, nt) + struct.pack("<%dd" % (3*nv), *[c for p in V for c in p]) + struct.pack("<%dI" % (3*nt), *T))
    keep.append(r)
csv.writer(open(dst + "/manifest.tsv", "w", newline=""), delimiter="\t").writerows(keep)
print(len(keep) - 1, "pairs")
