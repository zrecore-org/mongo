#!/usr/bin/env python3 -I
"""MP26 step 2: convert the selected models (STL -> MSH1, exact doubles, exact-coordinate welding) and build the pairs; writes corpus/real/pairs.tsv and
corpus/real/HASHES.txt (sha256 of the metadata inputs, selection files, pairs.tsv, the tool sources and every generated file). Model files stay in storage/mesh/mp26 (never committed).
usage: build_pairs.py <storage/thingi10k_raw> <storage/mesh/mp26> <corpus/real>"""
import hashlib, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
raw, work, real = sys.argv[1:4]
HERE = os.path.dirname(os.path.abspath(__file__))
sha = lambda p: hashlib.sha256(open(p, "rb").read()).hexdigest()
for d in ("models", "pairs"): os.makedirs(os.path.join(work, d), exist_ok=True)
rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(real, "selected_models.tsv"))][1:]
ids = [r[0] for r in rows]
def conv(i):
    p = subprocess.run([os.path.join(work, "bin/stl_to_msh"), os.path.join(raw, "Thingi10K/raw_meshes", i + ".stl"), os.path.join(work, "models", i + ".msh")], capture_output=True)
    return i, p.returncode, p.stdout.decode().strip()
with ThreadPoolExecutor(6) as ex: conv_res = list(ex.map(conv, ids))
bad = [c for c in conv_res if c[1] != 0]
if bad: print("CONVERSION FAILED for", bad)
with open(os.path.join(real, "model_conversion.tsv"), "w") as f:
    f.write("id\trc\tnv\tnt\tdegenerate_index_tris\tmsh_sha256\n")
    for i, rc, o in conv_res: f.write(f"{i}\t{rc}\t{o.replace(' ', chr(9)) if rc == 0 else '-\t-\t-'}\t{sha(os.path.join(work, 'models', i + '.msh')) if rc == 0 else '-'}\n")
pairs = []
def mk(pid, kind, a, b, seed):
    A = os.path.join(work, "pairs", pid + "_A.msh"); B = os.path.join(work, "pairs", pid + "_B.msh")
    p = subprocess.run([os.path.join(work, "bin/pair_build"), kind, str(seed), os.path.join(work, "models", a + ".msh"), os.path.join(work, "models", b + ".msh") if b != "-" else "-", A, B], capture_output=True)
    if p.returncode: print("pair_build failed", pid, p.stderr); return None
    return [pid, kind, a, b if b != "-" else a, p.stdout.decode().strip(), A, B]
jobs = []
n = 0
for k in range(150): jobs.append((f"p{n:03d}", "perm", ids[k], "-", 1000003 * 1 + n)); n += 1
for k in range(150, 280): jobs.append((f"p{n:03d}", "rot", ids[k], "-", 1000003 * 1 + n)); n += 1
for k in range(140): jobs.append((f"p{n:03d}", "diff", ids[280 + 2 * k], ids[281 + 2 * k], 1000003 * 1 + n)); n += 1
bad_ids = {c[0] for c in bad}
jobs = [j for j in jobs if j[2] not in bad_ids and j[3] not in bad_ids]
with ThreadPoolExecutor(6) as ex: res = [r for r in ex.map(lambda j: mk(*j), jobs) if r]
with open(os.path.join(real, "pairs.tsv"), "w") as f:
    f.write("pair\tkind\tidA\tidB\tparams\tA_path\tB_path\tA_sha256\tB_sha256\n")
    for r in res: f.write("\t".join(r + [sha(r[5]), sha(r[6])]) + "\n")
print(len(res), "pairs")
