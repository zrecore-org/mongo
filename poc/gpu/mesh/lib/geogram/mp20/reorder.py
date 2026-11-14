#!/usr/bin/env python3
"""MP20 step 1 helper. usage: reorder.py <in.msh> <out.msh> <rev|rot>
Same geometry and triangle order, vertex indices permuted exactly as the MP18b guard v2 did: rev: i -> n-1-i ; rot: i -> (i + n//2) mod n."""
import struct, sys
src, dst, mode = sys.argv[1:4]
d = open(src, "rb").read(); magic, nv, nt = struct.unpack_from("<3I", d, 0)
v = struct.unpack_from("<%dd" % (3 * nv), d, 12); t = struct.unpack_from("<%dI" % (3 * nt), d, 12 + 24 * nv)
perm = [nv - 1 - i for i in range(nv)] if mode == "rev" else [(i + nv // 2) % nv for i in range(nv)]
nvv = [None] * nv
for i in range(nv): nvv[perm[i]] = v[3 * i:3 * i + 3]
out = struct.pack("<3I", magic, nv, nt) + struct.pack("<%dd" % (3 * nv), *[c for p in nvv for c in p]) + struct.pack("<%dI" % (3 * nt), *[perm[i] for i in t])
open(dst, "wb").write(out)
