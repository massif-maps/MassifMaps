#!/usr/bin/env python3
"""Generate the Massif 3D models (trees, wind turbine) as GLB files.

Low-poly and vertex-coloured: no textures, no alpha, so they instance cheaply and cast clean shadows (docs/features/3d-models.md).
Trees carry smooth normals; the turbine none, so the loader shades it flat. Geometry is built Z-up in
metres and written Y-up as glTF requires.

    python3 gen_models.py [outdir]        # writes *.glb next to this script by default
    python3 gen_models.py --preview out.png
    python3 gen_models.py --viewer out.html   # interactive WebGL2 page, the GLBs embedded
"""

import json
import math
import os
import struct
import sys

import numpy as np


def unit(v):
    v = np.asarray(v, float)
    return v / np.linalg.norm(v)


class Mesh:
    """Indexed positions, vertex colours and, when every vertex has one, normals."""

    def __init__(self):
        self.pos = []
        self.col = []
        self.nrm = []
        self.idx = []

    def vert(self, p, color, n=None):
        self.pos.append(np.asarray(p, float))
        self.col.append(color)
        self.nrm.append(None if n is None else unit(n))
        return len(self.pos) - 1

    def itri(self, i, j, k):
        if np.linalg.norm(np.cross(self.pos[j] - self.pos[i], self.pos[k] - self.pos[i])) > 1e-9:
            self.idx += [i, j, k]

    def tri(self, a, b, c, color):
        self.itri(self.vert(a, color), self.vert(b, color), self.vert(c, color))

    def quad(self, a, b, c, d, color):
        self.tri(a, b, c, color)
        self.tri(a, c, d, color)

    def smooth(self):
        return bool(self.nrm) and all(n is not None for n in self.nrm)

    def extend(self, other):
        base = len(self.pos)
        self.pos += other.pos
        self.col += other.col
        self.nrm += other.nrm
        self.idx += [i + base for i in other.idx]

    def transformed(self, rot, offset):
        out = Mesh()
        out.pos = [rot @ p + offset for p in self.pos]
        out.col = list(self.col)
        out.nrm = [None if n is None else rot @ n for n in self.nrm]
        out.idx = list(self.idx)
        return out

    def triangles(self):
        """De-indexed positions, normals (flat where missing) and colours, for the preview."""
        p = np.array(self.pos, np.float32)[self.idx]
        c = np.clip(np.array(self.col, np.float32)[self.idx], 0, 1)
        flat = np.cross(p[1::3] - p[0::3], p[2::3] - p[0::3])
        flat = np.repeat(flat / np.linalg.norm(flat, axis=1, keepdims=True), 3, axis=0)
        n = np.array([flat[k] if self.nrm[i] is None else self.nrm[i] for k, i in enumerate(self.idx)], np.float32)
        return p, n, c


def shade(rgb, k):
    return tuple(min(1.0, c * k) for c in rgb)


def frustum(mesh, n, r0, r1, z0, z1, color, cap=True):
    """Tapered cylinder along z, flat-shaded."""
    ring0 = [mesh.vert((r0 * math.cos(2 * math.pi * (i + 0.5) / n), r0 * math.sin(2 * math.pi * (i + 0.5) / n), z0), color)
             for i in range(n)]
    ring1 = [mesh.vert((r1 * math.cos(2 * math.pi * (i + 0.5) / n), r1 * math.sin(2 * math.pi * (i + 0.5) / n), z1), color)
             for i in range(n)]
    for i in range(n):
        k = (i + 1) % n
        mesh.itri(ring0[i], ring0[k], ring1[k])
        mesh.itri(ring0[i], ring1[k], ring1[i])
    if cap:
        top = mesh.vert((0, 0, z1), color)
        for i in range(n):
            mesh.itri(ring1[i], ring1[(i + 1) % n], top)


def icosphere(subdiv):
    t = (1 + 5 ** 0.5) / 2
    v = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
         (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    v = [np.array(p, float) / np.linalg.norm(p) for p in v]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
         (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11),
         (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(subdiv):
        cache, nf = {}, []

        def mid(a, b):
            key = (min(a, b), max(a, b))
            if key not in cache:
                m = v[a] + v[b]
                v.append(m / np.linalg.norm(m))
                cache[key] = len(v) - 1
            return cache[key]

        for a, b, c in f:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        f = nf
    return v, f


def basis(d):
    d = unit(d)
    a = np.array((1.0, 0, 0)) if abs(d[0]) < 0.9 else np.array((0, 1.0, 0))
    u = unit(np.cross(d, a))
    return u, np.cross(d, u)


def tube(mesh, p0, p1, r0, r1, n, color, rng):
    """Tapered open tube from p0 to p1, smooth radial normals, colour jittered per vertex."""
    p0, p1 = np.asarray(p0, float), np.asarray(p1, float)
    u, v = basis(p1 - p0)
    rings = []
    for p, r in ((p0, r0), (p1, r1)):
        ring = []
        for i in range(n):
            radial = math.cos(2 * math.pi * i / n) * u + math.sin(2 * math.pi * i / n) * v
            ring.append(mesh.vert(p + r * radial, shade(color, rng.uniform(0.92, 1.06)), radial))
        rings.append(ring)
    for i in range(n):
        k = (i + 1) % n
        mesh.itri(rings[0][i], rings[0][k], rings[1][k])
        mesh.itri(rings[0][i], rings[1][k], rings[1][i])


def crown_ao(p, crown_center, crown_radii):
    """Darker deep inside and under the crown, as a baked occlusion term."""
    rel = (np.asarray(p) - crown_center) / crown_radii
    return 0.8 + 0.16 * np.clip(np.linalg.norm(rel), 0, 1) + 0.08 * np.clip(rel[2], -1, 1)


def leaf(mesh, rng, base, along, length, width, color, droop=0.24, cup=0.1, pitch=0.0, roll=0.0):
    """One leaf from `base` along the (level) direction `along`, bending down under its own weight
    towards the tip and cupped at the edges; normals follow the bend. Round from above: an ellipse
    outline. One-sided, 12 triangles: the renderer draws models unculled and lights a back face
    from its flipped normal, so a second face would only double the cost."""
    flat = unit(np.array((along[0], along[1], 0.0)))
    side = np.cross(UP, flat)
    up = unit(UP + side * roll - flat * pitch)
    u = unit(flat + UP * pitch)
    v = unit(np.cross(up, u))
    base = np.asarray(base, float)

    def point(t, side):
        half = width * 0.5 * math.sqrt(max(0.0, 1 - (2 * t - 1) ** 2))
        y = side * half
        z = -droop * length * t * t - cup * length * side * side * (half / (width * 0.5))
        normal = unit(up + u * (2 * droop * t) + v * (2 * cup * side))
        return base + u * (t * length) + v * y + up * z, normal

    ts = (0.18, 0.5, 0.82)
    rows = [[point(0.0, 0)]] + [[point(t, s) for s in (-1, 0, 1)] for t in ts] + [[point(1.0, 0)]]
    ids = [[mesh.vert(p, color, n) for p, n in row] for row in rows]
    b0, t = ids[0][0], ids[-1][0]
    (l, m, r), (lt, mt, rt) = ids[1], ids[-2]
    tris = [(b0, m, l), (b0, r, m), (lt, mt, t), (mt, rt, t)]
    for (l1, m1, r1), (l2, m2, r2) in zip(ids[1:-2], ids[2:-1]):
        tris += [(l1, m1, m2), (l1, m2, l2), (m1, r1, r2), (m1, r2, m2)]
    for i, j, k in tris:
        mesh.itri(i, k, j)


def blob(segments=8):
    """A low sphere: poles plus two rings turned half a segment from each other, 4 x segments
    triangles. From above it reads round at an eighth of icosphere(1)'s vertex count."""
    verts = [np.array((0, 0, 1.0))]
    for ring, (z, turn) in enumerate(((0.5, 0.0), (-0.5, 0.5))):
        r = math.sqrt(1 - z * z)
        for i in range(segments):
            a = 2 * math.pi * (i + turn) / segments
            verts.append(np.array((r * math.cos(a), r * math.sin(a), z)))
    verts.append(np.array((0, 0, -1.0)))
    top, bottom, upper, lower = 0, len(verts) - 1, 1, 1 + segments
    faces = []
    for i in range(segments):
        j = (i + 1) % segments
        faces.append((top, upper + i, upper + j))
        faces.append((upper + i, lower + i, upper + j))
        faces.append((upper + j, lower + i, lower + j))
        faces.append((bottom, lower + j, lower + i))
    return verts, faces


def clump(mesh, rng, center, radius, color):
    """A small lumpy cluster of leaves, normals from its own faces so it shades softly. 32 triangles."""
    verts, faces = blob()
    center = np.asarray(center, float)
    pts = [center + p * radius * rng.uniform(0.8, 1.15) * np.array((1, 1, 0.75)) for p in verts]
    normals = [np.zeros(3) for _ in pts]
    for a, b, c in faces:
        nf = np.cross(pts[b] - pts[a], pts[c] - pts[a])
        for i in (a, b, c):
            normals[i] += nf
    ids = [mesh.vert(p, shade(color, (0.9 + 0.12 * d[2]) * rng.uniform(0.95, 1.05)), n) for p, n, d in zip(pts, normals, verts)]
    for a, b, c in faces:
        mesh.itri(ids[a], ids[b], ids[c])


BARK = (0.52, 0.46, 0.39)
SPREADING_GREEN = (0.60, 0.82, 0.59)
UP = np.array((0, 0, 1.0))


def skeleton(rng, m, trunk_height, trunk_radius, limbs, depth, spread, rise, crown_center, crown_radii):
    """Trunk, `limbs` limbs, then two children per branch down to `depth`, kept inside an ellipsoid
    crown. Branches spread more and climb less the further out they are, which makes the crown a wide
    dome. Returns the twig tips with their directions."""
    crown_center = np.asarray(crown_center, float)
    radii = np.asarray(crown_radii, float)
    top = np.array((rng.uniform(-0.2, 0.2), rng.uniform(-0.2, 0.2), trunk_height))
    mid = top * 0.5 + np.array((rng.uniform(-0.1, 0.1), rng.uniform(-0.1, 0.1), 0))
    tube(m, (0, 0, 0), mid, trunk_radius * 1.15, trunk_radius * 0.92, 8, BARK, rng)
    tube(m, mid, top, trunk_radius * 0.92, trunk_radius * 0.72, 8, BARK, rng)
    tips = []

    def inside(p):
        rel = (p - crown_center) / radii
        r = np.linalg.norm(rel)
        return p if r <= 1.0 else crown_center + (p - crown_center) / r

    def branch(p0, d, length, radius, level):
        p1 = inside(p0 + unit(d) * length)
        if np.linalg.norm(p1 - p0) < 0.3:
            return
        tube(m, p0, p1, radius, radius * 0.65, 6 if level == 1 else (4 if level == 2 else 3), BARK, rng)
        if level >= depth:
            tips.append((p1, unit(p1 - p0)))
            return
        f = level / depth
        u, v = basis(d)
        phase = rng.uniform(0, 2 * math.pi)
        for i in range(2):
            a = phase + math.pi * i + rng.uniform(-0.4, 0.4)
            side = math.cos(a) * u + math.sin(a) * v
            side[2] *= 0.4
            nd = unit(d) * (1 - spread * (0.5 + 0.5 * f)) + side * spread * (0.5 + 0.5 * f) + UP * rise * (1 - f)
            start = p0 + (p1 - p0) * rng.uniform(0.6, 0.95)
            branch(start, nd, length * rng.uniform(0.62, 0.78), radius * 0.62, level + 1)

    phase = rng.uniform(0, 2 * math.pi)
    for i in range(limbs):
        a = phase + 2 * math.pi * (i + rng.uniform(-0.15, 0.15)) / limbs
        d = np.array((math.cos(a), math.sin(a), 0)) * spread * 1.2 + UP
        branch(top * rng.uniform(0.8, 1.0), d, max(radii) * 0.62 * rng.uniform(0.9, 1.1), trunk_radius * 0.62, 1)
    return tips, crown_center, radii


def leaf_color(rng, color):
    """Each leaf drifts between a yellow and a blue cast, lighter or darker."""
    warm, cool = np.array((1.06, 1.03, 0.92)), np.array((0.92, 0.98, 1.08))
    return tuple(np.clip(np.array(color) * (warm + (cool - warm) * rng.random()) * rng.uniform(0.85, 1.12), 0, 1))


def spray(m, rng, tip, d, counts, size, color, crown_center, radii):
    """2-4 leaves at uneven angles around the twig's heading, all from the twig tip, each its own
    size, bend and tilt."""
    heading = math.atan2(d[1], d[0]) if abs(d[0]) + abs(d[1]) > 1e-3 else rng.uniform(0, 2 * math.pi)
    count = int(rng.integers(counts[0], counts[1] + 1))
    for i in range(count):
        a = heading + rng.uniform(-1.4, 1.4)
        along = np.array((math.cos(a), math.sin(a), 0.0))
        c = shade(leaf_color(rng, color), crown_ao(tip, crown_center, radii))
        length = size * rng.uniform(0.75, 1.25)
        leaf(m, rng, tip, along, length, length * rng.uniform(0.45, 0.62), c, droop=rng.uniform(0.14, 0.34),
             cup=rng.uniform(0.05, 0.15), pitch=rng.uniform(-0.1, 0.3), roll=rng.uniform(-0.25, 0.25))


def spreading_tree(seed=11):
    """Wide dome on a clear trunk (plane tree, oak), a few big level leaves per twig. ~8 m tall, ~10 m across."""
    rng = np.random.default_rng(seed)
    m = Mesh()
    tips, cc, radii = skeleton(rng, m, trunk_height=3.2, trunk_radius=0.3, limbs=3, depth=2, spread=0.85, rise=0.7,
                               crown_center=(0, 0, 6.2), crown_radii=(4.6, 4.6, 2.6))
    for tip, d in tips:
        spray(m, rng, tip, d, (2, 4), 3.4, SPREADING_GREEN, cc, radii)
        if rng.random() < 0.4:
            spray(m, rng, tip - d * rng.uniform(0.8, 1.5), d, (1, 1), 2.8, SPREADING_GREEN, cc, radii)
    return m


def layered_tree(seed=23):
    """Central leader with three tiers of level branches, leaves in layers at different heights. ~9 m."""
    rng = np.random.default_rng(seed)
    m = Mesh()
    lean = np.array((rng.uniform(-0.2, 0.2), rng.uniform(-0.2, 0.2), 0))
    top = np.array((0, 0, 8.6)) + lean
    tube(m, (0, 0, 0), top * 0.45, 0.27, 0.19, 7, BARK, rng)
    tube(m, top * 0.45, top, 0.19, 0.07, 6, BARK, rng)
    cc, radii = np.array((0, 0, 6.0)), np.array((4.2, 4.2, 3.6))
    color = (0.63, 0.84, 0.62)
    phase = rng.uniform(0, 2 * math.pi)
    for z, counts, length, size in ((3.8, (3, 4), 2.6, 3.4), (5.9, (3, 4), 2.0, 3.0), (7.7, (2, 3), 1.3, 2.6)):
        z += rng.uniform(-0.35, 0.35)
        attach = np.array((0, 0, z)) + lean * (z / 8.6)
        phase += rng.uniform(1.8, 3.0)
        count = int(rng.integers(counts[0], counts[1] + 1))
        for i in range(count):
            a = phase + 2 * math.pi * (i + rng.uniform(-0.2, 0.2)) / count
            d = unit(np.array((math.cos(a), math.sin(a), 0)) + UP * rng.uniform(0.15, 0.45))
            start = attach + UP * rng.uniform(-0.3, 0.3)
            tip = start + d * length * rng.uniform(0.75, 1.25)
            tube(m, start, tip, 0.11, 0.05, 4, BARK, rng)
            spray(m, rng, tip, d, (2, 3), size, color, cc, radii)
    spray(m, rng, top, np.array((1.0, 0, 0)), (2, 3), 2.4, color, cc, radii)
    return m


def clumped_tree(seed=37):
    """Open crown of leafy clumps, one per twig end (ash, locust). ~8 m tall, ~9 m across."""
    rng = np.random.default_rng(seed)
    m = Mesh()
    tips, cc, radii = skeleton(rng, m, trunk_height=2.9, trunk_radius=0.27, limbs=3, depth=2, spread=0.8, rise=0.9,
                               crown_center=(0, 0, 6.0), crown_radii=(4.4, 4.4, 2.8))
    color = (0.66, 0.86, 0.64)
    for tip, d in tips:
        p = tip + UP * 0.5
        clump(m, rng, p, rng.uniform(1.8, 2.1), shade(color, crown_ao(p, cc, radii) * rng.uniform(0.92, 1.08)))
    return m


HUB_HEIGHT = 81.4
HUB_FORWARD = 3.4


def turbine_tower():
    """Tower + nacelle, rotor faces +y. The rotor model is placed at (0, 0, HUB_HEIGHT)."""
    m = Mesh()
    white, grey = (0.93, 0.94, 0.95), (0.62, 0.64, 0.66)
    frustum(m, 6, 4.2, 4.0, 0.0, 0.8, grey, cap=True)
    z = 0.8
    rings = [(0.8, 2.15), (30.0, 1.85), (60.0, 1.55), (79.6, 1.30)]
    for (z0, r0), (z1, r1) in zip(rings, rings[1:]):
        frustum(m, 20, r0, r1, z0, z1, white, cap=False)
    frustum(m, 20, 1.30, 1.30, 79.6, 79.9, grey, cap=True)
    # nacelle: tapered box, front at y = 2.4, back at y = -8
    x0, x1 = 1.75, 1.35
    zf0, zf1 = HUB_HEIGHT - 1.8, HUB_HEIGHT + 1.8
    zb0, zb1 = HUB_HEIGHT - 1.4, HUB_HEIGHT + 1.5
    yf, yb = 2.4, -8.0
    F = [(-x0, yf, zf0), (x0, yf, zf0), (x0, yf, zf1), (-x0, yf, zf1)]
    B = [(-x1, yb, zb0), (x1, yb, zb0), (x1, yb, zb1), (-x1, yb, zb1)]
    m.quad(F[0], F[1], F[2], F[3], white)
    m.quad(B[1], B[0], B[3], B[2], white)
    m.quad(F[3], F[2], B[2], B[3], white)
    m.quad(B[0], B[1], F[1], F[0], grey)
    m.quad(F[1], B[1], B[2], F[2], white)
    m.quad(B[0], F[0], F[3], B[3], white)
    return m


def turbine_rotor():
    """Hub + 3 blades, origin on the spin axis (+y), geometry 2.4-6 m forward of the origin."""
    m = Mesh()
    white, red = (0.95, 0.95, 0.96), (0.80, 0.22, 0.18)
    y0 = 2.4
    n = 14
    ring = [(1.45 * math.cos(2 * math.pi * i / n), y0, 1.45 * math.sin(2 * math.pi * i / n)) for i in range(n)]
    mid = [(1.30 * math.cos(2 * math.pi * i / n), y0 + 1.6, 1.30 * math.sin(2 * math.pi * i / n)) for i in range(n)]
    tip = (0, y0 + 3.6, 0)
    for i in range(n):
        k = (i + 1) % n
        m.quad(ring[k], ring[i], mid[i], mid[k], white)
        m.tri(mid[k], mid[i], tip, white)
        m.tri(ring[i], ring[k], (0, y0, 0), white)
    yb = HUB_FORWARD
    length = 42.0
    stations = [0.03, 0.08, 0.2, 0.35, 0.5, 0.65, 0.8, 0.9, 1.0]
    for b in range(3):
        a = 2 * math.pi * b / 3 + math.pi / 2
        u = np.array((math.cos(a), 0, math.sin(a)))        # span
        w = np.array((-math.sin(a), 0, math.cos(a)))       # chord direction in the rotor plane
        yv = np.array((0, 1, 0))
        secs = []
        for s in stations:
            r = 1.0 + s * length
            chord = 3.4 * (1 - s) ** 0.8 + 0.45 if s > 0.08 else 1.2 + s * 18
            thick = 0.6 * (1 - s) + 0.08
            twist = math.radians(16 * (1 - s) + 2)
            cd = w * math.cos(twist) + yv * math.sin(twist)
            td = -w * math.sin(twist) + yv * math.cos(twist)
            c = u * r + np.array((0, yb, 0))
            secs.append([c + cd * 0.35 * chord, c + td * thick * 0.5, c - cd * 0.65 * chord, c - td * thick * 0.5])
        for i in range(len(secs) - 1):
            col = red if stations[i] >= 0.9 else white
            for j in range(4):
                k = (j + 1) % 4
                m.quad(secs[i][j], secs[i][k], secs[i + 1][k], secs[i + 1][j], col)
        e = secs[-1]
        m.quad(e[0], e[1], e[2], e[3], red)
    return m


MODELS = {
    'tree-spreading': spreading_tree,
    'tree-layered': layered_tree,
    'tree-clumped': clumped_tree,
    'wind-turbine-tower': turbine_tower,
    'wind-turbine-rotor': turbine_rotor,
}


def to_gltf_axes(v):
    return np.stack((v[:, 0], v[:, 2], -v[:, 1]), axis=1)


def write_glb(mesh, path, name):
    pos = to_gltf_axes(np.array(mesh.pos, np.float32)).astype(np.float32)
    # glTF vertex colours are linear; the colours here are display (sRGB) values
    srgb = np.clip(np.array(mesh.col, np.float32), 0, 1)
    linear = np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)
    rgba = np.concatenate((linear, np.ones((len(mesh.col), 1), np.float32)), axis=1)
    col8 = np.round(rgba * 255).astype(np.uint8)
    index_type = np.uint16 if len(pos) < 65536 else np.uint32
    idx = np.array(mesh.idx, dtype=index_type)
    arrays = [(pos, 34962), (col8, 34962), (idx, 34963)]
    if mesh.smooth():
        arrays.append((to_gltf_axes(np.array(mesh.nrm, np.float32)).astype(np.float32), 34962))

    chunks, views, offset = [], [], 0
    for arr, target in arrays:
        data = arr.tobytes()
        pad = (-len(data)) % 4
        views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(data), 'target': target})
        chunks.append(data + b'\0' * pad)
        offset += len(data) + pad
    binary = b''.join(chunks)

    gltf = {
        'asset': {'version': '2.0', 'generator': 'massif gen_models.py'},
        'scene': 0,
        'scenes': [{'nodes': [0]}],
        'nodes': [{'mesh': 0, 'name': name}],
        'meshes': [{'name': name, 'primitives': [{
            'attributes': dict({'POSITION': 0, 'COLOR_0': 1}, **({'NORMAL': 3} if mesh.smooth() else {})),
            'indices': 2, 'material': 0}]}],
        'materials': [{'name': name, 'pbrMetallicRoughness': {
            'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0, 'roughnessFactor': 1}}],
        'accessors': [
            {'bufferView': 0, 'componentType': 5126, 'count': len(pos), 'type': 'VEC3',
             'min': pos.min(axis=0).tolist(), 'max': pos.max(axis=0).tolist()},
            {'bufferView': 1, 'componentType': 5121, 'normalized': True, 'count': len(pos), 'type': 'VEC4'},
            {'bufferView': 2, 'componentType': 5123 if index_type == np.uint16 else 5125, 'count': len(idx),
             'type': 'SCALAR'},
        ] + ([{'bufferView': 3, 'componentType': 5126, 'count': len(pos), 'type': 'VEC3'}] if mesh.smooth() else []),
        'bufferViews': views,
        'buffers': [{'byteLength': len(binary)}],
    }
    js = json.dumps(gltf, separators=(',', ':')).encode()
    js += b' ' * ((-len(js)) % 4)
    total = 12 + 8 + len(js) + 8 + len(binary)
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, total))
        f.write(struct.pack('<II', len(js), 0x4E4F534A) + js)
        f.write(struct.pack('<II', len(binary), 0x004E4942) + binary)
    return len(idx) // 3, total


def render(meshes, out, size=360, elevations=(12, 50)):
    """Tiny z-buffered rasteriser for a contact sheet, one row per camera elevation; Lambert sun + ambient."""
    from PIL import Image
    sun = np.array((-0.5, -0.6, 0.62))
    sun /= np.linalg.norm(sun)
    sheet = Image.new('RGB', (size * len(meshes), size * len(elevations)), 'white')
    for row, elev in enumerate(elevations):
        for i, tile in enumerate(render_row(meshes, size, sun, math.radians(35), math.radians(elev))):
            sheet.paste(tile, (i * size, row * size))
    sheet.save(out)


def render_row(meshes, size, sun, az, el):
    from PIL import Image
    right = np.array((math.cos(az), -math.sin(az), 0))
    fwd = np.array((math.sin(az) * math.cos(el), math.cos(az) * math.cos(el), -math.sin(el)))
    up = np.cross(right, fwd)
    tiles = []
    for title, mesh in meshes:
        pos, nrm, col = mesh.triangles()
        sx, sy, depth = pos @ right, pos @ up, pos @ fwd
        span = max(sx.max() - sx.min(), sy.max() - sy.min()) * 1.1
        cx, cy = (sx.max() + sx.min()) / 2, (sy.max() + sy.min()) / 2
        px = (sx - cx) / span * size + size / 2
        py = size / 2 - (sy - cy) / span * size
        img = np.ones((size, size, 3), np.float32) * np.array((0.80, 0.86, 0.92))
        zb = np.full((size, size), np.inf)
        for t in range(0, len(pos), 3):
            x, y, z = px[t:t + 3], py[t:t + 3], depth[t:t + 3]
            x0, x1 = int(max(0, math.floor(x.min()))), int(min(size - 1, math.ceil(x.max())))
            y0, y1 = int(max(0, math.floor(y.min()))), int(min(size - 1, math.ceil(y.max())))
            if x0 > x1 or y0 > y1:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            d = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
            if abs(d) < 1e-9:
                continue
            l0 = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / d
            l1 = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / d
            l2 = 1 - l0 - l1
            inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            zz = l0 * z[0] + l1 * z[1] + l2 * z[2]
            sub = zb[y0:y1 + 1, x0:x1 + 1]
            mask = inside & (zz < sub)
            facing = np.cross(pos[t + 1] - pos[t], pos[t + 2] - pos[t]) @ fwd > 0
            lit = []
            for k in range(3):
                n = -nrm[t + k] if facing else nrm[t + k]
                lit.append(col[t + k] * (0.45 + 0.75 * max(0.0, float(n @ sun))))
            sub[mask] = zz[mask]
            shaded = l0[..., None] * lit[0] + l1[..., None] * lit[1] + l2[..., None] * lit[2]
            img[y0:y1 + 1, x0:x1 + 1][mask] = np.clip(shaded[mask], 0, 1)
        tiles.append(Image.fromarray((img * 255).astype(np.uint8)))
    return tiles


def main():
    args = sys.argv[1:]
    if args and args[0] == '--preview':
        a = math.radians(20)
        rot = np.array(((math.cos(a), 0, -math.sin(a)), (0, 1, 0), (math.sin(a), 0, math.cos(a))))
        rotor = turbine_rotor().transformed(rot, np.array((0, 0, HUB_HEIGHT)))
        turbine = turbine_tower()
        turbine.extend(rotor)
        render([('spreading', spreading_tree()), ('layered', layered_tree()), ('clumped', clumped_tree()),
                ('turbine', turbine)], args[1] if len(args) > 1 else 'models-preview.png')
        return
    here = os.path.dirname(os.path.abspath(__file__))
    if args and args[0] == '--viewer':
        import base64
        models = {}
        for name, build in MODELS.items():
            path = os.path.join('/tmp', name + '.glb')
            write_glb(build(), path, name)
            models[name] = base64.b64encode(open(path, 'rb').read()).decode()
        template = open(os.path.join(here, 'viewer.html')).read()
        open(args[1] if len(args) > 1 else 'models-viewer.html', 'w').write(template.replace('$MODELS$', json.dumps(models)))
        return
    outdir = args[0] if args else here
    for name, build in MODELS.items():
        tris, size = write_glb(build(), os.path.join(outdir, name + '.glb'), name)
        print('%-20s %5d tris %7d bytes' % (name, tris, size))


if __name__ == '__main__':
    main()
