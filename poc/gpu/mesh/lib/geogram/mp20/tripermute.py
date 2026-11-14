#!/usr/bin/env python3
"""MP20 step 1 predictive probe. usage: tripermute.py <in.msh> <out.msh> <k>   same geometry and vertex indices; triangle list rotated left by k."""
import struct, sys
src, dst, k = sys.argv[1], sys.argv[2], int(sys.argv[3])
d = open(src, "rb").read(); magic, nv, nt = struct.unpack_from("<3I", d, 0)
tris = struct.unpack_from("<%dI" % (3 * nt), d, 12 + 24 * nv)
T = [tris[3 * i:3 * i + 3] for i in range(nt)]; k %= nt; T = T[k:] + T[:k]
open(dst, "wb").write(d[:12 + 24 * nv] + struct.pack("<%dI" % (3 * nt), *[i for t in T for i in t]))
