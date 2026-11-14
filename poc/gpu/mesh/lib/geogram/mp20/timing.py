#!/usr/bin/env python3
"""MP20 bar: median time change of the patched runner on non-touching pairs. usage: timing.py <unpatched_runner> <patched_runner> <corpus_dir> [reps=9] [exclude_case_regex]
Non-touching is fixed up front (before any run): every (case, op) row of the corpus except cases whose name matches the exclude regex (default 'touch', i.e. rows in a contact class).
For each row the end-to-end wall time (process start to exit, inputs read, output written) of both runners is measured `reps` times, alternating (unpatched first),
GEOGRAM_RUNNER_THREADS=1, and the median of each is taken. Prints the median over rows of (patched - unpatched)/unpatched and the ratio of summed medians. Run alone."""
import csv, os, re, statistics, subprocess, sys, time
u, p, corpus = sys.argv[1:4]; reps = int(sys.argv[4]) if len(sys.argv) > 4 else 9
excl = re.compile(sys.argv[5] if len(sys.argv) > 5 else "touch")
env = dict(os.environ, GEOGRAM_RUNNER_THREADS="1"); tmp = os.environ.get("TMPDIR", "/tmp")
rows = [r[0] for r in list(csv.reader(open(corpus + "/manifest.tsv"), delimiter="\t"))[1:] if not excl.search(r[0])]
def run(exe, op, a, b, out):
    t = time.monotonic(); q = subprocess.run([exe, "boolean", op, a, b, out], capture_output=True, text=True, env=env); return time.monotonic() - t, q.returncode
ratios, su, sp, nrows = [], 0.0, 0.0, 0
for cid in rows:
    a, b = f"{corpus}/adversarial/{cid}_A.msh", f"{corpus}/adversarial/{cid}_B.msh"
    for op in ("union", "intersection", "difference"):
        out = os.path.join(tmp, "mp20_timing_out.msh")
        if run(u, op, a, b, out)[1] != 0 or run(p, op, a, b, out)[1] != 0: continue
        tu, tp = [], []
        for _ in range(reps): tu.append(run(u, op, a, b, out)[0]); tp.append(run(p, op, a, b, out)[0])
        mu, mp = statistics.median(tu), statistics.median(tp); ratios.append((mp - mu) / mu); su += mu; sp += mp; nrows += 1
print(f"rows measured: {nrows}  median change: {statistics.median(ratios) * 100:+.2f}%  mean change: {statistics.mean(ratios) * 100:+.2f}%  summed-time change: {(sp - su) / su * 100:+.2f}%  max row: {max(ratios) * 100:+.1f}%  min row: {min(ratios) * 100:+.1f}%")
