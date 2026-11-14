#!/usr/bin/env python3
"""MP22b report: per-class verdict tables for the unpatched, MP20-patched and MP22-patched Geogram on set 5, verdict changes, sha256 of the MP22
outputs, and the bar line. usage: python3 -I mp22b_report.py <WORK> <arch-tag> <expected-pairs>
Reads WORK/score/{unpatched,mp20,mp22}_set5*.tsv (shard files *_set5_sK.tsv are merged) and the MP22 output meshes. Writes WORK/report5/*.
Exit status is 0 whatever the verdicts are (the workflow job is informational); the bar line is printed and, when NOT MET, a ::warning:: is emitted."""
import csv, glob, hashlib, os, sys
from collections import Counter

work, arch, npairs = sys.argv[1], sys.argv[2], int(sys.argv[3])
S = os.path.join(work, "score"); R = os.path.join(work, "report5"); os.makedirs(R, exist_ok=True)
BUILDS = ["unpatched", "mp20", "mp22"]; VERDICTS = ["PASS", "FAIL", "REFUSED"]
out = []
def P(s=""): print(s); out.append(s)

def load(b):
    rows = {}
    for f in sorted(glob.glob(os.path.join(S, f"{b}_set5.tsv")) + glob.glob(os.path.join(S, f"{b}_set5_s*.tsv"))):
        for r in list(csv.reader(open(f), delimiter="\t"))[1:]: rows[(r[0], r[1])] = r
    return rows

def cls(cid): return cid.split("_")[1]            # s5_<class>_<name>

def table(title, rows):
    P(f"\n{title}"); P("| class | rows | PASS | FAIL | REFUSED | other |"); P("|---|---:|---:|---:|---:|---:|")
    for k in sorted({cls(c) for (c, _o) in rows}) + ["ALL"]:
        c = Counter(v[3] for (cid, op), v in rows.items() if k == "ALL" or cls(cid) == k)
        n = sum(c.values()); P(f"| {k} | {n} | {c['PASS']} | {c['FAIL']} | {c['REFUSED']} | {n - sum(c[v] for v in VERDICTS)} |")

def out_sha(cid, op):
    for d in glob.glob(os.path.join(S, "mp22_set5*_*")):
        f = os.path.join(d, f"{cid}.{op}.msh")
        if os.path.exists(f): return hashlib.sha256(open(f, "rb").read()).hexdigest()
    return "-"

P(f"# MP22b set 5, Geogram v1.10.1 on {arch}")
data = {b: load(b) for b in BUILDS}; want = npairs * 3
for b in BUILDS:
    if data[b]: table(f"## set 5 / {b}", data[b])
    else: P(f"\n(no results for {b})")
for a, b in (("unpatched", "mp20"), ("unpatched", "mp22"), ("mp20", "mp22")):
    x, y = data[a], data[b]
    if not x or not y: continue
    ch = Counter((x[k][3], y[k][3]) for k in x if k in y)
    P(f"\n## verdict change {a} -> {b}"); P(f"| {a} | {b} | rows |"); P("|---|---|---:|")
    for (p, q), n in sorted(ch.items()): P(f"| {p} | {q} | {n} |")
    same = sum(1 for k in x if k in y and x[k][3] == "PASS" and x[k][5] == y[k][5]); passed = sum(1 for k in x if k in y and x[k][3] == "PASS")
    P(f"{a}-PASS rows whose output (sha256_16) is identical in {b}: {same} of {passed}")
m = data["mp22"]
bad = sorted(k for k in m if m[k][3] != "PASS")
if bad: P(f"\nMP22 non-PASS rows ({len(bad)}): " + ", ".join(f"{c}.{o}={m[(c, o)][3]}" for c, o in bad[:60]))
lines = [f"{out_sha(c, o)}  {c}.{o}  {r[3]}" for (c, o), r in sorted(m.items())]
open(os.path.join(R, "mp22_output_sha256.txt"), "w").write("\n".join(lines) + "\n")
agg = hashlib.sha256(("\n".join(lines) + "\n").encode()).hexdigest()
open(os.path.join(R, "mp22_aggregate_sha256.txt"), "w").write(agg + "\n")
P(f"\nMP22 outputs: {len(lines)} rows, aggregate sha256 of the sorted '<sha256>  <case.op>  <verdict>' list = {agg}")
c = Counter(v[3] for v in m.values()); nf, nr = c["FAIL"], c["REFUSED"]
complete = all(len(data[b]) == want for b in BUILDS)
if not complete: P("incomplete: rows per build " + ", ".join(f"{b}={len(data[b])}/{want}" for b in BUILDS))
ok = complete and nf == 0 and nr == 0 and len(m) == want
P(f"\nMP22b: 0 FAIL and 0 REFUSED on this platform: {'MET' if ok else 'NOT MET'} ({nf} FAIL, {nr} REFUSED, {len(m)} of {want} MP22 rows; "
  f"unpatched FAIL {Counter(v[3] for v in data['unpatched'].values())['FAIL']}, MP20 FAIL {Counter(v[3] for v in data['mp20'].values())['FAIL']})")
if not ok: print(f"::warning title=MP22b NOT MET on {arch}::{nf} FAIL, {nr} REFUSED, {len(m)}/{want} rows (informational job), see the report")
open(os.path.join(R, "report.md"), "w").write("\n".join(out) + "\n")
if os.environ.get("GITHUB_STEP_SUMMARY"):
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f: f.write("\n".join(out) + "\n")
