# MP14d: LABELLED speed table through a real mongod: one 9,600-triangle-class mesh, N random points per document in a BinData, warm second run, engine "cpu" versus the device path (gpuMinPoints 1).
# Not a G4 claim: one document per aggregation, one-thread CPU path inside the stage. Usage: bench_points.py <port> <real60 dir>
import sys, os, time, random, struct
from pymongo import MongoClient
from bson.binary import Binary
port, d = int(sys.argv[1]), sys.argv[2]
c = MongoClient("127.0.0.1", port, serverSelectionTimeoutMS=20000); col = c["mp14d"]["bench"]
ids = [l.strip() for l in open(os.path.join(d, "ids.txt")) if l.strip()]
best = None
for i in ids:   # the model whose triangle count is closest to 10,000
    nt = struct.unpack("<3I", open(os.path.join(d, i + ".mesh"), "rb").read(12))[2]
    if best is None or abs(nt - 10000) < abs(best[1] - 10000): best = (i, nt)
mid, nt = best; mesh = open(os.path.join(d, mid + ".mesh"), "rb").read()
nv = struct.unpack("<3I", mesh[:12])[1]; v = struct.unpack("<%dd" % (nv * 3), mesh[12:12 + nv * 24])
lo = [min(v[k::3]) for k in range(3)]; hi = [max(v[k::3]) for k in range(3)]
rnd = random.Random(7); print("model %s triangles %d" % (mid, nt))
print("points   | engine cpu ms | device ms | ratio cpu/device   (warm, one document)")
fails = 0
for n in (10000, 50000, 200000, 600000):
    pts = struct.pack("<%dd" % (3 * n), *[lo[k] + (hi[k] - lo[k]) * (1.3 * rnd.random() - 0.15) for _ in range(n) for k in range(3)])
    col.drop(); col.insert_one({"_id": 1, "mesh": Binary(mesh, 0), "points": Binary(pts, 0)})
    out = {}
    for name, extra in (("cpu", {"engine": "cpu"}), ("dev", {"gpuMinPoints": 1})):
        spec = {"input": "mesh", "points": "points", "output": "v"}; spec.update(extra)
        list(col.aggregate([{"$meshContains": spec}]))          # warm up (device creation, caches)
        t0 = time.time(); r = list(col.aggregate([{"$meshContains": spec}])); out[name] = ((time.time() - t0) * 1000, r[0]["v"])
    same = out["cpu"][1] == out["dev"][1]; fails += 0 if same else 1
    print("%-8d | %13.1f | %9.1f | %.2f %s" % (n, out["cpu"][0], out["dev"][0], out["cpu"][0] / out["dev"][0], "" if same else "VERDICTS DIFFER"))
cfg = list(col.aggregate([{"$limit": 1}, {"$meshContains": {"input": "mesh", "points": "points", "output": "cfg", "probe": "config"}}]))[0]["cfg"]; print("COUNTERS", cfg)
print("FAILS", fails); sys.exit(1 if fails else 0)
