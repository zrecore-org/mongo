#!/usr/bin/env python3 -I
"""MP26 aggregation: counts per tool and operation, timing/memory quantiles, residuals, lists of failures. usage: aggregate.py <rundir> <pairs.tsv>"""
import json, os, sys, collections, statistics
run, pairs = sys.argv[1:3]
P = {l.split("\t")[0]: l.rstrip("\n").split("\t") for l in list(open(pairs))[1:]}
runs = [json.loads(l) for l in open(os.path.join(run, "runs.jsonl"))]
score = {}; amb = {}; resid = {}
for pid in P:
    for line in open(os.path.join(run, "scores", pid + ".txt")):
        f = line.rstrip("\n").split("\t")
        if f[0] == "P": amb[pid] = int(f[1].split("=")[1])
        elif f[0] == "R": score[(pid, f[1], f[2])] = f
        elif f[0] == "S": resid[(pid, f[1])] = float(f[2])
def q(x, p):
    x = sorted(x); return x[min(len(x) - 1, int(p * len(x)))] if x else float("nan")
ambpairs = {p for p, a in amb.items() if a > 0}
print("pairs", len(P), "with ambiguous operands (winding outside {0,1} at some sample):", len(ambpairs))
for scope, flt in (("ALL PAIRS", lambda p: True), ("PAIRS WITHOUT AMBIGUOUS OPERANDS", lambda p: p not in ambpairs), ("PAIRS WITH AMBIGUOUS OPERANDS", lambda p: p in ambpairs)):
    print("\n==", scope)
    for tool in ("v2", "geo", "man"):
        for op in ("union", "intersection", "difference"):
            rs = [r for r in runs if r["tool"] == tool and r["op"] == op and flt(r["pair"])]
            cnt = collections.Counter()
            for r in rs:
                if r["rc"] == "TIMEOUT": cnt["timeout"] += 1
                elif r["rc"] == 3: cnt["refused"] += 1
                elif r["rc"] != 0: cnt["crash"] += 1
                else: cnt["correct" if score[(r["pair"], tool, op)][3] == "PASS" else "wrong"] += 1
            t = [r["wall_s"] for r in rs]; m = [r["rss_mib"] for r in rs]
            print(f"{tool:4}{op:13} n={len(rs):3} correct={cnt['correct']:3} wrong={cnt['wrong']:3} refused={cnt['refused']:3} crash={cnt['crash']} timeout={cnt['timeout']} | time s median={statistics.median(t):.3f} p99={q(t,.99):.2f} max={max(t):.2f} | peak RSS MiB median={statistics.median(m):.0f} max={max(m):.0f}")
print("\n== failures (pair kind idA idB tool op reasons)")
for (pid, tool, op), f in sorted(score.items()):
    if f[3] != "PASS": print(pid, P[pid][1], P[pid][2], P[pid][3], tool, op, f[-1], "amb=%d" % amb[pid], "nt=%s" % f[4])
print("\n== refusals")
for r in runs:
    if r["rc"] != 0: print(r["pair"], P[r["pair"]][1], P[r["pair"]][2], P[r["pair"]][3], r["tool"], r["op"], r["rc"], r["note"][:140])
print("\n== residual |V(AuB)+V(AnB)-V(A)-V(B)|/V(AuB): quantiles per tool (pairs without ambiguous operands, both ops produced)")
for tool in ("v2", "geo", "man"):
    x = [v for (pid, t), v in resid.items() if t == tool and pid not in ambpairs]
    print(tool, len(x), "median %.3g p90 %.3g p99 %.3g max %.3g" % (statistics.median(x), q(x, .9), q(x, .99), max(x)), " > 1e-12:", sum(1 for v in x if v > 1e-12), " > 1e-6:", sum(1 for v in x if v > 1e-6))
print("largest residuals:")
for (pid, t), v in sorted(resid.items(), key=lambda kv: -kv[1])[:15]: print(" ", pid, t, "%.3g" % v, "amb=%d" % amb[pid], P[pid][1], P[pid][2], P[pid][3])
nm = [(k, f) for k, f in score.items() if f[7] == "1" and f[3] == "PASS"]
print("\nPASS results with a non-manifold edge (geometric only):", collections.Counter(k[1] for k, f in nm))
print("zero-area triangles in PASS results:", collections.Counter(k[1] for k, f in score.items() if f[10] == "1" and f[3] == "PASS"))
vac = collections.Counter(k[1] for k, f in score.items() if f[3] == "PASS" and int(f[15]) == 0)
print("PASS results with zero informative (inside-expected) samples:", vac)
print("samples per result min:", min(int(f[13]) for f in score.values()))
