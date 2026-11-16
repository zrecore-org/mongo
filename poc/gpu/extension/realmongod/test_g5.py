#!/usr/bin/env python3
# MP14b route B tests against a real mongod built from the fork tree (CPU only). Needs pymongo (pip install pymongo in a venv). Usage: test_g5.py <port>
import math, random, struct, sys, time
from fractions import Fraction
import pymongo
from bson import Binary
port = int(sys.argv[1]) if len(sys.argv) > 1 else 27555
c = pymongo.MongoClient(f"mongodb://127.0.0.1:{port}", serverSelectionTimeoutMS=5000)
db = c.g5; col = db.meshes
fails = 0
def check(ok, what):
    global fails
    print(("PASS " if ok else "FAIL ") + what); fails += 0 if ok else 1
def pack(v, t): return Binary(struct.pack("<III", 0x3148534d, len(v), len(t)) + b"".join(struct.pack("<ddd", *p) for p in v) + b"".join(struct.pack("<III", *x) for x in t), 0)
def box(a, b, c_, o):
    v = [(o[0] + x * a, o[1] + y * b, o[2] + z * c_) for x in (0, 1) for y in (0, 1) for z in (0, 1)]
    q = [(0,1,3,2),(4,6,7,5),(0,4,5,1),(2,3,7,6),(0,2,6,4),(1,5,7,3)]
    t = []
    for f in q: t += [(f[0], f[1], f[2]), (f[0], f[2], f[3])]
    return v, t
def sphere(nu, nv, r, o):
    v = [(o[0], o[1], o[2] + r)]
    for i in range(1, nu):
        for j in range(nv): th, ph = math.pi * i / nu, 2 * math.pi * j / nv; v.append((o[0] + r * math.sin(th) * math.cos(ph), o[1] + r * math.sin(th) * math.sin(ph), o[2] + r * math.cos(th)))
    v.append((o[0], o[1], o[2] - r)); t = []
    for j in range(nv): t.append((0, 1 + j, 1 + (j + 1) % nv))
    for i in range(nu - 2):
        for j in range(nv):
            a, b, c2, d = 1 + i * nv + j, 1 + i * nv + (j + 1) % nv, 1 + (i + 1) * nv + j, 1 + (i + 1) * nv + (j + 1) % nv
            t += [(a, c2, b), (b, c2, d)]
    last = len(v) - 1
    for j in range(nv): t.append((last, 1 + (nu - 2) * nv + (j + 1) % nv, 1 + (nu - 2) * nv + j))
    return v, t
def vol(v, t):   # exact: sum of det/6 over exact Fractions of the stored doubles (signed volume, origin-based)
    s = Fraction(0)
    for a, b, c_ in t:
        A, B, C = [[Fraction(x) for x in v[i]] for i in (a, b, c_)]
        s += A[0]*(B[1]*C[2]-B[2]*C[1]) - A[1]*(B[0]*C[2]-B[2]*C[0]) + A[2]*(B[0]*C[1]-B[1]*C[0])
    return s / 6
random.seed(2026); docs = []; want = {}
for i in range(120):
    v, t = box(random.choice([1, 2, 3, 0.1, 7.5]), random.choice([1, 2, 0.3]), random.choice([1, 5, 0.7]), (random.choice([0, 1e8, 0.1]), random.choice([0, 3, -1e7]), 0))
    docs.append({"_id": i, "kind": "box", "mesh": pack(v, t)}); want[i] = vol(v, t)
for i in range(120, 220):
    v, t = sphere(random.randint(3, 12), random.randint(3, 16), random.choice([1.0, 0.37, 1e3]), (random.choice([0, 1e6]), 0.5, 0))
    docs.append({"_id": i, "kind": "sphere", "mesh": pack(v, t)}); want[i] = vol(v, t)
col.drop(); col.insert_many(docs)
nan_v, nan_t = box(1, 1, 1, (0, 0, 0)); nan_v[3] = (float("nan"), 0.0, 0.0)
bad = [{"_id": 1000, "kind": "nan", "mesh": pack(nan_v, nan_t)}, {"_id": 1001, "kind": "nomesh"}, {"_id": 1002, "kind": "magic", "mesh": Binary(b"XXXX" + b"\0" * 20, 0)}]
col.insert_many(bad)
print("server", c.server_info()["version"], "docs", col.count_documents({}))
# 1. correctness through a real aggregation with several getMore batches
cur = col.aggregate([{"$match": {"kind": {"$in": ["box", "sphere"]}}}, {"$meshVolume": {"input": "mesh", "output": "v", "batchSize": 32}}], batchSize=17)
res = list(cur)
check(len(res) == 220, f"220 results through $meshVolume with cursor batchSize 17 (got {len(res)})")
wrong = 0
for d in res:
    ex = d["v"].get("exact")
    if ex is None or Fraction(ex) != abs(want[d["_id"]]) and Fraction(ex) != want[d["_id"]] or d["v"]["approx"] != float(Fraction(ex)): wrong += 1
check(wrong == 0, f"exact volume equals the Fraction oracle and approx is its correct rounding on every document (wrong {wrong})")
# 2. refusals return per-document errors, not a crash
r2 = {d["_id"]: d for d in col.aggregate([{"$match": {"_id": {"$gte": 1000}}}, {"$meshVolume": {"input": "mesh", "output": "v"}}])}
print("refusals:", {k: v.get("v") for k, v in r2.items()})
check(all("error" in v.get("v", {}) for v in r2.values()) and len(r2) == 3, "NaN, missing field and bad magic documents give an error object each")
check(c.admin.command("ping")["ok"] == 1, "server healthy after the refusals")
# 3. unknown stage field is a parse error from the extension
try: list(col.aggregate([{"$meshVolume": {"bogus": 1}}])); check(False, "bad stage spec rejected")
except pymongo.errors.OperationFailure as e: check(True, f"bad stage spec rejected by the extension: code {e.code}")
check(c.admin.command("ping")["ok"] == 1, "server healthy after the parse error")
# 4. probes (informational): vulkan loader inside the real mongod, counters
for pr in ("vulkan", "counters", "symbol"):
    r = list(col.aggregate([{"$limit": 1}, {"$meshVolume": {"input": "mesh", "output": "p", "probe": pr}}]))
    print("PROBE", pr, r[0].get("p"))
check(c.admin.command("ping")["ok"] == 1, "server healthy after the probes")
# 5. deadline behaviour of a cooperative and a non-polling stage (informational)
for plant, ms in (("spinRude", 600),):
    t0 = time.time()
    try: list(col.aggregate([{"$limit": 1}, {"$meshVolume": {"input": "mesh", "output": "p", "plant": plant, "spinMs": ms}}], maxTimeMS=100)); out = "returned"
    except pymongo.errors.PyMongoError as e: out = f"error {type(e).__name__} {getattr(e, 'code', '')}"
    print(f"DEADLINE plant={plant} spinMs={ms} maxTimeMS=100 -> {out} after {1000*(time.time()-t0):.0f} ms")
check(c.admin.command("ping")["ok"] == 1, "server healthy after the deadline test")
print("FAILS", fails); sys.exit(1 if fails else 0)
