# MP14d: one planted device failure in a REAL mongod on a real device: results must stay equal to the oracle, the failure must be the intended one, the device must be disabled afterwards.
# Usage: plant_check.py <port> <real60 dir> <plant: gpuCreateFail|gpuOOM|gpuTimeout>
import sys, os
from pymongo import MongoClient
from bson.binary import Binary
port, d, plant = int(sys.argv[1]), sys.argv[2], sys.argv[3]
want = {"gpuCreateFail": "planted failure of the lazy device creation", "gpuOOM": ("vkAllocateMemory", "vkCreateBuffer"), "gpuTimeout": "vkWaitForFences"}[plant]
c = MongoClient("127.0.0.1", port, serverSelectionTimeoutMS=20000); col = c["mp14d"]["plant"]; col.drop()
ids = [l.strip() for l in open(os.path.join(d, "ids.txt")) if l.strip()][:8]; exp = {}
for i in ids:
    exp[i] = open(os.path.join(d, i + ".exp"), "rb").read().decode()
    col.insert_one({"_id": i, "mesh": Binary(open(os.path.join(d, i + ".mesh"), "rb").read(), 0), "points": Binary(open(os.path.join(d, i + ".pts"), "rb").read(), 0)})
fails = 0
def check(n, ok, x=""):
    global fails; print(("PASS " if ok else "FAIL ") + n + (" " + x if x else "")); fails += 0 if ok else 1
spec = {"input": "mesh", "points": "points", "output": "v", "gpuMinPoints": 1, "plant": plant}
res = list(col.aggregate([{"$meshContains": spec}]))
check("%s: results equal the oracle (CPU fallback)" % plant, len(res) == len(ids) and all(r["v"] == exp[r["_id"]] for r in res))
cfg = list(col.aggregate([{"$limit": 1}, {"$meshContains": {"input": "mesh", "points": "points", "output": "cfg", "probe": "config"}}]))[0]["cfg"]; print("COUNTERS", cfg)
wants = want if isinstance(want, tuple) else (want,)   # the driver refuses at vkAllocateMemory (Dozen) or already at vkCreateBuffer (NVIDIA)
check("%s: the intended failure fired (%r in gpuLastFailure)" % (plant, wants), any(w in cfg.get("gpuLastFailure", "") for w in wants), cfg.get("gpuLastFailure", ""))
# gpuTimeout waits 1 ns on the fence: if the device has already finished (it can be fast) the wait SUCCEEDS and that batch counts; a later document then times out. So for the timeout plant the number of
# completed device batches may be 0..n-1; the contract is: exactly one fallback, device disabled afterwards, results correct.
nb = cfg.get("gpuBatches", -1); okb = (nb == 0) if plant != "gpuTimeout" else (0 <= nb < len(ids))
check("%s: device disabled, engine cpu, 1 fallback, %s" % (plant, "0 device batches" if plant != "gpuTimeout" else "completed device batches %s (0..%d)" % (nb, len(ids) - 1)), cfg.get("engine") == "cpu" and cfg.get("gpuFallbacks") == 1 and okb)
spec2 = {"input": "mesh", "points": "points", "output": "v", "gpuMinPoints": 1}
res = list(col.aggregate([{"$meshContains": spec2}]))
check("%s: later aggregations still correct (CPU)" % plant, len(res) == len(ids) and all(r["v"] == exp[r["_id"]] for r in res))
print("FAILS", fails); sys.exit(1 if fails else 0)
