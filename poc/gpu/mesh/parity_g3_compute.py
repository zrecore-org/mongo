#!/usr/bin/env python3
"""Recomputes the G3 coverage tables with the PARITY.md definitions. Reads parity.csv (baseline check) and parity_g3.csv.
Counted rows = groups other than types, io_utilities, beyond_sfcgal (84 rows). without = C/N; with = (C+P+W)/N; reference (C+P)/N.
View A: `status` column. View B (fidelity): ONE threshold for booleans, offsets and measures alike: a route meets the requirement only if it
stays within 1e-12 of the operation's scale on the tested corpus (the bar MP08a declared). Bit-exactness is a separate third column.
B classes: C = covered and (no exactness needed, or exact_basis meets-tested); CU = covered but bar untested or pending MP24 (counts in 'with' only);
covered with exact_basis fails-tested becomes W if an exact route is sized (exact_route_pw) else UN (counted nowhere).
Whether a needs_exactness='maybe' row needs it is a policy: all yes, all no, or the per-row decision in parity_g3.csv (maybe_decision).
Pending MP24 (3D booleans, rows 45-47): shown counted as meeting the bar, and as not."""
import csv, re
EXCL = {"types", "io_utilities", "beyond_sfcgal"}
BUCKETS = [("2D", {"2D"}), ("3D", {"3D"}), ("2.5D", {"2.5D"}), ("3D+2.5D", {"3D", "2.5D"})]
def load(p): return [r for r in csv.DictReader(open(p)) if r["group"] not in EXCL]
def pw(s):
    m = re.findall(r"\d+", s or ""); return (int(m[0]), int(m[-1])) if m else None
KEY = {"covered-by-permissive": "C", "covered-exactness-unproven": "CU", "partial": "P", "write-ourselves": "W",
       "infeasible-in-product": "I", "unverified": "U", "unsized-exact-route": "UN"}
def cls(r, col, maybe="decided", pending_ok=False):
    if col == "status": return KEY[r["status"]]
    a = r["status"]
    if a != "covered-by-permissive": return KEY[a]
    nx = r["needs_exactness"]
    need = nx == "yes" or (nx == "maybe" and (maybe == "yes" or (maybe == "decided" and r["maybe_decision"] == "yes")))
    b = r["exact_basis"]
    if not need or b in ("meets-tested", "not-needed") or (pending_ok and b == "pending-MP24"): return "C"
    if b in ("untested", "pending-MP24"): return "CU"
    return "W" if r["exact_route_pw"] else "UN"
def counts(rows, dims, **kw):
    rs = [r for r in rows if r["dim"] in dims]; c = {k: 0 for k in "C CU P W I U UN".split()}
    for r in rs: c[cls(r, **kw)] += 1
    n = len(rs); return n, c, c["C"] / n, (c["C"] + c["CU"] + c["P"] + c["W"]) / n, (c["C"] + c["CU"] + c["P"]) / n
def table(rows, col, title, **kw):
    print(f"\n{title}\n{'bucket':8} {'N':>3} {'C':>3} {'CU':>3} {'P':>3} {'W':>3} {'I':>3} {'U':>3} {'UN':>3}  without  with   (C+P)")
    for name, dims in BUCKETS:
        n, c, wo, wi, cp = counts(rows, dims, col=col, **kw)
        print(f"{name:8} {n:>3} {c['C']:>3} {c['CU']:>3} {c['P']:>3} {c['W']:>3} {c['I']:>3} {c['U']:>3} {c['UN']:>3}  {wo*100:5.1f}%  {wi*100:5.1f}%  {cp*100:5.1f}%")
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
table(g3, "status", "A. G3 plain status (tested-evidence-adjusted, same definitions)")
est(g3, "G3")
print("\nB. fidelity view, one threshold (<= 1e-12 of scale on the tested corpus). 'without' / 'with' per scenario.")
print("   'with' assumes own code meets the bar (see footnote in PARITY_G3.md); it does not depend on the MP24 scenario.")
SC = [("maybe=all no ", "no"), ("maybe=decided", "decided"), ("maybe=all yes", "yes")]
print(f"{'':16} {'':9} {'3D bool pending MP24 counted MEETING':>40} | {'3D bool pending MP24 NOT counted':>34}")
for name, dims in BUCKETS:
    for lab, m in SC:
        a = counts(g3, dims, col="B", maybe=m, pending_ok=True); b = counts(g3, dims, col="B", maybe=m, pending_ok=False)
        print(f"{name:8} {lab:14} without {a[2]*100:5.1f}%  with {a[3]*100:5.1f}%   | without {b[2]*100:5.1f}%  with {b[3]*100:5.1f}%")
for lab, m in SC:
    for pend in (True, False):
        table(g3, "B", f"B detail: {lab}, pending MP24 {'counted as meeting' if pend else 'not counted'}", maybe=m, pending_ok=pend)
print("\nHeadline range for B (min over scenarios .. max over scenarios), without / with:")
for name in ("2D", "3D"):
    dims = dict(BUCKETS)[name]
    v = [counts(g3, dims, col="B", maybe=m, pending_ok=p) for _, m in SC for p in (True, False)]
    print(f"  {name}: without {min(x[2] for x in v)*100:.1f}%..{max(x[2] for x in v)*100:.1f}%  with {min(x[3] for x in v)*100:.1f}%..{max(x[3] for x in v)*100:.1f}%")
print("\nBit-exactness (third column, separate): covered rows with bit_exact == yes / N")
for name, dims in BUCKETS[:2]:
    rs = [r for r in g3 if r["dim"] in dims]
    k = sum(1 for r in rs if r["status"] == "covered-by-permissive" and r["bit_exact"] == "yes")
    no = [r["operation"] for r in rs if r["bit_exact"] == "no"]
    print(f"  {name}: {k}/{len(rs)} = {k/len(rs)*100:.1f}%; tested NOT bit-exact: {len(no)} ({', '.join(no)})")
print("\nBars (suggested 70% without, 90% with), by scenario extremes:")
for name in ("2D", "3D"):
    dims = dict(BUCKETS)[name]
    v = [counts(g3, dims, col="B", maybe=m, pending_ok=p) for _, m in SC for p in (True, False)]
    a = counts(g3, dims, col="status")
    print(f"  {name}: A {a[2]*100:.1f}/{a[3]*100:.1f}; B best case without {max(x[2] for x in v)*100:.1f}% ({'MET' if max(x[2] for x in v)>=.7 else 'not met'}), B best case with {max(x[3] for x in v)*100:.1f}% ({'MET' if max(x[3] for x in v)>=.9 else 'not met'})")
print("\nneeds_exactness='maybe' decisions:")
for r in csv.DictReader(open("parity_g3.csv")):
    if r["maybe_decision"]: print(f"  {r['maybe_decision']:3} {r['dim']:4} {r['operation'][:42]:42} {r['maybe_reason']}")
print("\nRows covered in A with bar untested or pending MP24 (needs_exactness yes/maybe):")
for r in g3:
    if r["status"] == "covered-by-permissive" and r["exact_basis"] in ("untested", "pending-MP24") and r["needs_exactness"] in ("yes", "maybe"):
        print(f"  {r['dim']:4} {r['operation'][:60]} [{r['exact_basis']}]")
print("\nRows in B with no sized exact route (UN):")
for r in g3:
    if cls(r, "B", maybe="decided") == "UN": print(f"  {r['dim']:4} {r['operation']}")
print("\nRemaining infeasible/unverified rows in counted groups:")
for r in g3:
    if r["status"] in ("infeasible-in-product", "unverified"): print(f"  {r['dim']:4} {r['status']:22} {r['operation'][:60]}")
