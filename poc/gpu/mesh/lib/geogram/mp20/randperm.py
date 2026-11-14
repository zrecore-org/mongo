#!/usr/bin/env python3
"""MP20 step 1 helper. usage: randperm.py <in.msh> <out.msh> <seed>   same geometry, same triangle order, vertex indices randomly permuted (same permutation applied to the file's vertices)."""
import random, struct, sys
src, dst, seed = sys.argv[1], sys.argv[2], int(sys.argv[3])
d = open(src, "rb").read(); magic, nv, nt = struct.unpack_from("<3I", d, 0)
v = struct.unpack_from("<%dd" % (3 * nv), d, 12); t = struct.unpack_from("<%dI" % (3 * nt), d, 12 + 24 * nv)
perm = list(range(nv)); random.Random(seed).shuffle(perm)
nvv = [None] * nv
for i in range(nv): nvv[perm[i]] = v[3 * i:3 * i + 3]
open(dst, "wb").write(struct.pack("<3I", magic, nv, nt) + struct.pack("<%dd" % (3 * nv), *[c for p in nvv for c in p]) + struct.pack("<%dI" % (3 * nt), *[perm[i] for i in t]))
