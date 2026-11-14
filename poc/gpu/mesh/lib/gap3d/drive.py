#!/usr/bin/env python3
# Runs every case through every route of its kind (one process per case, 60 s timeout, at most 3 workers) -> storage/mesh/gap3d/out/results.tsv. A crash or timeout becomes a CRASH row.
import os, subprocess, sys, glob
from concurrent.futures import ThreadPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__)); ST = "/Volumes/CrucialX9/Projects/mongo/storage/mesh/gap3d"
BIN = ST + "/bin/run3d"; CASES = os.path.join(HERE, "../../corpus3d/cases")
ROUTES = {"extrude": ["manifold", "own", "own_cdt"], "revolve": ["manifold"] + (["neg_seg", "neg_nudge"] if __import__("os").environ.get("NEGCTL") else []), "tess": ["earcut", "cdt"], "mink": ["msum", "msumT", "hull"], "mink_nc": ["msum", "msumT"], "dist_pm": ["own"], "dist_mm": ["mingap", "own"], "measure": ["manifold", "own"]}
def one(job):
    route, path = job; name = os.path.basename(path)[:-4]; kind = name.rsplit("_", 1)[0]
    try:
        p = subprocess.run([BIN, route, path], capture_output=True, text=True, timeout=60)
        if p.returncode != 0 or not p.stdout.strip():
            return [f"{name}\t{kind}\t?\t{route}\tsolid\tCRASH\t0\t0\texit {p.returncode} {p.stderr.strip()[:100]}"]
        return p.stdout.strip().split("\n")
    except subprocess.TimeoutExpired:
        return [f"{name}\t{kind}\t?\t{route}\tsolid\tCRASH\t0\t0\ttimeout"]
only = sys.argv[1] if len(sys.argv) > 1 else None
jobs = []
for f in sorted(glob.glob(CASES + "/*.c3d")):
    kind = os.path.basename(f)[:-4].rsplit("_", 1)[0]
    if only and kind != only: continue
    for r in ROUTES[kind]: jobs.append((r, f))
out = ST + "/out/results" + ("_negctl" if __import__("os").environ.get("NEGCTL") else "") + ("_" + only if only else "") + ".tsv"
with ThreadPoolExecutor(3) as ex, open(out, "w") as o:
    o.write("name\tkind\tcls\troute\tsub\tverdict\tulps\tns\tmsg\n")
    for rows in ex.map(one, jobs):
        for r in rows: o.write(r + "\n")
print("wrote", out, len(jobs), "jobs")
