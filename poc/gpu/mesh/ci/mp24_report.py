#!/usr/bin/env python3
"""MP24 report: the assembled pipeline (geogram_fidelity) scored on G1, the offset family, the thin family, touch sets 3 and 5 (PLAN.md section MP24).
usage: python3 -I mp24_report.py <WORK> <arch-tag>        Reads WORK/score24/{g1,off,thin,set3,set5}.tsv and the output meshes in WORK/score24/<name>_<k>/.
Prints per-family tables, bars 1-5 (this platform), the translate-back error, the refusals with reasons, the aggregate output hash (bar 6; the comparison between platforms is made
from the two artifacts afterwards) and the bar line 'MP24: ...: MET/NOT MET'. Exit status 0 whatever the verdicts are (informational CI job)."""
import csv, glob, hashlib, os, re, statistics, sys
from collections import Counter

work, arch = sys.argv[1], sys.argv[2]
S = os.path.join(work, "score24"); R = os.path.join(work, "report24"); os.makedirs(R, exist_ok=True)
FAM = ["g1", "off", "thin", "set3", "set5"]
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
def table(n):
    rows = data[n]; P(f"\n## {n} ({len(rows)} rows)"); P("| class | rows | PASS | FAIL | REFUSED | other | volume exact | volume close |"); P("|---|---:|---:|---:|---:|---:|---:|---:|")
    for k in sorted({cls(n, c) for (c, _o) in rows}) + ["ALL"]:
        sub = [v for (c, _o), v in rows.items() if k == "ALL" or cls(n, c) == k]; c = Counter(v[3] for v in sub); t = len(sub)
        P(f"| {k} | {t} | {c['PASS']} | {c['FAIL']} | {c['REFUSED']} | {t - c['PASS'] - c['FAIL'] - c['REFUSED']} | {sum(1 for v in sub if vol(v) == 'exact')} | {sum(1 for v in sub if vol(v) == 'close')} |")

P(f"# MP24 assembled pipeline (validate, exact re-centre, Geogram MP20+MP22, translate back) on {arch}")
for n in FAM:
    if data[n]: table(n)
    else: P(f"\n(no results for {n})")
def cnt(n, verdict, kind=None): return sum(1 for v in data[n].values() if v[3] == verdict and (kind is None or v[2] == kind))
def fails(n): return sorted(f"{c}.{o}" for (c, o), v in data[n].items() if v[3] not in ("PASS", "REFUSED", "ACCEPTED-UNVERIFIED"))
ok = {}
# bar 1
g = data["g1"]; must = [f"box_huge_offset_overlap", "box_huge_offset_one_ulp", "box_vertex_touch"]
mustrows = [(k, g[k][3]) for k in sorted(g) if k[0] in must]
ok[1] = len(g) == 93 and cnt("g1", "FAIL") == 0 and all(v == "PASS" for _, v in mustrows) and len(mustrows) == 9
P(f"\nbar 1 (G1 93 rows: 0 FAIL on valid and on malformed; the 6 huge-offset and 3 vertex-touch rows PASS): rows {len(g)}, valid FAIL {cnt('g1','FAIL','valid')}, malformed FAIL {cnt('g1','FAIL','malformed')}, "
  f"valid REFUSED {cnt('g1','REFUSED','valid')}, malformed REFUSED {cnt('g1','REFUSED','malformed')}; must-pass rows PASS {sum(1 for _, v in mustrows if v=='PASS')}/{len(mustrows)}; non-PASS valid: "
  + (", ".join(f"{c}.{o}={v[3]}" for (c, o), v in sorted(g.items()) if v[2] == "valid" and v[3] != "PASS") or "none"))
# bar 2
o = data["off"]; ref = {k: v for k, v in o.items() if v[3] == "REFUSED"}
okref = {k for k, v in ref.items() if "span54" in k[0] and "53 bits" in v[6]}
span54 = [k for k in o if "span54" in k[0]]; b53 = [k for k in o if "b53" in k[0]]
ok[2] = len(o) > 0 and cnt("off", "FAIL") == 0 and set(ref) == okref
npairs_o = len({c for (c, _x) in o})
P(f"\nbar 2 (offset family, {npairs_o} pairs / {len(o)} rows: 0 FAIL, refusals only where the span exceeds 53 bits, counted): FAIL {cnt('off','FAIL')}, REFUSED {len(ref)} "
  f"(of which span54 with the 53-bit reason: {len(okref)}; other: {len(ref)-len(okref)}), span54 rows refused {sum(1 for k in span54 if o[k][3]=='REFUSED')}/{len(span54)}, b53 rows PASS {sum(1 for k in b53 if o[k][3]=='PASS')}/{len(b53)}")
for k in sorted(set(ref) - okref): P(f"  UNEXPECTED REFUSAL {k[0]}.{k[1]}: {ref[k][6][:160]}")
# bar 3
t = data["thin"]; npairs_t = len({c for (c, _x) in t})
ok[3] = len(t) > 0 and cnt("thin", "FAIL") == 0 and npairs_t >= 100
P(f"\nbar 3 (thin family, {npairs_t} pairs / {len(t)} rows: 0 FAIL): FAIL {cnt('thin','FAIL')}, REFUSED {cnt('thin','REFUSED')}, volume exact {sum(1 for v in t.values() if vol(v)=='exact')}, close {sum(1 for v in t.values() if vol(v)=='close')}")
# bar 4
ok[4] = all(data[n] and cnt(n, "FAIL") == 0 and cnt(n, "REFUSED") == 0 for n in ("set3", "set5"))
P(f"\nbar 4 (touch set 3 and set 5 through the whole pipeline: 0 FAIL, 0 REFUSED): set3 {len(data['set3'])} rows FAIL {cnt('set3','FAIL')} REFUSED {cnt('set3','REFUSED')}; set5 {len(data['set5'])} rows FAIL {cnt('set5','FAIL')} REFUSED {cnt('set5','REFUSED')}")
# fidelity line statistics
def parse(line):
    d = dict(re.findall(r"(\w+)=([^ ]+)", line)); return d
allrows = [(n, k, v) for n in FAM for k, v in data[n].items() if v[3] == "PASS" and len(v) > 7 and v[7]]
maxulp = (0.0, ""); rounded = 0; tot = 0
ratios = {n: [] for n in FAM}; ovh = []; bms = []
for n, k, v in allrows:
    d = parse(v[7]); u = float(d["translate_back_max_err_ulps"])
    if u > maxulp[0]: maxulp = (u, f"{n}:{k[0]}.{k[1]}")
    a, b = d["rounded_coords"].split("/"); rounded += int(a); tot += int(b)
    o_ms = float(d["validate_ms"]) + float(d["recentre_ms"]) + float(d["back_ms"]); b_ms = float(d["boolean_ms"])
    if v[2] == "valid" and b_ms > 0: ratios[n].append(o_ms / b_ms); ovh.append(o_ms); bms.append(b_ms)
allr = [x for n in FAM for x in ratios[n]]
med = statistics.median(allr) if allr else float("nan")
ok[5] = bool(allr) and med <= 0.10
P(f"\nbar 5 (overhead of validate + re-centre + translate back <= 10% of the boolean's time, median over valid PASS rows): median of per-row ratios {100*med:.2f}% (ratio of medians {100*statistics.median(ovh)/statistics.median(bms):.2f}%) over {len(allr)} rows; "
  + "per family median ratio " + ", ".join(f"{n} {100*statistics.median(ratios[n]):.2f}%" for n in FAM if ratios[n]) + " (in-process stage timers; scoring ran in parallel)")
P(f"\ntranslate-back error: maximum {maxulp[0]:.6g} ulps of the original-frame value (row {maxulp[1] or '-'}); {rounded} of {tot} result coordinates rounded by the translate-back addition over {len(allrows)} PASS rows (the representation limit; first-rounding by Geogram in the translated frame is not included)")
# refusal reasons
rs = Counter()
for n in FAM:
    for k, v in data[n].items():
        if v[3] == "REFUSED": rs[(n, re.sub(r"\(axis.*", "", re.sub(r"^.*?REFUSED: ", "", v[6]))[:110])] += 1
P("\nrefusals by reason: " + ("; ".join(f"{n}: {r} x{c}" for (n, r), c in sorted(rs.items())) or "none"))
# bar 6 hashes
def out_sha(n, cid, op):
    for d in glob.glob(os.path.join(S, f"{n}_*")):
        f = os.path.join(d, f"{cid}.{op}.msh")
        if os.path.exists(f): return hashlib.sha256(open(f, "rb").read()).hexdigest()
    return "-"
lines = [f"{out_sha(n, c, op)}  {n}:{c}.{op}  {v[3]}" for n in FAM for (c, op), v in sorted(data[n].items())]
verd = [f"{n}:{c}.{op}  {v[3]}" for n in FAM for (c, op), v in sorted(data[n].items())]
open(os.path.join(R, "mp24_output_sha256.txt"), "w").write("\n".join(lines) + "\n")
open(os.path.join(R, "mp24_verdicts.txt"), "w").write("\n".join(verd) + "\n")
agg = hashlib.sha256(("\n".join(lines) + "\n").encode()).hexdigest(); aggv = hashlib.sha256(("\n".join(verd) + "\n").encode()).hexdigest()
P(f"\nbar 6 (platforms): MP24 outputs: {len(lines)} rows; aggregate sha256 of the sorted '<sha256>  <family:case.op>  <verdict>' list = {agg}; aggregate of the verdict list alone = {aggv}. "
  "Identical verdicts and identical bytes between x86_64 and aarch64 are decided by comparing these two lines across the two artifacts (M1 vs Linux bytes may differ).")
okall = all(ok[i] for i in range(1, 6))
nm = [i for i in range(1, 6) if not ok[i]]
P(f"\nMP24: bars 1-5 on this platform: {'MET' if okall else 'NOT MET'} (bar 1 {'MET' if ok[1] else 'NOT MET'}, bar 2 {'MET' if ok[2] else 'NOT MET'}, bar 3 {'MET' if ok[3] else 'NOT MET'}, bar 4 {'MET' if ok[4] else 'NOT MET'}, bar 5 {'MET' if ok[5] else 'NOT MET'}); "
  f"max translate-back error {maxulp[0]:.6g} ulps; aggregate output hash {agg[:16]}")
if not okall: print(f"::warning title=MP24 NOT MET on {arch}::bars not met: {nm}; see the report")
open(os.path.join(R, "report.md"), "w").write("\n".join(out) + "\n")
if os.environ.get("GITHUB_STEP_SUMMARY"):
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f: f.write("\n".join(out) + "\n")
