#!/usr/bin/env python3
"""Recomputes the G3 coverage tables with the PARITY.md definitions. Reads parity.csv (baseline check) and parity_g3.csv.
Counted rows = groups other than types, io_utilities, beyond_sfcgal (84 rows). without = C/N; with = (C+P+W)/N; reference (C+P)/N.
Plain view (A): status column. Fidelity view (B): status_exact column; C counts only covered rows whose route is exact on the tested corpus
(or that need no exactness); 'covered-exactness-unproven' (CU) counts in 'with' only; 'unsized-exact-route' (UN) counts nowhere (like U/I)."""
import csv, re
EXCL = {"types", "io_utilities", "beyond_sfcgal"}
BUCKETS = [("2D", {"2D"}), ("3D", {"3D"}), ("2.5D", {"2.5D"}), ("3D+2.5D", {"3D", "2.5D"})]
def load(p): return [r for r in csv.DictReader(open(p)) if r["group"] not in EXCL]
def pw(s):
    m = re.findall(r"\d+", s or ""); return (int(m[0]), int(m[-1])) if m else None
def table(rows, col, title, thin_counts=False, exact_needed=("yes", "maybe")):
    print(f"\n{title}\n{'bucket':8} {'N':>3} {'C':>3} {'CU':>3} {'P':>3} {'W':>3} {'I':>3} {'U':>3} {'UN':>3}  without  with   (C+P)")
    res = {}
    for name, dims in BUCKETS:
        rs = [r for r in rows if r["dim"] in dims]
        n = len(rs); c = {k: 0 for k in "C CU P W I U UN".split()}
        for r in rs:
            s = r[col]
            if col == "status_exact":
                # re-derive B with the chosen exactness-needed set / thin treatment
                a = r["status"]
                if a == "covered-by-permissive":
                    b = r["exact_basis"]; need = r["needs_exactness"] in exact_needed
                    if not need or b in ("exact-tested", "not-needed") or (thin_counts and b == "exact-thin"): s = "covered-by-permissive"
                    elif b in ("untested", "exact-thin"): s = "covered-exactness-unproven"
                    else: s = "write-ourselves" if r["exact_route_pw"] else "unsized-exact-route"
                else: s = a
            k = {"covered-by-permissive": "C", "covered-exactness-unproven": "CU", "partial": "P", "write-ourselves": "W",
                 "infeasible-in-product": "I", "unverified": "U", "unsized-exact-route": "UN"}[s]
            c[k] += 1
        wo = c["C"] / n; wi = (c["C"] + c["CU"] + c["P"] + c["W"]) / n; cp = (c["C"] + c["CU"] + c["P"]) / n
        res[name] = (wo, wi)
        print(f"{name:8} {n:>3} {c['C']:>3} {c['CU']:>3} {c['P']:>3} {c['W']:>3} {c['I']:>3} {c['U']:>3} {c['UN']:>3}  {wo*100:5.1f}%  {wi*100:5.1f}%  {cp*100:5.1f}%")
    return res
def est(rows, label):
    for name, dims in BUCKETS[:3]:
        rs = [r for r in rows if r["dim"] in dims and r["status"] in ("partial", "write-ourselves")]
        lo = sum((pw(r["own_code_estimate"]) or (0, 0))[0] for r in rs); hi = sum((pw(r["own_code_estimate"]) or (0, 0))[1] for r in rs)
        un = sum(1 for r in rs if pw(r["own_code_estimate"]) is None)
        print(f"  {label} {name}: {lo}-{hi} pw over {len(rs)} partial+write-ourselves rows ({un} without an estimate)")
base, g3 = load("parity.csv"), load("parity_g3.csv")
print(f"counted rows: baseline {len(base)}, G3 {len(g3)}")
table(base, "status", "BASELINE check: parity.csv (must equal PARITY.md: 2D 44.4/86.1, 3D 25.0/88.6, 2.5D 50.0/100, 3D+2.5D 27.1/89.6)")
print("baseline estimate sums (PARITY.md says 2D 34, 3D 55, 2.5D 2):"); est(base, "baseline")
A = table(g3, "status", "A. G3 plain status (tested-evidence-adjusted, same definitions)")
est(g3, "G3")
B = table(g3, "status_exact", "B. G3 under the fidelity requirement (needs_exactness yes or maybe must be exact on the tested corpus)")
table(g3, "status_exact", "B-sens-1. as B, but counting exact-thin rows (3D booleans pending MP24, 2D offset) as exact", thin_counts=True)
table(g3, "status_exact", "B-sens-2. as B, but only needs_exactness == yes counts as needing exactness", exact_needed=("yes",))
print("\nBars (suggested 70% without, 90% with):")
for lab, d in (("A", A), ("B", B)):
    for name in ("2D", "3D"):
        wo, wi = d[name]
        print(f"  {lab} {name}: without {wo*100:.1f}% {'MET' if wo>=.7 else 'not met'}; with {wi*100:.1f}% {'MET' if wi>=.9 else 'not met'}")
print("\nRows counted 'covered' in A whose exactness is untested or thin (needs_exactness yes/maybe):")
for r in g3:
    if r["status"] == "covered-by-permissive" and r["exact_basis"] in ("untested", "exact-thin") and r["needs_exactness"] in ("yes", "maybe"):
        print(f"  {r['dim']:4} {r['operation'][:60]} [{r['exact_basis']}]")
print("\nRows in B with no sized exact route (UN):")
for r in g3:
    if r["status_exact"] == "unsized-exact-route": print(f"  {r['dim']:4} {r['operation']}")
print("\nBiggest remaining gaps: infeasible/unverified rows in counted groups:")
for r in g3:
    if r["status"] in ("infeasible-in-product", "unverified"): print(f"  {r['dim']:4} {r['status']:22} {r['operation'][:60]}")
