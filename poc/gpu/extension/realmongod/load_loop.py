# MP14d: keeps $meshContains aggregations (device path forced with gpuMinPoints 1) running until the server goes away; writes one line per completed aggregation
# ("<n> <unix time>") to <progress file> and a start line before each one, so the runner can prove aggregations were in flight at the SIGTERM. Usage: load_loop.py <port> <real60 dir> <seconds> <progress file>
import sys, os, time
from pymongo import MongoClient
from bson.binary import Binary
port, d, secs, prog = int(sys.argv[1]), sys.argv[2], float(sys.argv[3]), sys.argv[4]
c = MongoClient("127.0.0.1", port, serverSelectionTimeoutMS=5000); col = c["mp14d"]["loop"]; col.drop()
ids = [l.strip() for l in open(os.path.join(d, "ids.txt")) if l.strip()][:30]
for i in ids: col.insert_one({"_id": i, "mesh": Binary(open(os.path.join(d, i + ".mesh"), "rb").read(), 0), "points": Binary(open(os.path.join(d, i + ".pts"), "rb").read(), 0)})
print("LOOP STARTED", flush=True)
f = open(prog, "w", buffering=1); end = time.time() + secs; n = 0
try:
    while time.time() < end:
        f.write("start %d %.3f\n" % (n, time.time()))
        list(col.aggregate([{"$meshContains": {"input": "mesh", "points": "points", "output": "v", "gpuMinPoints": 1}}])); n += 1
        f.write("done %d %.3f\n" % (n, time.time()))
except Exception as e:
    f.write("ended %s %.3f\n" % (type(e).__name__, time.time())); print("LOOP ENDED by", type(e).__name__, "after", n, "aggregations", flush=True)
else:
    print("LOOP FINISHED", n, flush=True)
