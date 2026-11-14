#!/usr/bin/env python3
"""usage: drive3d.py <m0|m1|m2>: runs geo3d_<m> over corpus_hard/cases3d, scores with oracle3d (4 parallel)."""
import subprocess, sys, os, glob, collections
from concurrent.futures import ThreadPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__)); ST = "/Volumes/CrucialX9/Projects/mongo/storage/mesh/hard2d3d"; m = sys.argv[1]
od = f"{ST}/out/geo3d_{m}"; os.makedirs(od, exist_ok=True)
def run(c):
    nm = os.path.basename(c)[:-4]; r = f"{od}/{nm}.res"; subprocess.run([f"{ST}/bin/geo3d_{m}", c, r], timeout=120)
    return nm, subprocess.run([f"{ST}/bin/oracle3d", c, r], capture_output=True, text=True, timeout=600).stdout.strip()
with ThreadPoolExecutor(4) as ex: rows = list(ex.map(run, sorted(glob.glob(HERE + "/../../corpus_hard/cases3d/*.h3d"))))
open(f"{ST}/out/geo3d_{m}.tsv", "w").write("".join(f"{a}\t{b}\n" for a, b in rows))
print(m, dict(collections.Counter(b.split()[0] for a, b in rows)))
