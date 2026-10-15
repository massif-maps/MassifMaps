#!/usr/bin/env python3 -I
"""Cut a walkable route with several turns out of the LOCAL vector tiles (no network, no routing engine).

  python3 -I make-grenoble-route.py <europe.pmtiles> <out.json> [lon lat]

Reads the z14 tile around lon/lat (default: Grenoble, Place Victor Hugo), takes the street lines of the
`transportation` layer, joins them at shared vertices and walks the shortest path between two
intersections about 350-500 m apart with at least 4 turns. Output: {"points": [[lon, lat], ...]}.
Pure python: PMTiles v3 directory + a minimal MVT/protobuf reader.
"""
import gzip
import heapq
import json
import math
import struct
import sys
import zlib

STREETS = {"primary", "secondary", "tertiary", "minor", "service", "residential", "pedestrian", "living_street", "street", "trunk"}


def varint(buf, pos):
    shift = value = 0
    while True:
        byte = buf[pos]
        pos += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, pos
        shift += 7


def hilbert(z, x, y):
    base = sum(4 ** i for i in range(z))
    n = 1 << z
    d, s, tx, ty = 0, n // 2, x, y
    while s > 0:
        rx = 1 if (tx & s) > 0 else 0
        ry = 1 if (ty & s) > 0 else 0
        d += s * s * ((3 * rx) ^ ry)
        if ry == 0:
            if rx == 1:
                tx, ty = s - 1 - tx, s - 1 - ty
            tx, ty = ty, tx
        s //= 2
    return base + d


def read_directory(raw, compressed):
    data = gzip.decompress(raw) if compressed == 2 else raw
    count, pos = varint(data, 0)
    ids, runs, lengths, offsets = [], [], [], []
    last = 0
    for _ in range(count):
        delta, pos = varint(data, pos)
        last += delta
        ids.append(last)
    for _ in range(count):
        value, pos = varint(data, pos)
        runs.append(value)
    for _ in range(count):
        value, pos = varint(data, pos)
        lengths.append(value)
    for i in range(count):
        value, pos = varint(data, pos)
        if value == 0 and i > 0:
            offsets.append(offsets[i - 1] + lengths[i - 1])
        else:
            offsets.append(value - 1)
    return list(zip(ids, runs, lengths, offsets))


def find_tile(path, z, x, y):
    with open(path, "rb") as handle:
        head = handle.read(127)
        root_off, root_len, _, _, leaf_off, _, data_off, _ = struct.unpack("<8Q", head[8:72])
        internal, tile_comp = head[97], head[98]
        target = hilbert(z, x, y)
        offset, length = root_off, root_len
        for _ in range(4):
            handle.seek(offset)
            entries = read_directory(handle.read(length), internal)
            hit = None
            for entry_id, run, entry_len, entry_off in entries:
                if run == 0:
                    if entry_id <= target:
                        hit = (entry_id, run, entry_len, entry_off)
                elif entry_id <= target < entry_id + run:
                    hit = (entry_id, run, entry_len, entry_off)
                    break
            if hit is None:
                return None
            if hit[1] == 0:
                offset, length = leaf_off + hit[3], hit[2]
                continue
            handle.seek(data_off + hit[3])
            tile = handle.read(hit[2])
            return gzip.decompress(tile) if tile_comp == 2 else tile
    return None


def fields(buf):
    pos = 0
    while pos < len(buf):
        key, pos = varint(buf, pos)
        number, kind = key >> 3, key & 7
        if kind == 0:
            value, pos = varint(buf, pos)
        elif kind == 2:
            size, pos = varint(buf, pos)
            value = buf[pos:pos + size]
            pos += size
        elif kind == 1:
            value = buf[pos:pos + 8]
            pos += 8
        elif kind == 5:
            value = buf[pos:pos + 4]
            pos += 4
        else:
            raise ValueError("wire type %d" % kind)
        yield number, value


def decode_lines(layer_buf):
    keys, values, features, extent = [], [], [], 4096
    for number, value in fields(layer_buf):
        if number == 3:
            keys.append(value.decode())
        elif number == 4:
            for n, v in fields(value):
                if n == 1:
                    values.append(v.decode())
                else:
                    values.append(v)
        elif number == 5:
            extent = value
        elif number == 2:
            features.append(value)
    lines = []
    for feature in features:
        tags, geometry, kind = [], b"", 0
        for number, value in fields(feature):
            if number == 2:
                pos = 0
                while pos < len(value):
                    item, pos = varint(value, pos)
                    tags.append(item)
            elif number == 3:
                kind = value
            elif number == 4:
                geometry = value
        props = {keys[tags[i]]: values[tags[i + 1]] for i in range(0, len(tags) - 1, 2)}
        if kind != 2:
            continue
        pos, x, y, current = 0, 0, 0, []
        parts = []
        while pos < len(geometry):
            command, pos = varint(geometry, pos)
            op, count = command & 7, command >> 3
            for _ in range(count):
                if op in (1, 2):
                    dx, pos = varint(geometry, pos)
                    dy, pos = varint(geometry, pos)
                    x += (dx >> 1) ^ -(dx & 1)
                    y += (dy >> 1) ^ -(dy & 1)
                    if op == 1 and current:
                        parts.append(current)
                        current = []
                    current.append((x, y))
        if current:
            parts.append(current)
        for part in parts:
            lines.append((props, part))
    return lines, extent


def to_lonlat(z, tx, ty, x, y, extent):
    n = 2 ** z
    lon = (tx + x / extent) / n * 360.0 - 180.0
    lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * (ty + y / extent) / n))))
    return lon, lat


def turn_angle(a, b, c):
    ax, ay, bx, by = b[0] - a[0], b[1] - a[1], c[0] - b[0], c[1] - b[1]
    return abs(math.degrees(math.atan2(ax * by - ay * bx, ax * bx + ay * by)))


def main():
    path, out = sys.argv[1], sys.argv[2]
    lon = float(sys.argv[3]) if len(sys.argv) > 3 else 5.7245
    lat = float(sys.argv[4]) if len(sys.argv) > 4 else 45.1885
    z = 14
    n = 2 ** z
    tx = int((lon + 180.0) / 360.0 * n)
    ty = int((1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2 * n)
    tile = find_tile(path, z, tx, ty)
    if tile is None:
        sys.exit("tile not found")
    roads = []
    for number, value in fields(tile):
        if number == 3:
            name = next(v for n_, v in fields(value) if n_ == 1).decode()
            if name == "transportation":
                lines, extent = decode_lines(value)
                roads = [(p, l) for p, l in lines if p.get("class") in STREETS]
    graph = {}
    xy = lambda node: node
    for _, line in roads:
        for a, b in zip(line, line[1:]):
            length = math.hypot(a[0] - b[0], a[1] - b[1]) * (2 * math.pi * 6371000 * math.cos(math.radians(lat)) / n / extent)
            graph.setdefault(a, []).append((b, length))
            graph.setdefault(b, []).append((a, length))

    def metres(p):
        scale = 2 * math.pi * 6371000 * math.cos(math.radians(lat)) / n / extent
        return p[0] * scale, p[1] * scale

    centre = min(graph, key=lambda p: math.hypot(*(a - b for a, b in zip(metres(p), metres((int(((lon + 180) / 360 * n - tx) * extent), int(((1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2 * n - ty) * extent)))))))

    def dijkstra(start):
        dist, prev, heap = {start: 0.0}, {}, [(0.0, start)]
        while heap:
            d, node = heapq.heappop(heap)
            if d > dist[node]:
                continue
            for nxt, w in graph[node]:
                nd = d + w
                if nd < dist.get(nxt, 1e18):
                    dist[nxt], prev[nxt] = nd, node
                    heapq.heappush(heap, (nd, nxt))
        return dist, prev

    dist, prev = dijkstra(centre)
    best = None
    for goal, d in dist.items():
        if not 350 <= d <= 500:
            continue
        chain = [goal]
        while chain[-1] != centre:
            chain.append(prev[chain[-1]])
        chain.reverse()
        simple = [chain[0]]
        for p in chain[1:]:
            simple.append(p)
        turns = sum(1 for a, b, c in zip(simple, simple[1:], simple[2:]) if 35 < turn_angle(a, b, c) < 150)
        if turns >= 4 and (best is None or turns > best[0]):
            best = (turns, simple, d)
    if best is None:
        sys.exit("no route with 4 turns found")
    turns, chain, d = best
    points = [list(to_lonlat(z, tx, ty, x, y, extent)) for x, y in chain]
    json.dump({"points": points, "turns": turns, "length_m": round(d)}, open(out, "w"))
    print("route %d m, %d points, %d turns -> %s" % (d, len(points), turns, out))


main()
