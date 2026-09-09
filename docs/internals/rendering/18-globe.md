---
title: Globe mode
description: What spherical render projection shares with the planar one, the two scale traps between them, and what still has to be seen on a device.
sidebar_position: 18
---

# Globe mode

`Options.setRenderProjectionMode(RENDER_PROJECTION_MODE_SPHERICAL)` draws the map on a sphere
instead of the Mercator plane. It arrived with CARTO's `feature/globe` and was carried unexercised
for years. 2D tiled content, vector elements, the camera, the sky and now 3D terrain all reach it;
**terrain shadows, picking on terrain, and the camera rules over terrain do not**.

This page is the shared conventions and the traps. What is missing is at the bottom.

## Two hierarchies decide the shape of the world

| | plane | sphere | terrain, over either of them |
|---|---|---|---|
| `ProjectionSurface` — vector elements, camera, celestial | `PlanarProjectionSurface` | `SphericalProjectionSurface` | `TerrainProjectionSurface` |
| `vt::TileTransformer` — tiled content | `DefaultTileTransformer` | `SphericalTileTransformer` | `TerrainTileTransformer` |

Terrain **decorates** a base rather than replacing it: both terrain classes take the plane or the
globe and add elevation to it, and every condition that used to refuse terrain on a globe is gone.
`RENDER_PROJECTION_MODE_PLANAR` no longer appears in `MapRenderer` or `VectorLayer` at all.

**None of it has been seen on a device.** Everything below is host-tested arithmetic and a shader
that has never been compiled. The first device check is the real gate: 3D terrain at a globe camera,
plus a planar A/B to confirm none of this moved the shipping map.

## What the two surfaces share

Pinned by `tests/api/SphericalSurfaceTest.cpp`; these are what makes fixing the above tractable
rather than a rewrite.

**Internal coordinates are still Mercator.** `SphericalProjectionSurface::calculateMapPos` returns
the same `x = lon · WORLD_SIZE / 2pi`, `y = atanh(sin lat) · WORLD_SIZE / 2pi` that
`EPSG3857::toInternal` produces. Every `ElevationManager` lookup is keyed by internal x/y, so the
DEM grids, the tile pyramid and the prefetch queue all address the same data on the globe.

**Internal z is the same height in metres.** 1000 m of internal z lifts a point 1000 m above the
plane and 1000 m above the sphere's surface alike, at any latitude.

## The two traps

**The spherical world is twice the planar world's scale.** The sphere has radius
`WORLD_SIZE / pi`, so its equator is `2 * WORLD_SIZE` of world length where the planar map's is
`WORLD_SIZE`. `calculateDistance` between the same two points returns twice as much in spherical
mode. The factor is uniform — the local frame carries it too, so a length converted *through*
`calculateLocalFrameMatrix` agrees between the modes and a raw world-space number does not. Any
threshold, clearance shell or parallax expressed in raw world units is off by exactly 2 on the
globe.

**`calculateNormal` is not a unit vector off the surface.** It returns
`InternalToSpherical(mapPos)`, whose length is `1 + height`. `ViewState::getFocusPosNormal` hands
that straight to `SolidRenderer` and `BackgroundRenderer` as a light direction, so a camera focused
above sea level scales the lighting by its own altitude. The planar surface returns `(0, 0, 1)`
always, which is why it has never shown. The local frame's up column *is* normalized — prefer it.

**`calculateHitPoint` solves the infinite line**, so a ray aimed away from the planet still reports
a hit, at negative `t`. `ViewState::screenToWorld` checks `t`; a new globe consumer copied from the
planar caller will not, because below the horizon the plane is always ahead.

## Where the globe already differs on purpose

- `VectorTileLayer` adds two **pole tiles** (`vt::TileId(0, 0, ±1)`) in spherical mode, so the
  sphere is closed at the caps.
- `SphericalTileTransformer` subdivides tile geometry against a zoom-0 grid so a straight segment
  follows the curve; the planar transformer passes geometry through untouched.
- `BackgroundRenderer` and `SolidRenderer` carry their own spherical branches.
- `Options::calculatePanBounds` skips the Mercator latitude clamp — there is no top or bottom edge.

## What is missing

In the order it is being addressed. The sequence is chosen so each step is provable by the host
suite before the one that depends on it.

1. ~~**The sky's up vector.**~~ Done. The sky shader's model was always a local-observer one — the
   elevation angle, the star azimuth, the planet-relative atmosphere origin, and `u_sunDir`, which
   `LightOptions::getSunDirection` produces from an azimuth/altitude pair. Only the view ray
   arrived in world space, so `main()` now rotates it by `u_localFrame`
   (`renderers/utils/SkyFrame.h`) and everything downstream is unchanged. The frame is the identity
   on the plane, so planar output is bit-exact. `u_cameraHeight` reads the surface's own internal z
   for the same reason.
2. ~~**Terrain as a decorator.**~~ Done. `TerrainProjectionSurface` and `TerrainTileTransformer`
   now take a BASE surface / transformer and add elevation to it, instead of deriving from the
   plane and inlining planar tile math.

   Two things this turned up. The tile transformer barely displaces anything: since the GPU-draping
   commit, `calculateLocalHeight` returns 0 and tile geometry is built FLAT, so the composition
   there is a pure forward of point, normal, vector and metres-to-tile-local, and what terrain adds
   is only subdivision and a bbox grown by the elevation range. And `TerrainProjectionSurface` now
   takes the light `ElevationProvider` interface rather than `ElevationManager`, which is what lets
   `tests/api/TerrainSurfaceTest.cpp` drive it from a synthetic height field.
3. ~~**The terrain surface itself.**~~ Done.

   The GPU half is in, behind a `TERRAIN_SPHERICAL` define set centrally in `buildShaderProgram`
   so it is part of the program cache key. `applyTerrain` now displaces along the surface normal
   rather than along z, and recovers the DEM uv by inverting the sphere — `atan` for the longitude
   and `atanh` for the Mercator y — because tile-local xy is a curved position there and not the
   tile's unit square. Two things fall out of the arithmetic rather than being tuned: the frame-space
   displacement is exactly `SphericalVertexTransformer::calculateHeight`, so shader and CPU agree by
   construction; and a spherical height is radial, so setting `uElevationScale.y/z` to zero makes
   the shader's existing `cosh` equal 1 and the scale formula needs no spherical case at all.

   The CPU half is done too. `Options` now owns the BASE tile transformer alongside the projection
   surface it already owned, on the same lifecycle, so `TileLayer` and `TerrainRenderer` read one
   object instead of each deciding the projection for themselves. `TerrainRenderer` takes it and
   builds its mesh, tile matrix, tile bounding box and LOD centre through it, dropping its mesh
   cache when it changes. The plane is untouched by construction: the mesh keeps its literal
   `(x, y, localZ)` and its `(1 << zoom) / WORLD_SIZE` height factor, its bounding box is the same
   flat box the transformer reproduces, and only the spherical branches are new.

   Two things had to change shape rather than be ported. `ensureSurfaceAttribs` read a node's height
   back out of the vertex `z`, which on a sphere is a curved coordinate and not a height, so the
   mesh now keeps its node heights and rotates the slope normal into the local east/north/up frame.
   And **skirts** no longer fold their drop into the vertex `z` on a globe — see below.
4. **Picking and the camera.** `ElevationManager::intersectRay`, `CameraClearance` and
   `AutoFlatten::parallax` are all expressed along the Z axis.
5. **Space.** `Options::setZoomRange` clamps the minimum to `0`, so there is no zoom at which the
   whole planet is small on screen. Beyond the clamp: a planet-relative atmosphere (mapbox's globe
   atmosphere is a shell the ray is tested against, not a hemisphere overhead), fog that fades with
   altitude, back-face culling of the globe and a tile budget floor.

Spans, bridges and 3D extrusions are **not** in that list. They carry their own anchor and chord
machinery built around a flat frame ([3D bridges](17-bridges.md)) and will be wrong on the globe
until they are done separately.

## Two things worth knowing about the spherical shader path

**A skirt's drop is a globe-only vertex attribute.** On the plane it is still folded into the
vertex `z` as `-1000000 - drop`, which costs nothing; on a sphere that would destroy the curved
position the shader displaces from, so it travels as `aVertexSkirt` instead. `TileSurface`'s layout
was already data-driven — every attribute is present exactly when its array is non-empty — so the
planar vertex keeps its size, its offsets and its encoding, and the globe pays one float per vertex.
`TerrainRenderer`'s mesh has its own skirts and its own shader, so it needs no attribute: it hangs
them off the base surface point along its normal, which reduces to `(x, y, skirtZ)` on a plane.

**The lattice clamp is off on a globe.** It locates a surface cell from tile-local xy, which is the
one thing that stops meaning "position in the tile" there. Draped geometry then takes the plain
node sample, which is what the adaptive path already does.

## What could be better

- The 2× scale difference is a latent bug generator. It would be better expressed as an explicit
  "world units per metre" on `ProjectionSurface` than left for each caller to rediscover.
- `calculateNormal` returning a non-unit vector is a bug in all but name; the only reason it is
  documented here rather than fixed is that changing it moves the planar lighting too, and that
  needs a device A/B.
- There is no globe render check anywhere. `tests/api/SphericalSurfaceTest.cpp` and
  `tests/api/SkyFrameTest.cpp` cover the arithmetic; that the globe *draws* is still an unverified
  device check.
- `u_cameraHeight` omits the Mercator latitude scale — it is internal z times
  `EARTH_CIRCUMFERENCE / WORLD_SIZE` with no `cos(lat)`, so at latitude 60 the atmosphere marches
  from twice the real altitude. Both surfaces omit it alike, which is the only reason it is
  documented here rather than fixed: correcting it moves the planar sky and needs a device A/B.

## Trying it

The demo has a `globe` checkbox under **BASE MAP**, and `--es globe true` at launch. Terrain is
dropped while it is on, so the globe shows 2D content alone until step 2 above lands.
