#!/usr/bin/env python3
"""MP21b overhead: median over valid rows of (validated - direct)/direct, end-to-end wall time, GEOGRAM_RUNNER_THREADS=1, alternating, median of reps.
usage: timing.py <direct_runner> <validated_wrapper> <corpus_dir> [reps=9]. Valid rows = manifest cases whose tags lack expect=. Run alone."""
import csv, os, statistics, subprocess, sys, time
d, v, corpus = sys.argv[1:4]; reps = int(sys.argv[4]) if len(sys.argv) > 4 else 9
env = dict(os.environ, GEOGRAM_RUNNER_THREADS="1"); out = os.path.join(os.environ.get("TMPDIR", "/tmp"), "mp21b_t.msh")
rows = [r[0] for r in list(csv.reader(open(corpus + "/manifest.tsv"), delimiter="\t"))[1:] if "expect=" not in r[2]]
def run(exe, op, a, b):
    t = time.monotonic(); q = subprocess.run([exe, "boolean", op, a, b, out], capture_output=True, env=env); return time.monotonic() - t, q.returncode
rat = []; sd = sv = 0.0
for cid in rows:
    a, b = f"{corpus}/adversarial/{cid}_A.msh", f"{corpus}/adversarial/{cid}_B.msh"
    for op in ("union", "intersection", "difference"):
        td, tv = [], []
        for _ in range(reps):
            x = run(d, op, a, b); y = run(v, op, a, b)
            if x[1] != 0 or y[1] != 0: break
            td.append(x[0]); tv.append(y[0])
        if len(td) < reps: continue
        md, mv = statistics.median(td), statistics.median(tv); rat.append((mv - md) / md); sd += md; sv += mv
print(f"rows {len(rat)} median overhead {statistics.median(rat)*100:+.2f}%  mean {statistics.mean(rat)*100:+.2f}%  summed {(sv-sd)/sd*100:+.2f}%  max {max(rat)*100:+.1f}%  min {min(rat)*100:+.1f}%")
