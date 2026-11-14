#!/usr/bin/env python3 -I
"""MP25 sweep driver. usage: sweep.py <tag> <runner> <magdir> <outdir> [--scale] [--selfcheck] [--jobs N] [--fam f1,f2]
Runs `<runner> boolean <op> A B out` (GEOGRAM_RUNNER_THREADS=1) for every pair in <magdir>/manifest.tsv and each of union/intersection/difference,
computes the EXACT signed volume of the output (Fractions over the binary64 coordinates, scaled back exactly when --scale) and compares it with the
exact expected volume from the manifest (volume = N/3 * 2^(3e)). With --scale both operands are first multiplied by 2^-s (s = smallest >= 0 with
max|coordinate| < 2^18, checked exact by round trip), the result is multiplied by 2^s before measuring.
--selfcheck only verifies that the input meshes have the manifest volumes (generator check; runs no Geogram).
Verdicts: OK (volume exactly equal), WRONG (a result with another volume; subtype EMPTY if no triangles), REFUSED (exit 3), ERROR (other)."""
import math, os, struct, subprocess, sys
from fractions import Fraction
from multiprocessing import Pool

def read_msh(p):
    b = open(p, "rb").read()
    magic, nv, nt = struct.unpack_from("<III", b, 0)
    assert magic == 0x3148534d
    v = struct.unpack_from("<%dd" % (3 * nv), b, 12)
    t = struct.unpack_from("<%dI" % (3 * nt), b, 12 + 24 * nv)
    return list(v), list(t)

def write_msh(p, v, t):
    with open(p, "wb") as f:
        f.write(struct.pack("<III", 0x3148534d, len(v) // 3, len(t) // 3)); f.write(struct.pack("<%dd" % len(v), *v)); f.write(struct.pack("<%dI" % len(t), *t))

def vol(v, t, s=0):
    """exact signed volume of the mesh whose coordinates are v * 2^s"""
    P = [Fraction(x) for x in v]
    tot = Fraction(0)
    for i in range(0, len(t), 3):
        a, b, c = (t[i] * 3, t[i + 1] * 3, t[i + 2] * 3)
        ax, ay, az, bx, by, bz, cx, cy, cz = P[a], P[a + 1], P[a + 2], P[b], P[b + 1], P[b + 2], P[c], P[c + 1], P[c + 2]
        tot += ax * (by * cz - bz * cy) - ay * (bx * cz - bz * cx) + az * (bx * cy - by * cx)
    return tot / 6 * Fraction(2) ** (3 * s)

def closed(t):
    from collections import Counter
    c = Counter()
    for i in range(0, len(t), 3):
        a, b, d = t[i], t[i + 1], t[i + 2]
        for u, w in ((a, b), (b, d), (d, a)): c[(u, w)] += 1
    return all(c[(w, u)] == n for (u, w), n in c.items())

def expect(N, e): return Fraction(int(N), 3) * Fraction(2) ** (3 * e)

def scale_exp(v):
    m = max(abs(x) for x in v)
    if m == 0: return 0
    _, ex = math.frexp(m)
    return max(0, ex - 18)

def scale(v, s):
    r = [math.ldexp(x, -s) for x in v]
    for x, y in zip(v, r):
        if math.ldexp(y, s) != x: return None   # not exact (underflow)
    return r

def work(a):
    (tag, runner, mag, out, do_scale, row) = a
    cid, fam, k, extra, e, fa, fb, NA, NB, NU, NI, ND = row
    va, ta = read_msh(f"{mag}/{fa}"); vb, tb = read_msh(f"{mag}/{fb}")
    res = []
    s = 0; A, B = f"{mag}/{fa}", f"{mag}/{fb}"
    if do_scale:
        s = max(scale_exp(va), scale_exp(vb)); sa, sb = scale(va, s), scale(vb, s)
        if sa is None or sb is None:
            return [(cid, fam, k, extra, op, s, "ERROR", "scaling not exact (underflow)", "") for op in ("union", "intersection", "difference")]
        A, B = f"{out}/{cid}.sA.msh", f"{out}/{cid}.sB.msh"; write_msh(A, sa, ta); write_msh(B, sb, tb)
    for op, N in (("union", NU), ("intersection", NI), ("difference", ND)):
        o = f"{out}/{cid}.{op}.msh"
        if os.path.exists(o): os.remove(o)
        env = dict(os.environ, GEOGRAM_RUNNER_THREADS="1")
        try:
            p = subprocess.run([runner, "boolean", op, A, B, o], capture_output=True, text=True, timeout=120, env=env); code = p.returncode
        except subprocess.TimeoutExpired:
            res.append((cid, fam, k, extra, op, s, "ERROR", "timeout", "")); continue
        if code == 3: res.append((cid, fam, k, extra, op, s, "REFUSED", (p.stderr.strip().splitlines() or [""])[-1][:100], "")); continue
        if code != 0 or not os.path.exists(o): res.append((cid, fam, k, extra, op, s, "ERROR", f"exit {code}", "")); continue
        v, t = read_msh(o)
        got = vol(v, t, s); exp = expect(N, int(e))
        if got == exp: res.append((cid, fam, k, extra, op, s, "OK", f"nt={len(t)//3}", "closed" if closed(t) else "OPEN"))
        else:
            sub = "EMPTY" if len(t) == 0 else "VOL"
            ratio = float(got / exp) if exp != 0 else float("inf") if got != 0 else 1.0
            res.append((cid, fam, k, extra, op, s, "WRONG", f"{sub} got/exp={ratio:.6g} nt={len(t)//3}", "closed" if closed(t) else "OPEN"))
        os.remove(o)
    if do_scale: os.remove(A); os.remove(B)
    return res

def main():
    a = sys.argv[1:]
    tag, runner, mag, out = a[0], a[1], a[2], a[3]
    do_scale = "--scale" in a
    jobs = int(a[a.index("--jobs") + 1]) if "--jobs" in a else 6
    fams = set(a[a.index("--fam") + 1].split(",")) if "--fam" in a else None
    rows = [l.rstrip("\n").split("\t") for l in open(f"{mag}/manifest.tsv")][1:]
    if fams: rows = [r for r in rows if r[1] in fams]
    os.makedirs(out, exist_ok=True)
    if "--selfcheck" in a:
        bad = 0
        for r in rows:
            for f, N in ((r[5], r[7]), (r[6], r[8])):
                v, t = read_msh(f"{mag}/{f}")
                if vol(v, t) != expect(N, int(r[4])) or not closed(t): bad += 1; print("SELFCHECK FAIL", r[0], f)
        print(f"selfcheck: {len(rows)} pairs, {bad} bad"); return
    with Pool(jobs) as pool:
        allr = [x for rs in pool.imap(work, [(tag, runner, mag, out, do_scale, r) for r in rows]) for x in rs]
    with open(f"{out}/{tag}.tsv", "w") as f:
        f.write("id\tfamily\tk\textra\top\tscale_s\tverdict\tdetail\tclosed\n")
        for r in allr: f.write("\t".join(map(str, r)) + "\n")
    from collections import Counter
    print(tag, dict(Counter(r[6] for r in allr)))

if __name__ == "__main__":
    main()
