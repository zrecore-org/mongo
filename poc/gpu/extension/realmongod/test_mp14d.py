# MP14d bar 1 (CPU path) and error handling through a REAL mongod aggregation. Usage: test_mp14d.py <port> <real60 dir>
# Expected verdicts come from the exactmesh oracle (cpu/pip_hardened.h), written by real_contains.cpp; the same 540,000 queries as MP12e.
import sys, os, struct
from pymongo import MongoClient
from bson.binary import Binary
port, d = int(sys.argv[1]), sys.argv[2]
GPU_MIN = int(os.environ.get("MP14D_GPU_MIN", "0")); EXPECT_GPU = os.environ.get("EXPECT_GPU", "")   # EXPECT_GPU: "1" device path must be used, "0" must not
SPEC = {"input": "mesh", "points": "points", "output": "v"}
if GPU_MIN: SPEC["gpuMinPoints"] = GPU_MIN
c = MongoClient("127.0.0.1", port, serverSelectionTimeoutMS=20000)
db = c["mp14d"]; col = db["real60"]; col.drop()
ids = [l.strip() for l in open(os.path.join(d, "ids.txt")) if l.strip()]
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("PASS " if ok else "FAIL ") + name + (" " + extra if extra else ""))
    if not ok: fails += 1
exp = {}
for i in ids:
    mesh = open(os.path.join(d, i + ".mesh"), "rb").read(); pts = open(os.path.join(d, i + ".pts"), "rb").read()
    exp[i] = open(os.path.join(d, i + ".exp"), "rb").read().decode()
    assert len(pts) // 24 == len(exp[i])
    col.insert_one({"_id": i, "mesh": Binary(mesh, 0), "points": Binary(pts, 0)})
check("inserted %d model documents" % len(ids), col.count_documents({}) == len(ids))
h = 1469598103934665603; total = 0; bad = 0; on = 0
code = {"O": 0, "I": 1, "B": 2}
for batch in (1000, 7):   # one big batch, and a tiny cursor batch size (stage still pulls its own batches)
    res = list(col.aggregate([{"$meshContains": SPEC}], batchSize=batch))
    got = {r["_id"]: r["v"] for r in res}
    check("batchSize %d: %d documents returned" % (batch, len(res)), len(res) == len(ids))
    nb = sum(1 for i in ids if got.get(i) != exp[i])
    check("batchSize %d: every verdict string equals the oracle" % batch, nb == 0, "mismatched models=%d" % nb)
for i in ids:
    for ch in exp[i]:
        h ^= code[ch]; h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        total += 1; on += ch == "B"
print("QUERIES %d ON %d resolved-verdict hash=%016x" % (total, on, h))
check("hash equals the MP14d reference ae59d9976297a67b", "%016x" % h == "ae59d9976297a67b")
def counters():
    r = list(col.aggregate([{"$meshContains": {"input": "mesh", "points": "points", "output": "cfg", "probe": "config"}}]))
    return r[0]["cfg"] if r else {}
if EXPECT_GPU in ("0", "1"):
    cfg = counters(); print("COUNTERS", cfg)
    if EXPECT_GPU == "1":
        check("device path used: gpuBatches > 0, UNCERTAIN resolved exactly (gpuUncertain > 0), 0 fallbacks, 0 shadow mismatches, engine gpu",
              cfg.get("gpuBatches", 0) > 0 and cfg.get("gpuUncertain", 0) > 0 and cfg.get("gpuFallbacks", 1) == 0 and cfg.get("gpuShadowMismatch", 1) == 0 and cfg.get("engine") == "gpu", str(cfg))
        # planted wrong device verdicts in a real mongod: the shadow check must catch it, results must still equal the oracle, then the device path is disabled
        ids0 = ids[:6]; wrong = SPEC.copy(); wrong["plant"] = "gpuWrong"
        res = list(col.aggregate([{"$match": {"_id": {"$in": ids0}}}, {"$meshContains": wrong}]))
        check("planted wrong verdicts: results still equal the oracle", all(r["v"] == exp[r["_id"]] for r in res) and len(res) == len(ids0))
        cfg2 = counters(); print("COUNTERS", cfg2)
        check("shadow check fired once, fell back, device disabled (engine cpu)", cfg2.get("gpuShadowMismatch") == 1 and cfg2.get("gpuFallbacks", 0) >= 1 and cfg2.get("engine") == "cpu", str(cfg2))
        res = list(col.aggregate([{"$meshContains": SPEC}]))
        check("after the breaker: results still equal the oracle on all 60 models", all(r["v"] == exp[r["_id"]] for r in res) and len(res) == len(ids))
    else:
        check("software device: no device batch", cfg.get("gpuBatches", 1) == 0 and cfg.get("engine") == "cpu", str(cfg))
# the pipeline can also use the points array form and $meshVolume still works
big = ids[0]; mesh = open(os.path.join(d, big + ".mesh"), "rb").read()
col.drop(); col.insert_one({"_id": 1, "mesh": Binary(mesh, 0), "points": [[0.0, 0.0, 0.0], [1e9, 1e9, 1e9]]})
r = list(col.aggregate([{"$meshContains": {"input": "mesh", "points": "points", "output": "v"}}]))
check("points as an array of arrays", len(r) == 1 and isinstance(r[0].get("v"), str) and len(r[0]["v"]) == 2 and r[0]["v"][1] == "O", str(r[0].get("v")))
r = list(col.aggregate([{"$meshVolume": {"input": "mesh", "output": "vol"}}]))
check("$meshVolume still works", len(r) == 1 and "exact" in r[0]["vol"], str(r[0]["vol"])[:80])
col.drop(); col.insert_many([{"_id": 1, "mesh": Binary(mesh, 0)}, {"_id": 2, "points": [[0.0, 0.0, 0.0]]}, {"_id": 3, "mesh": Binary(b"junk", 0), "points": [[0.0, 0.0, 0.0]]},
                              {"_id": 4, "mesh": Binary(mesh, 0), "points": [[0.0, 0.0]]}])
r = {x["_id"]: x for x in col.aggregate([{"$meshContains": {"input": "mesh", "points": "points", "output": "v"}}])}
codes = {k: (v["v"]["error"]["code"] if isinstance(v.get("v"), dict) else None) for k, v in r.items()}
check("per-document errors (missing points 1, missing mesh 1, bad header 4, bad point 15)", codes == {1: 1, 2: 1, 3: 4, 4: 15}, str(codes))
try:
    list(col.aggregate([{"$meshContains": {"bogus": 1}}])); check("unknown stage field is rejected", False)
except Exception as e:
    check("unknown stage field is rejected", True, str(e)[:100])
print("FAILS", fails)
sys.exit(1 if fails else 0)
