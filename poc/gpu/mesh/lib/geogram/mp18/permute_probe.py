#!/usr/bin/env python3
"""MP18 step 2 probes: reorder triangles/corners of touch_* operands (geometry unchanged) so that the first facet's first vertex is / is not
the contact vertex. usage: permute_probe.py <touch_corpus> <outcorpus> <mode: avoid|force> <label d0-VV|d0-VF|...>"""
import csv, os, struct, sys
src, dst, mode, label = sys.argv[1:5]
os.makedirs(dst + "/adversarial", exist_ok=True)
def rd(p):
    d = open(p, "rb").read(); _, nv, nt = struct.unpack_from("<3I", d, 0)
    v = struct.unpack_from("<%dd" % (3*nv), d, 12); t = struct.unpack_from("<%dI" % (3*nt), d, 12 + 24*nv)
    return [tuple(int(x) for x in v[3*i:3*i+3]) for i in range(nv)], [tuple(t[3*i:3*i+3]) for i in range(nt)]
def wr(p, V, T):
    open(p, "wb").write(struct.pack("<3I", 0x3148534d, len(V), len(T)) + struct.pack("<%dd" % (3*len(V)), *[float(c) for v in V for c in v]) + struct.pack("<%dI" % (3*len(T)), *[i for t in T for i in t]))
def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def on_tri(p, a, b, c):
    n = cross(sub(b, a), sub(c, a))
    if dot(n, sub(p, a)) != 0: return False
    for u, v in ((a, b), (b, c), (c, a)):
        if dot(cross(sub(v, u), sub(p, u)), n) < 0: return False
    return True
def on_surface(p, V, T): return any(on_tri(p, V[a], V[b], V[c]) for a, b, c in T)
rows = list(csv.reader(open(src + "/manifest.tsv"), delimiter="\t"))
out = [rows[0]]
for r in rows[1:]:
    if "contact=" + label not in r[2]: continue
    cid = r[0]; VA, TA = rd(f"{src}/adversarial/{cid}_A.msh"); VB, TB = rd(f"{src}/adversarial/{cid}_B.msh")
    contact = {v for v in VA if on_surface(v, VB, TB)} | {v for v in VB if on_surface(v, VA, TA)}
    assert len(contact) == 1, (cid, contact)
    cv = next(iter(contact))
    res = []
    for V, T in ((VA, TA), (VB, TB)):
        # candidate rotations: a (facet, corner) such that vertex at that corner is / is not the contact vertex
        pick = None
        for i, t in enumerate(T):
            for k in range(3):
                is_c = V[t[k]] == cv
                if (mode == "force" and is_c) or (mode == "avoid" and not is_c): pick = (i, k); break
            if pick: break
        if pick is None: pick = (0, 0)
        i, k = pick
        T2 = [tuple(t[(j + (k if n == 0 else 0)) % 3] for j in range(3)) for n, t in enumerate(T[i:] + T[:i])]
        res.append((V, T2))
    wr(f"{dst}/adversarial/{cid}_A.msh", *res[0]); wr(f"{dst}/adversarial/{cid}_B.msh", *res[1]); out.append(r)
csv.writer(open(dst + "/manifest.tsv", "w", newline=""), delimiter="\t").writerows(out)
print(mode, label, len(out) - 1, "pairs written to", dst)
