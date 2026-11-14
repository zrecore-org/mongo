#!/usr/bin/env python3
"""MP08a driver. usage: drive.py corpus|speed [workers]. Runs every (library, op) on every case in a subprocess (crash/timeout isolation), then the exact checker.
Writes results/{corpus,speed}.tsv under storage/mesh/gap2d/ and copies are kept in lib/gap2d/results/."""
import os, sys, subprocess, glob, time, signal, statistics, json
from concurrent.futures import ThreadPoolExecutor
ST = "/Volumes/CrucialX9/Projects/mongo/storage/mesh/gap2d"; BIN = ST + "/bin"
HERE = os.path.dirname(os.path.abspath(__file__)); CASES = HERE + "/../../corpus2d/cases"
OUT = ST + "/out"; os.makedirs(OUT, exist_ok=True)
BOOL = ["union", "intersection", "difference"]
LIBS_OPS = {  # library -> ops it is asked to run
    "clipper2": BOOL + ["offset", "area", "intersects", "triangulate"],
    "boost": BOOL + ["offset", "hull", "area", "intersects", "isvalid"],
    "manifold": BOOL + ["offset", "hull", "area", "triangulate"],
    "earcut": ["triangulate"], "cdt": ["triangulate"]}
def run(cmd, timeout):
    t = time.monotonic()
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout, text=True)
        return p.returncode, p.stdout, p.stderr, time.monotonic() - t
    except subprocess.TimeoutExpired:
        return "TIMEOUT", "", "", time.monotonic() - t
def parse(line):
    d = {}
    for kv in line.strip().split("\t"):
        if "=" in kv: k, v = kv.split("=", 1); d[k] = v
    return d
def medns(resfile):
    ns = [float(l.split()[1]) for l in open(resfile) if l.startswith("T ")]
    return statistics.median(ns) if ns else -1
def one(job):
    lib, op, case = job; name = os.path.basename(case)[:-4]; fam = name.split("_")[1] if False else None
    res = f"{OUT}/{lib}.{op}.{name}.res"
    if os.path.exists(res): os.remove(res)
    rc, so, se, wall = run([f"{BIN}/run_{lib}", op, case, res, "1"], 120)
    row = {"case": name, "lib": lib, "op": op}
    if rc == "TIMEOUT": row.update(status="TIMEOUT", verdict="CRASH", msg="timeout 120 s"); return row
    if rc != 0 or not os.path.exists(res):
        row.update(status="CRASH", verdict="CRASH", msg=f"exit {rc} {se.strip()[-120:]}"); return row
    head = open(res).readline().split(None, 2)
    row["status"] = head[1]; row["ms"] = f"{medns(res)/1e6:.3f}"
    if head[1] != "OK": row.update(verdict="REFUSED", msg=(head[2].strip() if len(head) > 2 else "")); return row
    rc2, so2, se2, w2 = run([f"{BIN}/check2d", "eval", case, op, res], 900)
    if rc2 != 0:
        row.update(verdict="CHECKER_ERROR", msg=f"checker rc={rc2} {se2.strip()[-100:]}"); return row
    d = parse(so2); row.update(d)
    row["verdict"] = judge(lib, op, d)
    os.remove(res) if row["verdict"] == "CORRECT" else None
    return row
def judge(lib, op, d):
    if d.get("verdict") in ("REFUSED", "WRONG_INVALID", "NORESULT"): return d["verdict"]
    if op in BOOL or op == "offset":
        key = {"clipper2": "ok_grid1"}.get(lib, "ok_rel"); return "CORRECT" if d.get(key) == "1" else "WRONG"
    if op == "hull": return "CORRECT" if d.get("hull_same") == "1" else "WRONG"
    if op == "area": return "CORRECT" if d.get("ok_rel") == "1" else "WRONG"
    if op in ("intersects", "isvalid"): return "CORRECT" if d.get("ok") == "1" else "WRONG"
    if op == "triangulate": return "CORRECT" if d.get("covers") == "1" and d.get("no_overlap") == "1" else "WRONG"
    return "?"
def jobs():
    out = []
    for f in sorted(glob.glob(CASES + "/*.p2d")):
        fam = open(f).read().split("family ")[1].split()[0]
        if fam == "speed": continue
        for lib, ops in LIBS_OPS.items():
            for op in ops:
                if fam == "offset" and op in BOOL + ["intersects"]: continue
                if fam != "offset" and op == "offset": continue
                if fam != "invalid" or True:
                    if fam == "invalid" and op in ("offset",): continue
                out.append((lib, op, f))
    return out
def speed():
    rows = []
    for name in ["spd1k", "spd100k"]:
        f = (CASES if name == "spd1k" else ST + "/speed") + f"/{name}.p2d"
        for lib, ops in LIBS_OPS.items():
            for op in ops:
                if op in ("offset", "intersects") : continue
                res = f"{OUT}/speed.{lib}.{op}.{name}.res"
                if os.path.exists(res): os.remove(res)
                rc, so, se, wall = run([f"{BIN}/run_{lib}", op, f, res, "5"], 600)
                r = {"case": name, "lib": lib, "op": op}
                if rc == "TIMEOUT": r.update(status="TIMEOUT", msg="timeout 600 s for 5 reps")
                elif rc != 0 or not os.path.exists(res): r.update(status="CRASH", msg=f"exit {rc} {se.strip()[-100:]}")
                else:
                    head = open(res).readline().split(None, 2); r["status"] = head[1]
                    if head[1] == "OK":
                        ns = sorted(float(l.split()[1]) for l in open(res) if l.startswith("T "))
                        r["median_ms"] = f"{statistics.median(ns)/1e6:.2f}"; r["min_ms"] = f"{ns[0]/1e6:.2f}"; r["max_ms"] = f"{ns[-1]/1e6:.2f}"
                        r["nrings"] = str(sum(1 for l in open(res) if l.startswith("ring"))); r["nverts"] = str(sum(int(l.split()[1]) for l in open(res) if l.startswith("ring")))
                    else: r["msg"] = head[2].strip() if len(head) > 2 else ""
                rows.append(r); print(r, flush=True)
    return rows
def main():
    mode = sys.argv[1]; workers = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    if mode == "speed":
        rows = speed(); keys = ["case", "lib", "op", "status", "median_ms", "min_ms", "max_ms", "nrings", "nverts", "msg"]
        tsv = ST + "/speed.tsv"
    else:
        js = jobs(); print(len(js), "jobs", flush=True)
        with ThreadPoolExecutor(workers) as ex: rows = list(ex.map(one, js))
        keys = ["case", "lib", "op", "status", "verdict", "ms", "valid", "orientOK", "nrings", "area_res", "area_oracle", "diff", "perim", "ok_rel", "ok_grid1", "ok_grid8", "exact", "symdiff", "symdiff_zero", "sd_ok_rel", "sd_ok_grid1", "sd_ok_grid8",
                "hull_same", "ntri", "degenerate", "steiner", "covers", "no_overlap", "delaunay_edges", "delaunay_violations", "want", "got", "ok", "msg"]
        tsv = ST + "/corpus.tsv"
    with open(tsv, "w") as f:
        f.write("\t".join(keys) + "\n")
        for r in rows: f.write("\t".join(str(r.get(k, "")) for k in keys) + "\n")
    print("wrote", tsv, len(rows), "rows")
main()
