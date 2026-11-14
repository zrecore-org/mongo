#!/usr/bin/env python3
"""Informational only (not scoring): exact volume (Fraction arithmetic on the float64 output coordinates)
of each output mesh vs the manifest's exact expected volume, for cases that have one. usage: volcheck.py <runs_dir> <corpus_dir>"""
import csv, struct, sys, os
from fractions import Fraction
runs, corpus = sys.argv[1:3]
man = {r["id"]: r for r in csv.DictReader(open(os.path.join(corpus, "manifest.tsv")), delimiter="\t")}
def parse(s):
    if not s or "*2^" not in s: return None
    n, e = s.split("*2^"); return Fraction(int(n)) * Fraction(2) ** int(e)
def exact_vol(p):
    d = open(p, "rb").read(); _, nv, nt = struct.unpack_from("<3I", d, 0)
    v = [Fraction(x) for x in struct.unpack_from("<%dd" % (3 * nv), d, 12)]
    t = struct.unpack_from("<%dI" % (3 * nt), d, 12 + 24 * nv)
    s = Fraction(0)
    for i in range(nt):
        a, b, c = (v[3*t[3*i+k]:3*t[3*i+k]+3] for k in range(3))
        s += a[0]*(b[1]*c[2]-b[2]*c[1]) - a[1]*(b[0]*c[2]-b[2]*c[0]) + a[2]*(b[0]*c[1]-b[1]*c[0])
    return s / 6
key = {"union": "vol6_union", "intersection": "vol6_inter", "difference": "vol6_diff"}
print("case\top\texpected_vol\toutput_exact_vol\tmatch")
for cid, m in sorted(man.items()):
    for op, col in key.items():
        e6 = parse(m[col]); p = os.path.join(runs, "%s.%s.r1.msh" % (cid, op))
        if e6 is None or not os.path.exists(p): continue
        e = e6 / 6; o = exact_vol(p)
        print("%s\t%s\t%s\t%s\t%s" % (cid, op, float(e), float(o), "yes" if e == o else "NO"))
