#!/usr/bin/env python3
"""Runs prototype (baseline m0 or a mutant) over corpus_hard/cases, scores each with the oracle, writes a TSV and prints counts per mode/family.
usage: drive.py <proto binary suffix, e.g. m0> [mode ...]   (<= 4 parallel processes)"""
import subprocess, sys, os, glob, collections
from concurrent.futures import ThreadPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__)); ST = "/Volumes/CrucialX9/Projects/mongo/storage/mesh/hard2d3d"
cases = sorted(glob.glob(HERE + "/../../corpus_hard/cases/*.h2d")); tag = os.path.basename(sys.argv[1]).replace("proto_", ""); modes = sys.argv[2:]
proto = f"{ST}/bin/proto_{tag}"
od = f"{ST}/out/{tag}"; os.makedirs(od, exist_ok=True)
def run(c):
    nm = os.path.basename(c)[:-4]; mode = nm.split("_")[1]; fam = nm.split("_")[2]
    if modes and mode not in modes: return None
    r = f"{od}/{nm}.res"; subprocess.run([proto, c, r], check=False, timeout=120)
    try: o = subprocess.run([f"{ST}/bin/oracle", c, r], capture_output=True, text=True, timeout=900).stdout.strip()
    except subprocess.TimeoutExpired: o = "WRONG oracle timeout"
    return (nm, mode, fam, o)
with ThreadPoolExecutor(4) as ex: rows = [x for x in ex.map(run, cases) if x]
with open(f"{ST}/out/{tag}.tsv", "w") as f:
    for r in rows: f.write("\t".join(r) + "\n")
cnt = collections.defaultdict(collections.Counter)
for nm, mode, fam, o in rows: cnt[mode][o.split()[0]] += 1
for mode in sorted(cnt): print(tag, mode, dict(cnt[mode]), "total", sum(cnt[mode].values()))
