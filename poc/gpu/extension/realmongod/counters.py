# prints the extension's config/counter document: counters.py <port>
import sys
from pymongo import MongoClient
c = MongoClient("127.0.0.1", int(sys.argv[1]), serverSelectionTimeoutMS=5000)
r = list(c["mp14d"]["loop"].aggregate([{"$limit": 1}, {"$meshContains": {"input": "mesh", "points": "points", "output": "cfg", "probe": "config"}}]))
print(r[0]["cfg"] if r else {})
