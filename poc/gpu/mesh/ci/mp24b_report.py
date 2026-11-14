#!/usr/bin/env python3
"""MP24b report: pipeline v2 (geogram_fidelity2) on the MP24 families (G1, offset, thin, set 3, set 5) and the fresh MP24b families f2 (PLAN.md "MP24b amended rule").
usage: python3 -I mp24b_report.py <WORK> <arch-tag>   Reads WORK/score24b/{g1,off,thin,set3,set5,f2}.tsv (and the output meshes in WORK/score24b/<name>_<k>/).
Prints per-family tables, per-family path counts (recentred / c0 / refused), the MP24 bars re-evaluated under v2, the MP24b acceptance (0 FAIL; every refusal of a valid pair is a hard limit,
listed with its class and reason), overhead, translate-back error, refusals by reason, aggregate hashes and the bar line 'MP24b: 0 FAIL and only hard-limit refusals: MET/NOT MET'. Exit status 0 always."""
import csv, glob, hashlib, os, re, statistics, sys
from collections import Counter

work, arch = sys.argv[1], sys.argv[2]
S = os.path.join(work, "score24b"); R = os.path.join(work, "report24b"); os.makedirs(R, exist_ok=True)
FAM = ["g1", "off", "thin", "set3", "set5", "f2"]
out = []
def P(s=""): print(s); out.append(s)
def load(n):
    f = os.path.join(S, n + ".tsv")
    return {(r[0], r[1]): r for r in list(csv.reader(open(f), delimiter="\t"))[1:]} if os.path.exists(f) else {}
data = {n: load(n) for n in FAM}
def cls(n, cid):
    p = cid.split("_"); return p[0] if n == "g1" else p[1] if len(p) > 1 else cid
def vol(r):
    m = re.search(r"volume=(\w+)", r[6]); return m.group(1) if m else "-"
def fid(r):
    return dict(re.findall(r"(\w+)=([^ ]+)", r[7])) if len(r) > 7 and r[7] else {}
def table(n):
    rows = data[n]; P(f"\n## {n} ({len(rows)} rows)"); P("| class | rows | PASS | FAIL | REFUSED | other | volume exact | volume close | path recentred | path c0 |"); P("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for k in sorted({cls(n, c) for (c, _o) in rows}) + ["ALL"]:
        sub = [v for (c, _o), v in rows.items() if k == "ALL" or cls(n, c) == k]; c = Counter(v[3] for v in sub); t = len(sub); pc = Counter(fid(v).get("path") for v in sub)
        P(f"| {k} | {t} | {c['PASS']} | {c['FAIL']} | {c['REFUSED']} | {t - c['PASS'] - c['FAIL'] - c['REFUSED']} | {sum(1 for v in sub if vol(v) == 'exact')} | {sum(1 for v in sub if vol(v) == 'close')} | {pc['recentred']} | {pc['c0']} |")

P(f"# MP24b pipeline v2 (validate, exact scale, exact re-centre or c = 0 or hard-limit refusal, Geogram MP20+MP22, translate back, scale back) on {arch}")
for n in FAM:
    if data[n]: table(n)
    else: P(f"\n(no results for {n})")
def cnt(n, verdict, kind=None): return sum(1 for v in data[n].values() if v[3] == verdict and (kind is None or v[2] == kind))
ok = {}
# MP24 bars re-run under v2
g = data["g1"]; must = ["box_huge_offset_overlap", "box_huge_offset_one_ulp", "box_vertex_touch"]
mustrows = [(k, g[k][3]) for k in sorted(g) if k[0] in must]
ok[1] = len(g) == 93 and cnt("g1", "FAIL") == 0 and len(mustrows) == 9 and all(v == "PASS" for _, v in mustrows)
P(f"\nbar 1 (G1 93 rows: 0 FAIL on valid and on malformed; the 9 huge-offset and vertex-touch rows PASS): rows {len(g)}, valid FAIL {cnt('g1','FAIL','valid')}, malformed FAIL {cnt('g1','FAIL','malformed')}, "
  f"valid REFUSED {cnt('g1','REFUSED','valid')}, malformed REFUSED {cnt('g1','REFUSED','malformed')}; must-pass rows PASS {sum(1 for _, v in mustrows if v=='PASS')}/{len(mustrows)}; non-PASS valid: "
  + (", ".join(f"{c}.{o}={v[3]}" for (c, o), v in sorted(g.items()) if v[2] == "valid" and v[3] != "PASS") or "none"))
o = data["off"]; npairs_o = len({c for (c, _x) in o})
ok[2] = len(o) > 0 and cnt("off", "FAIL") == 0
P(f"\nbar 2 (offset family, {npairs_o} pairs / {len(o)} rows: 0 FAIL; under v2 the span54 pairs take the c = 0 path instead of refusing): FAIL {cnt('off','FAIL')}, REFUSED {cnt('off','REFUSED')}, span54 rows PASS {sum(1 for k in o if 'span54' in k[0] and o[k][3]=='PASS')}/{sum(1 for k in o if 'span54' in k[0])}, b53 rows PASS {sum(1 for k in o if 'b53' in k[0] and o[k][3]=='PASS')}/{sum(1 for k in o if 'b53' in k[0])}")
t = data["thin"]; npairs_t = len({c for (c, _x) in t})
ok[3] = len(t) > 0 and cnt("thin", "FAIL") == 0
P(f"\nbar 3 (thin family, {npairs_t} pairs / {len(t)} rows: 0 FAIL): FAIL {cnt('thin','FAIL')}, REFUSED {cnt('thin','REFUSED')}, volume exact {sum(1 for v in t.values() if vol(v)=='exact')}, close {sum(1 for v in t.values() if vol(v)=='close')}")
ok[4] = all(data[n] and cnt(n, "FAIL") == 0 and cnt(n, "REFUSED") == 0 for n in ("set3", "set5"))
P(f"\nbar 4 (touch set 3 and set 5: 0 FAIL, 0 REFUSED; bar-4 style refusals): set3 {len(data['set3'])} rows FAIL {cnt('set3','FAIL')} REFUSED {cnt('set3','REFUSED')}; set5 {len(data['set5'])} rows FAIL {cnt('set5','FAIL')} REFUSED {cnt('set5','REFUSED')}")
# MP24b acceptance on f2 and on every valid row
f2 = data["f2"]; T = None
for v in f2.values():
    d = fid(v)
    if "T_exp" in d: T = int(d["T_exp"]); break
hard = {k: v for k, v in f2.items() if v[3] == "REFUSED"}
okhard = {k for k, v in hard.items() if re.search(r"REFUSED: hard limit", v[6]) and cls("f2", k[0]) in ("cfar", "uflow")}
def ratio_ok(v):  # a far hard-limit refusal must be above T; an underflow refusal has its own reason
    d = fid(v)
    return "underflow" in v[6] or (T is not None and float(d.get("ratio", "0")) > 2.0 ** T)
badhard = sorted(k for k in hard if k not in okhard or not ratio_ok(hard[k]))
nonhard_valid = sorted((n, k) for n in FAM for k, v in data[n].items() if v[3] == "REFUSED" and v[2] == "valid" and not re.search(r"REFUSED: hard limit", v[6]))
fails_all = sorted((n, k) for n in FAM for k, v in data[n].items() if v[3] not in ("PASS", "REFUSED", "ACCEPTED-UNVERIFIED"))
hard_all = [(n, k, v) for n in FAM for k, v in data[n].items() if v[3] == "REFUSED" and v[2] == "valid" and re.search(r"REFUSED: hard limit", v[6])]
npairs = len({c for (c, _x) in f2})
ok[5] = len(f2) > 0 and npairs >= 600 and not fails_all and not badhard and not nonhard_valid
P(f"\nMP24b acceptance (f2: {npairs} pairs / {len(f2)} rows; T = 2^{T}): FAIL {cnt('f2','FAIL')} (all families: {len(fails_all)}), REFUSED {len(hard)} (hard-limit refusals in documented classes {len(okhard)}; undocumented {len(badhard)}), non-hard-limit refusals of valid rows in any family {len(nonhard_valid)}, "
  f"PASS {cnt('f2','PASS')}")
for n, k in fails_all: P(f"  FAIL {n}:{k[0]}.{k[1]}: {data[n][k][6][:200]}")
for k in badhard: P(f"  UNDOCUMENTED REFUSAL {k[0]}.{k[1]}: {hard[k][6][:200]}")
for n, k in nonhard_valid: P(f"  NON-HARD-LIMIT REFUSAL (valid row) {n}:{k[0]}.{k[1]}: {data[n][k][6][:200]}")
hc = Counter((cls("f2", k[0]), "underflow" if "underflow" in v[6] else "far from the origin") for k, v in hard.items())
P("hard-limit refusals by class (f2): " + ("; ".join(f"{c} / {r}: {x}" for (c, r), x in sorted(hc.items())) or "none") + f"; hard-limit refusals in the older families: {sum(1 for n, k, v in hard_all if n != 'f2')}")
cl = Counter((cls("f2", c), v[3]) for (c, _o), v in f2.items())
P("f2 verdicts by class: " + ", ".join(f"{c}: " + "/".join(f"{x} {cl[(c, x)]}" for x in ("PASS", "FAIL", "REFUSED") if cl[(c, x)]) for c in sorted({c for c, _ in cl})))
farpass = [k for k, v in f2.items() if cls("f2", k[0]) == "cfar" and v[3] == "PASS"]
P(f"cfar rows that PASSED anyway (exactly re-centrable or ratio <= T): {len(farpass)} of {sum(1 for k in f2 if cls('f2', k[0]) == 'cfar')}")
# overhead and translate-back
allrows = [(n, k, v) for n in FAM for k, v in data[n].items() if v[3] == "PASS" and v[7]]
maxulp = (0.0, ""); rounded = 0; tot = 0; ratios = {n: [] for n in FAM}; ovh = []; bms = []; paths = Counter()
for n, k, v in allrows:
    d = fid(v)
    if "translate_back_max_err_ulps" not in d: continue
    paths[(n, d["path"])] += 1
    u = float(d["translate_back_max_err_ulps"])
    if u > maxulp[0]: maxulp = (u, f"{n}:{k[0]}.{k[1]}")
    a, b = d["rounded_coords"].split("/"); rounded += int(a); tot += int(b)
    o_ms = float(d["validate_ms"]) + float(d["recentre_ms"]) + float(d["back_ms"]); b_ms = float(d["boolean_ms"])
    if v[2] == "valid" and b_ms > 0: ratios[n].append(o_ms / b_ms); ovh.append(o_ms); bms.append(b_ms)
allr = [x for n in FAM for x in ratios[n]]
med = statistics.median(allr) if allr else float("nan")
P(f"\noverhead of validate + scale + re-centre + translate/scale back vs the boolean (median over valid PASS rows): median of per-row ratios {100*med:.2f}% (ratio of medians {100*statistics.median(ovh)/statistics.median(bms) if ovh else float('nan'):.2f}%) over {len(allr)} rows; "
  + "per family " + ", ".join(f"{n} {100*statistics.median(ratios[n]):.2f}%" for n in FAM if ratios[n]) + " (in-process stage timers; scoring ran in parallel)")
P(f"translate-back error: maximum {maxulp[0]:.6g} ulps of the original-frame value (row {maxulp[1] or '-'}); {rounded} of {tot} result coordinates rounded by the translate-back addition over {len(allrows)} PASS rows (the representation limit; Geogram's own rounding in the working frame is not included)")
P("paths of PASS rows: " + ", ".join(f"{n}/{p} {c}" for (n, p), c in sorted(paths.items())))
rs = Counter()
for n in FAM:
    for k, v in data[n].items():
        if v[3] == "REFUSED": rs[(n, re.sub(r"\(axis.*", "", re.sub(r"^.*?REFUSED: ", "", v[6]))[:50] + ("" if "hard limit" in v[6] else "")) ] += 1
P("\nrefusals by reason: " + ("; ".join(f"{n}: {r} x{c}" for (n, r), c in sorted(rs.items())) or "none"))
def out_sha(n, cid, op):
    for d in glob.glob(os.path.join(S, f"{n}_*")):
        f = os.path.join(d, f"{cid}.{op}.msh")
        if os.path.exists(f): return hashlib.sha256(open(f, "rb").read()).hexdigest()
    return "-"
lines = [f"{out_sha(n, c, op)}  {n}:{c}.{op}  {v[3]}" for n in FAM for (c, op), v in sorted(data[n].items())]
verd = [f"{n}:{c}.{op}  {v[3]}" for n in FAM for (c, op), v in sorted(data[n].items())]
open(os.path.join(R, "mp24b_output_sha256.txt"), "w").write("\n".join(lines) + "\n")
open(os.path.join(R, "mp24b_verdicts.txt"), "w").write("\n".join(verd) + "\n")
agg = hashlib.sha256(("\n".join(lines) + "\n").encode()).hexdigest(); aggv = hashlib.sha256(("\n".join(verd) + "\n").encode()).hexdigest()
P(f"\nplatforms: MP24b outputs: {len(lines)} rows; aggregate sha256 of the sorted '<sha256>  <family:case.op>  <verdict>' list = {agg}; aggregate of the verdict list alone = {aggv}. "
  "Identical verdicts and identical bytes between x86_64 and aarch64 are decided by comparing these two lines across the two artifacts (M1 vs Linux bytes may differ).")
okmp24 = [i for i in range(1, 5) if not ok[i]]
P(f"\nMP24 bars under v2: bar 1 {'MET' if ok[1] else 'NOT MET'}, bar 2 {'MET' if ok[2] else 'NOT MET'}, bar 3 {'MET' if ok[3] else 'NOT MET'}, bar 4 {'MET' if ok[4] else 'NOT MET'}")
P(f"MP24b: 0 FAIL and only hard-limit refusals: {'MET' if ok[5] else 'NOT MET'} ({len(fails_all)} FAIL rows over all families, {len(hard_all)} hard-limit refusals of valid rows (all documented classes: {not badhard}), {len(nonhard_valid)} other refusals of valid rows); "
  f"max translate-back error {maxulp[0]:.6g} ulps; aggregate output hash {agg[:16]}")
if not ok[5] or okmp24: print(f"::warning title=MP24b on {arch}::MP24b {'NOT MET' if not ok[5] else 'met'}; MP24 bars not met under v2: {okmp24}; see the report")
open(os.path.join(R, "report.md"), "w").write("\n".join(out) + "\n")
if os.environ.get("GITHUB_STEP_SUMMARY"):
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f: f.write("\n".join(out) + "\n")
