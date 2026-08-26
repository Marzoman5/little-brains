"""Merge sweep chunk files into one results JSON, printing an integrity checksum."""
import json, sys

out = sys.argv[1]
chunks = sys.argv[2:]
merged = []
for c in chunks:
    merged += json.load(open(c, encoding="utf-8"))

cnt = 0
s = 0.0

def walk(v):
    global cnt, s
    if isinstance(v, bool):
        return
    if isinstance(v, (int, float)):
        s += v
        cnt += 1
    elif isinstance(v, list):
        for x in v:
            walk(x)
    elif isinstance(v, dict):
        for x in v.values():
            walk(x)

walk(merged)
print("jobs:", len(merged), "leaves:", cnt, "checksum:", round(s, 6))
json.dump(merged, open(out, "w", encoding="utf-8"))
