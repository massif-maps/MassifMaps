import re, sys, collections
# Summarize dem.sh DEM lines per [label]. Counters are per-interval sums, so compare builds per encode;
# intervals with no encode are dropped (their node time belongs to a neighbouring interval's tile).
enc = re.compile(r"\[(?P<label>[^\]]+)\].*dem encodes=(?P<encodes>\d+) patches=(?P<patches>\d+) encodeWorkerMs=(?P<worker>[\d.]+)")
spl = re.compile(r"\[(?P<label>[^\]]+)\].*demEncode textureMs=(?P<tex>[\d.]+) bitmapMs=(?P<bmp>[\d.]+) nodeMs=(?P<node>[\d.]+)")
box = re.compile(r"\[(?P<label>[^\]]+)\].*demNode edgeCalls=(?P<calls>\d+) boxTexelsPerCall=(?P<texels>\d+)")

tot = collections.defaultdict(lambda: collections.Counter())
for line in sys.stdin:
    m = enc.search(line)
    if m and int(m.group("encodes")) > 0:
        t = tot[m.group("label")]
        t["encodes"] += int(m.group("encodes"))
        t["patches"] += int(m.group("patches"))
        t["worker"] += float(m.group("worker"))
        t["intervals"] += 1
        continue
    m = spl.search(line)
    if m:
        t = tot[m.group("label")]
        t["tex"] += float(m.group("tex"))
        t["bmp"] += float(m.group("bmp"))
        t["node"] += float(m.group("node"))
        continue
    m = box.search(line)
    if m:
        t = tot[m.group("label")]
        t["calls"] += int(m.group("calls"))
        t["boxsum"] += int(m.group("texels"))
        t["boxn"] += 1

print(f"{'config':<20} {'enc':>5} {'worker/enc':>11} {'node/enc':>9} {'tex/enc':>8} {'edgeCalls':>10} {'boxTexels':>10}")
for label, t in tot.items():
    n = max(1, t["encodes"])
    boxavg = t["boxsum"] / max(1, t["boxn"])
    print(f"{label:<20} {t['encodes']:>5} {t['worker']/n:>11.1f} {t['node']/n:>9.1f} {t['tex']/n:>8.1f} {t['calls']:>10} {boxavg:>10.0f}")
