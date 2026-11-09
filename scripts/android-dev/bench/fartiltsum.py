import re, sys, statistics
# Median fps, drape ms and tiles drawn per frame, per [label], from fartilt.sh output (a PROF line is followed by its RenderStats).
prof = re.compile(r"\[(?P<label>[^\]]+)\].*PROF: (?P<frames>\d+) frames in (?P<ms>\d+) ms \((?P<fps>[\d.]+) fps\).*? drape (?P<drape>[\d.]+) ")
stats = re.compile(r"\[(?P<label>[^\]]+)\].*RenderStats: .*renderTiles=(?P<tiles>\d+)")
groups, last = {}, None
for line in sys.stdin:
    m = prof.search(line)
    if m:
        last = (m.group("label"), int(m.group("frames")), float(m.group("ms")), float(m.group("fps")), float(m.group("drape")))
        continue
    m = stats.search(line)
    if m and last and last[0] == m.group("label"):
        if last[2] <= 1600 and last[1] > 0:
            groups.setdefault(last[0], []).append((last[3], int(m.group("tiles")) / last[1], last[4]))
        last = None
print(f"{'config':<24} {'n':>3} {'fps med':>8} {'drape ms':>9} {'tiles/frame':>12}")
for label, rows in groups.items():
    print(f"{label:<24} {len(rows):>3} {statistics.median(r[0] for r in rows):>8.1f} {statistics.median(r[2] for r in rows):>9.1f} {statistics.median(r[1] for r in rows):>12.1f}")
