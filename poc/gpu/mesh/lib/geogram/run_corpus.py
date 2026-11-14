#!/usr/bin/env python3
"""MP04: run geogram_runner on every adversarial pair x 3 ops, twice each. Writes results.tsv.
usage: run_corpus.py <runner> <corpus_dir> <out_dir>"""
import csv, hashlib, math, os, struct, subprocess, sys, time

runner, corpus, outdir = sys.argv[1:4]
adv = os.path.join(corpus, "adversarial")
os.makedirs(outdir, exist_ok=True)
ids = sorted({f[:-6] for f in os.listdir(adv) if f.endswith("_A.msh")})

def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()

def signed_vol(p):
    d = open(p, "rb").read()
    _, nv, nt = struct.unpack_from("<3I", d, 0)
    v = struct.unpack_from("<%dd" % (3 * nv), d, 12)
    t = struct.unpack_from("<%dI" % (3 * nt), d, 12 + 24 * nv)
    terms = []
    for i in range(nt):
        a, b, c = (v[3*t[3*i+k]:3*t[3*i+k]+3] for k in range(3))
        terms.append((a[0]*(b[1]*c[2]-b[2]*c[1]) - a[1]*(b[0]*c[2]-b[2]*c[0]) + a[2]*(b[0]*c[1]-b[1]*c[0])) / 6.0)
    return nv, nt, math.fsum(terms)

rows = []
for cid in ids:
    A = os.path.join(adv, cid + "_A.msh"); B = os.path.join(adv, cid + "_B.msh")
    for op in ("union", "intersection", "difference"):
        res = []
        for rep in (1, 2):
            out = os.path.join(outdir, "%s.%s.r%d.msh" % (cid, op, rep))
            if os.path.exists(out): os.remove(out)
            t0 = time.monotonic()
            try:
                p = subprocess.run([runner, "boolean", op, A, B, out], capture_output=True, timeout=60)
                rc, err, so = p.returncode, p.stderr.decode(errors="replace"), p.stdout
            except subprocess.TimeoutExpired as e:
                rc, err, so = "TIMEOUT", (e.stderr or b"").decode(errors="replace"), b""
            wall = (time.monotonic() - t0) * 1000
            h = sha(out) if rc == 0 and os.path.exists(out) else ""
            nt = nv = ""; sv = ""
            if h:
                nv, nt, sv = signed_vol(out)
            conv = ""
            for line in err.splitlines():
                if line.startswith("convert_ms="): conv = line.split("=")[1]
            res.append(dict(rc=rc, wall=wall, nt=nt, nv=nv, sv=sv, sha=h, conv=conv, stdout=len(so),
                            err=" | ".join(l for l in err.splitlines() if l.startswith(("REFUSED", "INTERNAL", "note")))[:300],
                            errfull=err))
        r1, r2 = res
        rows.append(dict(case=cid, op=op, rc=r1["rc"], rc2=r2["rc"], wall_ms=round(r1["wall"], 1), wall2_ms=round(r2["wall"], 1),
                         convert_ms=r1["conv"], out_nv=r1["nv"], out_nt=r1["nt"], signed_vol=("%.17g" % r1["sv"]) if r1["sv"] != "" else "",
                         sha256=r1["sha"], identical=("yes" if r1["sha"] and r1["sha"] == r2["sha"] else ("n/a" if not r1["sha"] and not r2["sha"] and r1["rc"] == r2["rc"] else "NO")),
                         stdout_bytes=r1["stdout"], note=r1["err"]))
        print(cid, op, rows[-1]["rc"], rows[-1]["rc2"], rows[-1]["wall_ms"], rows[-1]["out_nt"], rows[-1]["identical"], flush=True)
        if r1["rc"] != 0:
            open(os.path.join(outdir, "%s.%s.stderr.txt" % (cid, op)), "w").write(r1["errfull"])
with open(os.path.join(outdir, "results.tsv"), "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys()), delimiter="\t")
    w.writeheader(); w.writerows(rows)
