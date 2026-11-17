#!/usr/bin/env python3
"""Crops a vector tile to the layers and the window a label scenario needs, so a fixture stays a few KB.

    crop_tile.py in.pbf out.pbf x0 y0 x1 y1 [layer ...]

Keeps the named layers (default poi, transportation_name) and, in them, the features with a vertex inside
the window, given in the tile's own extent units (z14 tile: 4096). Features are copied byte for byte.
"""
import gzip
import sys


def varint(b, i):
    r = s = 0
    while True:
        c = b[i]
        i += 1
        r |= (c & 0x7f) << s
        if not c & 0x80:
            return r, i
        s += 7


def put_varint(n):
    out = bytearray()
    while n >= 0x80:
        out.append((n & 0x7f) | 0x80)
        n >>= 7
    out.append(n)
    return bytes(out)


def fields(b):
    i = 0
    while i < len(b):
        key, i = varint(b, i)
        tag, wire = key >> 3, key & 7
        if wire == 0:
            v, i = varint(b, i)
        elif wire == 2:
            n, i = varint(b, i)
            v = b[i:i + n]
            i += n
        else:
            raise ValueError("wire type %d" % wire)
        yield tag, wire, v


def put_field(tag, wire, v):
    key = put_varint(tag << 3 | wire)
    return key + (put_varint(v) if wire == 0 else put_varint(len(v)) + v)


def vertices(geometry):
    vals, i, x, y = [], 0, 0, 0
    while i < len(geometry):
        v, i = varint(geometry, i)
        vals.append(v)
    i = 0
    while i < len(vals):
        command, count = vals[i] & 7, vals[i] >> 3
        i += 1
        for _ in range(count if command != 7 else 0):
            x += (vals[i] >> 1) ^ -(vals[i] & 1)
            y += (vals[i + 1] >> 1) ^ -(vals[i + 1] & 1)
            i += 2
            yield x, y


def crop(data, window, keep):
    if data[:2] == b'\x1f\x8b':
        data = gzip.decompress(data)
    x0, y0, x1, y1 = window
    out = bytearray()
    for tag, wire, layer in fields(data):
        if tag != 3 or dict((t, v) for t, w, v in fields(layer)).get(1, b'').decode() not in keep:
            continue
        kept = bytearray()
        for t, w, v in fields(layer):
            if t == 2:
                geometry = next((g for gt, gw, g in fields(v) if gt == 4), b'')
                if not any(x0 <= x <= x1 and y0 <= y <= y1 for x, y in vertices(geometry)):
                    continue
            kept += put_field(t, w, v)
        out += put_field(3, 2, bytes(kept))
    return gzip.compress(bytes(out), mtime=0)


if __name__ == '__main__':
    src, dst = sys.argv[1:3]
    window = [int(v) for v in sys.argv[3:7]]
    layers = set(sys.argv[7:]) or {'poi', 'transportation_name'}
    open(dst, 'wb').write(crop(open(src, 'rb').read(), window, layers))
