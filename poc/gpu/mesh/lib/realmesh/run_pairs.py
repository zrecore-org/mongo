#!/usr/bin/env python3
"""MP26 driver: for every pair in corpus/real/pairs.tsv run pipeline v2, plain patched Geogram and Manifold (serial) on union, intersection, difference, one thread,
10-minute timeout each, recording wall time and peak RSS (wait4 rusage of that child), exit code and stderr tail; then score the pair with real_score.
usage: run_pairs.py <pairs.tsv> <outdir> [--jobs 6] [--npts 5000] [--only id,id]
Results of PASSing runs are deleted after scoring (failing ones are kept for examination). Resumable: a pair with outdir/scores/<id>.txt is skipped."""
import json, os, signal, subprocess, sys, time, resource
from multiprocessing import Pool

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../../.."))
S = os.path.join(ROOT, "storage/mesh")
TOOLS = {
    "v2": [os.path.join(S, "mp24b/bin/geogram_fidelity2")],
    "geo": [os.path.join(S, "mp24b/work/geo_chain/bin/geogram_runner")],
    "man": [os.path.join(S, "manifold/runA/manifold_runner")],
}
SCORE = os.path.join(S, "mp26/bin/real_score")
OPS = ["union", "intersection", "difference"]
TIMEOUT = 600

def run_one(cmd, env):
    t0 = time.monotonic()
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env, start_new_session=True)
    # wait4 on the child for its own rusage; stderr drained by a thread-free poll: use communicate-like via temp file instead
    return p, t0

def timed(cmd, env, errpath):
    t0 = time.monotonic()
    with open(errpath, "wb") as ef:
        p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=ef, env=env, start_new_session=True)
        rc = None; ru = None; timeout = False
        while True:
            pid, st, ru = os.wait4(p.pid, os.WNOHANG)
            if pid:
                rc = os.waitstatus_to_exitcode(st); break
            if time.monotonic() - t0 > TIMEOUT:
                os.killpg(p.pid, signal.SIGKILL); pid, st, ru = os.wait4(p.pid, 0); rc = "TIMEOUT"; timeout = True; break
            time.sleep(0.02)
        p.returncode = 0
    wall = time.monotonic() - t0
    return rc, wall, (ru.ru_maxrss / 1048576.0 if ru else 0.0)   # macOS: bytes -> MiB

def do_pair(args):
    pid, A, B, outdir, npts = args
    sc = os.path.join(outdir, "scores", pid + ".txt")
    if os.path.exists(sc): return pid
    env = dict(os.environ, GEOGRAM_RUNNER_THREADS="1", OMP_NUM_THREADS="1", VECLIB_MAXIMUM_THREADS="1")
    rd = os.path.join(outdir, "res"); os.makedirs(rd, exist_ok=True); os.makedirs(os.path.join(outdir, "err"), exist_ok=True)
    recs = []; results = []
    for tool, base in TOOLS.items():
        for op in OPS:
            out = os.path.join(rd, f"{pid}.{tool}.{op}.msh"); err = os.path.join(outdir, "err", f"{pid}.{tool}.{op}.txt")
            if os.path.exists(out): os.remove(out)
            rc, wall, rss = timed(base + ["boolean", op, A, B, out], env, err)
            txt = open(err, "rb").read().decode(errors="replace")
            lines = [l for l in txt.splitlines() if l.startswith(("REFUSED", "INTERNAL", "ERROR", "fidelity2:"))]
            note = " | ".join(lines)[:700]
            rec = dict(pair=pid, tool=tool, op=op, rc=rc, wall_s=round(wall, 3), rss_mib=round(rss, 1), note=note, out_exists=os.path.exists(out))
            if rc == 0 and os.path.exists(out): results.append(f"{tool}:{op}:{out}")
            recs.append(rec)
            if rc == 0 and not rec["note"] and os.path.exists(err): os.remove(err)   # keep stderr only when informative
    with open(os.path.join(outdir, "runs.jsonl"), "a") as f:
        for r in recs: f.write(json.dumps(r) + "\n")
    t0 = time.monotonic()
    p = subprocess.run([SCORE, "pair", A, B, str(npts), "20260514"] + results, capture_output=True, timeout=7200)
    txt = p.stdout.decode(errors="replace") + "\n#rc=%s %.1fs\n" % (p.returncode, time.monotonic() - t0) + p.stderr.decode(errors="replace")
    os.makedirs(os.path.join(outdir, "scores"), exist_ok=True)
    # delete the result files of PASS lines
    keep = set()
    for line in txt.splitlines():
        f = line.split("\t")
        if f[0] == "R" and f[3] != "PASS": keep.add(f"{f[1]}.{f[2]}")
    for r in results:
        tool, op, path = r.split(":", 2)
        if f"{tool}.{op}" not in keep and os.path.exists(path): os.remove(path)
    open(sc, "w").write(txt)
    return pid

if __name__ == "__main__":
    a = sys.argv[1:]
    pairs_tsv, outdir = a[0], a[1]
    jobs = int(a[a.index("--jobs") + 1]) if "--jobs" in a else 6
    npts = int(a[a.index("--npts") + 1]) if "--npts" in a else 5000
    only = set(a[a.index("--only") + 1].split(",")) if "--only" in a else None
    os.makedirs(outdir, exist_ok=True)
    rows = [l.rstrip("\n").split("\t") for l in open(pairs_tsv)][1:]
    work = [(r[0], r[5], r[6], outdir, npts) for r in rows if not only or r[0] in only]
    # largest first would balance the tail, but keep file order for reproducibility of the log
    with Pool(jobs) as pool:
        for i, pid in enumerate(pool.imap_unordered(do_pair, work), 1):
            print(f"[{i}/{len(work)}] {pid}", flush=True)
