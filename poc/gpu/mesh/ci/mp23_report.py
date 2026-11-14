#!/usr/bin/env python3
"""MP23 report: Manifold v3.5.5 serial build, G1 corpus, tier-3 measurement (informational).
usage: python3 -I mp23_report.py <WORK> <arch-tag> <M1 scoring_results.tsv>
Reads WORK/score/run1/results.tsv (score_library.py) and WORK/score/run2.tsv (second run: case op rc sha256). Writes WORK/report/*.
Exit status is always 0. Tier 3 = outputs byte-identical across platforms: this script compares with the M1 (sha256_16 column of the reference);
x86_64 vs aarch64 is compared after the CI run by the aggregate hash (report/aggregate_sha256.txt) or output_sha256.txt of the two artifacts."""
import csv, hashlib, os, sys
from collections import Counter

work, arch, refp = sys.argv[1], sys.argv[2], sys.argv[3]
S = os.path.join(work, "score"); R = os.path.join(work, "report"); os.makedirs(R, exist_ok=True)
out = []
def P(s=""): print(s); out.append(s)
def load(p): return {(r[0], r[1]): r for r in list(csv.reader(open(p), delimiter="\t"))[1:]}
rows, ref = load(os.path.join(S, "run1", "results.tsv")), load(refp)
VERD = ["PASS", "FAIL", "REFUSED", "ACCEPTED-UNVERIFIED"]
def cls(cid): return cid.split("_")[0]

P(f"# MP23 Manifold v3.5.5 serial build (-ffp-contract=off), G1 corpus, {arch}")
P(f"\nrows scored: {len(rows)} (M1 reference: {len(ref)})")
for kind in ("valid", "malformed"):
    sub = {k: v for k, v in rows.items() if v[2] == kind}
    P(f"\n## {kind} inputs"); P("| class | rows | PASS | FAIL | REFUSED | ACCEPTED-UNVERIFIED | other |"); P("|---|---:|---:|---:|---:|---:|---:|")
    for c in sorted({cls(k[0]) for k in sub}) + ["ALL"]:
        cnt = Counter(v[3] for k, v in sub.items() if c == "ALL" or cls(k[0]) == c); n = sum(cnt.values())
        P(f"| {c} | {n} | " + " | ".join(str(cnt[v]) for v in VERD) + f" | {n - sum(cnt[v] for v in VERD)} |")
P("\nM1 registered result (reference): valid 57 PASS / 15 FAIL; malformed 12 REFUSED / 6 ACCEPTED-UNVERIFIED / 3 PASS / 0 FAIL")

# verdict diff against the M1
P("\n## verdict diff against lib/manifold/scoring_results.tsv (M1)")
common = [k for k in rows if k in ref]
vd = sorted(k for k in common if rows[k][3] != ref[k][3] or rows[k][2] != ref[k][2])
dd = sorted(k for k in common if rows[k][6] != ref[k][6])
P(f"common rows {len(common)}; verdict (or kind) differs on {len(vd)}; detail text differs on {len(dd)} (detail has no timings)")
for k in vd[:40]: P(f"  VERDICT {k[0]}.{k[1]}: M1 {ref[k][3]} -> here {rows[k][3]}")
for k in [k for k in dd if k not in vd][:20]: P(f"  DETAIL  {k[0]}.{k[1]}: M1 [{ref[k][6][:110]}] here [{rows[k][6][:110]}]")
if len(common) != len(ref) or len(common) != len(rows): P("  row sets differ: " + str(sorted(set(ref) ^ set(rows))[:10]))

# full sha256 of every output, aggregate
def full_sha(cid, op):
    f = os.path.join(S, "run1", f"{cid}.{op}.msh")
    return hashlib.sha256(open(f, "rb").read()).hexdigest() if os.path.exists(f) else "-"
lines = sorted(f"{full_sha(c, o)} {c}.{o} {r[3]}" for (c, o), r in rows.items())
open(os.path.join(R, "output_sha256.txt"), "w").write("\n".join(lines) + "\n")
agg = hashlib.sha256(("\n".join(lines) + "\n").encode()).hexdigest()
open(os.path.join(R, "aggregate_sha256.txt"), "w").write(agg + "\n")
P(f"\n## aggregate\n{len(lines)} rows; aggregate sha256 of the sorted '<sha256> <case.op> <verdict>' lines = {agg}")
P(f"output files written: {sum(1 for l in lines if not l.startswith('- '))}, no file (refused/error): {sum(1 for l in lines if l.startswith('- '))}")

# run-to-run
P("\n## run-to-run identity (two runs of the same binary on this machine)")
r2p = os.path.join(S, "run2.tsv")
if os.path.exists(r2p):
    r2 = {(r[0], r[1]): r for r in csv.reader(open(r2p), delimiter="\t")}
    rc1 = lambda k: 0 if rows[k][3] in ("PASS", "FAIL", "ACCEPTED-UNVERIFIED") else 3 if rows[k][3] == "REFUSED" else -1
    dh = [k for k in rows if k in r2 and full_sha(*k) != r2[k][3]]
    drc = [k for k in rows if k in r2 and rc1(k) != int(r2[k][2])]
    both = sum(1 for k in rows if k in r2 and full_sha(*k) != "-")
    P(f"{len(r2)} pairs rerun; output files compared {both}; sha256 differs on {len(dh)}; exit status differs on {len(drc)} (run 1 status derived from the verdict)")
    P(f"run-to-run: {'IDENTICAL' if not dh and not drc and len(r2) == len(rows) else 'DIFFERS ' + ', '.join(f'{c}.{o}' for c, o in (dh + drc)[:10])}")
else: P("run2.tsv missing")

# tier 3 versus the M1
P("\n## tier 3 (outputs byte-identical across platforms; measured, not required)")
withfile = [k for k in ref if ref[k][5] != "-"]
same = [k for k in withfile if k in rows and rows[k][5] == ref[k][5]]
diff = [k for k in withfile if k in rows and rows[k][5] != ref[k][5]]
nofile_both = [k for k in ref if ref[k][5] == "-" and k in rows and rows[k][5] == "-"]
P(f"TIER 3 vs M1 ({arch} vs Darwin-arm64): {len(same)} of {len(withfile)} output files byte-identical (sha256_16 equal); {len(diff)} differ; "
  f"{len(nofile_both)} rows write no file on either platform")
if diff: P("  differing outputs: " + ", ".join(f"{c}.{o}" for c, o in sorted(diff)[:30]))
P("x86_64 vs aarch64: compare report/aggregate_sha256.txt (and output_sha256.txt) of the two CI artifacts; equal aggregates mean all outputs and verdicts are byte-identical.")
if diff or vd: print(f"::warning title=MP23 tier 3 on {arch}::{len(diff)} outputs differ from the M1, {len(vd)} verdicts differ (informational)")
open(os.path.join(R, "report.md"), "w").write("\n".join(out) + "\n")
if os.environ.get("GITHUB_STEP_SUMMARY"):
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f: f.write("\n".join(out) + "\n")
