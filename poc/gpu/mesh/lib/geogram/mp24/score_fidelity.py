#!/usr/bin/env python3
"""MP24 copy of oracle/score_library.py (the original is unchanged). Differences: (1) ROOT depth; (2) an extra last column `fidelity` with the runner's stderr line starting "fidelity:" (re-centring, translate-back error, per-stage ms);
The scoring itself (mesh_compare, exact, manifest expectations in the ORIGINAL frame) is untouched: a translated pair is scored with
the original-frame operands and the original-frame result, which is what the pipeline returns.
Runs a boolean-engine runner over the corpus and scores every output with mesh_compare (the pre-registered bars in mesh/PLAN.md).
Usage: score_library.py <name> <runner> <corpusdir> [--points N] [--outdir DIR] [--cases id,id]
The runner contract: `<runner> boolean <union|intersection|difference> A.msh B.msh out.msh`; exit 0 ok, 3 refused, 4 error. A 60 s timeout is a TIMEOUT.
Writes <outdir>/results.tsv and prints a summary. Big outputs go to the outdir (default: storage/mesh/<name>/score on the external volume)."""
import csv, hashlib, os, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../../../../../.."))   # copy of oracle/score_library.py (MP24), one directory deeper
CMP = os.path.join(ROOT, "poc/gpu/build/mesh/mesh_compare")
OPS = ["union", "intersection", "difference"]
MALFORMED = ("open", "non-manifold-edge", "self-intersecting", "unwelded-vertices", "zero-area-triangle", "inverted", "duplicate-triangle")

def main():
    a = sys.argv[1:]
    name, runner, corpus = a[0], a[1], a[2]
    points = int(a[a.index("--points") + 1]) if "--points" in a else 5000
    outdir = a[a.index("--outdir") + 1] if "--outdir" in a else os.path.join(ROOT, "storage/mesh", name, "score")
    only = set(a[a.index("--cases") + 1].split(",")) if "--cases" in a else None
    os.makedirs(outdir, exist_ok=True)
    rows = list(csv.reader(open(os.path.join(corpus, "manifest.tsv")), delimiter="\t"))[1:]
    res = []
    for r in rows:
        cid, tags = r[0], r[2]
        if only and cid not in only: continue
        malformed = any(t in tags.split(",") or t.startswith("expect=") for t in MALFORMED) or "expect=" in tags
        for op in OPS:
            out = os.path.join(outdir, f"{cid}.{op}.msh")
            if os.path.exists(out): os.remove(out)
            t0 = time.monotonic()
            try:
                p = subprocess.run([runner, "boolean", op, f"{corpus}/adversarial/{cid}_A.msh", f"{corpus}/adversarial/{cid}_B.msh", out], capture_output=True, text=True, timeout=60)
                code, err = p.returncode, p.stderr.strip().splitlines()
            except subprocess.TimeoutExpired:
                code, err = "timeout", []
            dt = time.monotonic() - t0
            sha = hashlib.sha256(open(out, "rb").read()).hexdigest()[:16] if os.path.exists(out) else "-"
            if code == 0:
                q = subprocess.run([CMP, "score", corpus, cid, op, out, str(points)], capture_output=True, text=True)
                detail = q.stdout.strip().splitlines()[-1] if q.stdout.strip() else q.stderr.strip()
                verdict = "PASS" if " PASS " in detail else "ACCEPTED-UNVERIFIED" if "ACCEPTED-UNVERIFIED" in detail else "FAIL"
            elif code == 3: verdict, detail = "REFUSED", next((l for l in err if l.startswith("REFUSED")), "")
            elif code == "timeout": verdict, detail = "TIMEOUT", ""
            else: verdict, detail = ("CRASH" if isinstance(code, int) and code < 0 else "ERROR"), " ".join(err[-1:])
            fid = next((l for l in err if l.startswith("fidelity:")), "") if code == 0 else ""
            res.append((cid, op, "malformed" if malformed else "valid", verdict, f"{dt:.3f}", sha, detail, fid))
            print(f"{cid:28s} {op:12s} {'malformed' if malformed else 'valid':9s} {verdict:20s} {dt:6.3f}s  {detail[:150]}", flush=True)
    with open(os.path.join(outdir, "results.tsv"), "w") as f:
        w = csv.writer(f, delimiter="\t"); w.writerow(["case", "op", "kind", "verdict", "seconds", "sha256_16", "detail", "fidelity"]); w.writerows(res)
    from collections import Counter
    for kind in ("valid", "malformed"):
        c = Counter(v for (_, _, k, v, *_r) in res if k == kind); print(f"summary {name} {kind}: {dict(c)}")

main()
