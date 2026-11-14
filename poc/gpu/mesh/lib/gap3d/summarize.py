#!/usr/bin/env python3
# Re-derives every count in REPORT.md from storage/mesh/gap3d/out/results.tsv: per kind / route / sub-result verdict counts, per-class WRONG/REFUSED breakdown, max ulps of NEAR+WRONG rows.
import csv, collections, sys
ST = "/Volumes/CrucialX9/Projects/mongo/storage/mesh/gap3d/out/results.tsv"
rows = list(csv.DictReader(open(ST), delimiter="\t"))
V = ["CORRECT", "NEAR", "WRONG", "REFUSED", "NOAPI", "CRASH"]
tab = collections.OrderedDict()
for r in rows: tab.setdefault((r["kind"], r["route"], r["sub"]), collections.Counter())[r["verdict"]] += 1
print("%-9s %-10s %-9s %7s %5s %5s %7s %5s %5s %5s" % ("kind", "route", "result", "CORRECT", "NEAR", "WRONG", "REFUSED", "NOAPI", "CRASH", "total"))
for k, c in tab.items(): print("%-9s %-10s %-9s %7d %5d %5d %7d %5d %5d %5d" % (k + tuple(c[v] for v in V) + (sum(c.values()),)))
print("\nnon-CORRECT rows by class (verdict:count) and max ulps")
by = collections.OrderedDict()
for r in rows:
    if r["verdict"] == "CORRECT": continue
    key = (r["kind"], r["route"], r["sub"], r["cls"].split("+")[0]); d = by.setdefault(key, [collections.Counter(), 0.0]); d[0][r["verdict"]] += 1; d[1] = max(d[1], float(r["ulps"]))
for k, (c, u) in by.items(): print("  %-9s %-10s %-9s %-22s %s  max_ulps=%.3g" % (k + (" ".join("%s:%d" % (a, b) for a, b in sorted(c.items())), u)))
print("\ncases per kind:", dict(collections.Counter(r["kind"] for r in rows if r["route"] == rows[0]["route"] or True and r["sub"] in ("solid", "tess", "distance", "volume")).items()))
