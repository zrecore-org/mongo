#!/usr/bin/env python3
"""MP18 bar: added time of the guard on non-touching pairs. usage: timing.py <runner> <guard> <corpus_dir> [reps=7]
A pair is "touching" if the guard reports triggered=1 on any op. For every other (pair, op) the end-to-end wall time (process start to exit,
inputs read and output written) of runner and guard are each measured `reps` times, alternating, and the median of each is taken.
Prints the median over rows of (guard - runner) / runner, and the median of absolute added ms."""
import csv, os, statistics, subprocess, sys, tempfile, time
runner, guard, corpus = sys.argv[1:4]; reps = int(sys.argv[4]) if len(sys.argv) > 4 else 7
env = dict(os.environ, GEOGRAM_RUNNER_THREADS="1")
rows = list(csv.reader(open(corpus + "/manifest.tsv"), delimiter="\t"))[1:]
tmp = os.environ.get("TMPDIR", "/tmp")
def run(exe, op, a, b, out):
    t = time.monotonic(); p = subprocess.run([exe, "boolean", op, a, b, out], capture_output=True, text=True, env=env); return time.monotonic() - t, p
ratios = []; added = []; skipped = []
for r in rows:
    a = f"{corpus}/adversarial/{r[0]}_A.msh"; b = f"{corpus}/adversarial/{r[0]}_B.msh"
    for op in ("union", "intersection", "difference"):
        out = os.path.join(tmp, "mp18_timing_out.msh")
        _, p = run(guard, op, a, b, out)
        if p.returncode != 0 or "triggered=1" in p.stderr: skipped.append((r[0], op, p.returncode)); continue
        tr, tg = [], []
        for _ in range(reps):
            tr.append(run(runner, op, a, b, out)[0]); tg.append(run(guard, op, a, b, out)[0])
        mr, mg = statistics.median(tr), statistics.median(tg)
        ratios.append((mg - mr) / mr); added.append((mg - mr) * 1000)
print(f"non-touching rows measured: {len(ratios)}  skipped (touching/refused): {len(skipped)}")
print(f"median added time: {statistics.median(ratios) * 100:.2f}%   median added ms: {statistics.median(added):.3f}   max added: {max(ratios) * 100:.1f}%")
