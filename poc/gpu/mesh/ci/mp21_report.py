#!/usr/bin/env python3
"""MP21a report: per-architecture verdict tables (patched vs unpatched Geogram), per-output sha256 of the patched builds, and an
optional comparison with reference TSVs (the M1 results of MP20).
usage: python3 -I mp21_report.py <WORK> <arch-tag> [--ref DIR]
Reads WORK/score/{unpatched,patched}_{touch,set4}.tsv and the output meshes in WORK/score/<name>_<k>/. Writes WORK/report/*.
Exit status is 0 whatever the verdicts are (informational); the bar (0 FAIL patched) is only printed."""
import csv, hashlib, os, sys
from collections import Counter, defaultdict

work, arch = sys.argv[1], sys.argv[2]
ref = sys.argv[sys.argv.index("--ref") + 1] if "--ref" in sys.argv else None
S = os.path.join(work, "score"); R = os.path.join(work, "report"); os.makedirs(R, exist_ok=True)
REFNAMES = {"patched_touch": "p_touch.tsv", "unpatched_touch": "u_touch.tsv", "patched_set4": "s4_p.tsv", "unpatched_set4": "s4_u.tsv"}
VERDICTS = ["PASS", "FAIL", "REFUSED"]
out = []
def P(s=""): print(s); out.append(s)

def load(name):
    p = os.path.join(S, name + ".tsv")
    return {(r[0], r[1]): r for r in list(csv.reader(open(p), delimiter="\t"))[1:]} if os.path.exists(p) else {}

def cls(cid): return "_".join(cid.split("_")[:2]) if cid.startswith("touch_") else cid.split("_")[1]

def table(title, rows):
    classes = sorted({cls(c) for (c, _o) in rows}); P(f"\n{title}"); P(f"| class | rows | PASS | FAIL | REFUSED | other |"); P("|---|---:|---:|---:|---:|---:|")
    tot = Counter()
    for k in classes + ["ALL"]:
        c = Counter(v[3] for (cid, op), v in rows.items() if k == "ALL" or cls(cid) == k)
        n = sum(c.values()); oth = n - sum(c[v] for v in VERDICTS); tot[k] = c["FAIL"]
        P(f"| {k} | {n} | {c['PASS']} | {c['FAIL']} | {c['REFUSED']} | {oth} |")
    return tot["ALL"]

def out_sha(name, cid, op):
    for k in range(64):
        f = os.path.join(S, f"{name}_{k}", f"{cid}.{op}.msh")
        if os.path.exists(f): return hashlib.sha256(open(f, "rb").read()).hexdigest()
    return "-"

P(f"# MP21a Geogram cross-platform check, {arch}")
data = {n: load(n) for n in REFNAMES}
fails = {}
for corpus in ("touch", "set4"):
    for b in ("unpatched", "patched"):
        rows = data[f"{b}_{corpus}"]
        if not rows: P(f"\n(no results for {b}_{corpus})"); continue
        fails[(b, corpus)] = table(f"## {corpus} / {b}", rows)
for corpus in ("touch", "set4"):
    u, p = data[f"unpatched_{corpus}"], data[f"patched_{corpus}"]
    if not u or not p: continue
    # verdict changes unpatched -> patched
    ch = Counter((u[k][3], p[k][3]) for k in u if k in p)
    P(f"\n## {corpus}: verdict change unpatched -> patched"); P("| unpatched | patched | rows |"); P("|---|---|---:|")
    for (a, b), n in sorted(ch.items()): P(f"| {a} | {b} | {n} |")
    same = sum(1 for k in u if k in p and u[k][3] == "PASS" and u[k][5] == p[k][5])
    passed = sum(1 for k in u if k in p and u[k][3] == "PASS")
    P(f"unpatched-PASS rows whose output (sha256_16) is identical in the patched build: {same} of {passed}")
    bad = sorted(k for k in p if p[k][3] != "PASS")
    if bad: P(f"patched non-PASS rows ({len(bad)}): " + ", ".join(f"{c}.{o}={p[(c, o)][3]}" for c, o in bad[:40]))

# per-output sha256 of the patched build (for comparing the three platforms) and an aggregate over everything
for b in ("patched", "unpatched"):
    lines = []
    for corpus in ("touch", "set4"):
        for (cid, op), r in sorted(data[f"{b}_{corpus}"].items()):
            lines.append(f"{out_sha(f'{b}_{corpus}', cid, op)}  {cid}.{op}  {r[3]}")
    open(os.path.join(R, f"{b}_output_sha256.txt"), "w").write("\n".join(lines) + "\n")
    agg = hashlib.sha256(("\n".join(lines) + "\n").encode()).hexdigest()
    P(f"\n{b} outputs: {len(lines)} rows, aggregate sha256 of the sorted '<sha256>  <case.op>  <verdict>' list = {agg}")
    open(os.path.join(R, f"{b}_aggregate_sha256.txt"), "w").write(agg + "\n")

# comparison with reference TSVs (M1 results of MP20), restricted to the rows run here
if ref:
    P(f"\n## comparison with reference results in {ref}")
    for n, fn in REFNAMES.items():
        fp = os.path.join(ref, fn)
        if not os.path.exists(fp) or not data[n]: P(f"{n}: reference {fn} not found or no rows"); continue
        rr = {(r[0], r[1]): r for r in list(csv.reader(open(fp), delimiter="\t"))[1:]}
        common = [k for k in data[n] if k in rr]
        vd = [k for k in common if data[n][k][3] != rr[k][3]]
        hd = [k for k in common if data[n][k][5] != rr[k][5]]
        P(f"{n}: {len(common)} common rows; verdict differs on {len(vd)}; output sha256_16 differs on {len(hd)}"
          + ("  e.g. " + ", ".join(f"{c}.{o}" for c, o in (vd or hd)[:5]) if (vd or hd) else ""))

pf = fails.get(("patched", "touch"), 0) + fails.get(("patched", "set4"), 0)
P(f"\nBar (MP21a): 0 FAIL patched on this architecture: {'MET' if pf == 0 and fails else 'NOT MET'} ({pf} patched FAIL rows; unpatched FAIL rows: "
  f"{fails.get(('unpatched', 'touch'), 0)} touch, {fails.get(('unpatched', 'set4'), 0)} set 4)")
if pf: print(f"::warning title=MP21a patched FAIL on {arch}::{pf} patched FAIL rows (informational), see the report")
open(os.path.join(R, "report.md"), "w").write("\n".join(out) + "\n")
if os.environ.get("GITHUB_STEP_SUMMARY"):
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f: f.write("\n".join(out) + "\n")
