---
title: Globe mode
description: What spherical render projection shares with the planar one, the two scale traps between them, and why 3D terrain, shadows and the sky do not reach the globe yet.
sidebar_position: 18
---

# Globe mode

`Options.setRenderProjectionMode(RENDER_PROJECTION_MODE_SPHERICAL)` draws the map on a sphere
instead of the Mercator plane. It arrived with CARTO's `feature/globe` and has been carried,
unexercised, ever since: 2D tiled content, vector elements, the camera and the sky work; **3D
terrain and terrain shadows do not**.

This page is the shared conventions and the traps. What is missing and in what order it is being
fixed is at the bottom.

## Two hierarchies decide the shape of the world

| | plane | sphere | terrain, over either of them |
|---|---|---|---|
| `ProjectionSurface` — vector elements, camera, celestial | `PlanarProjectionSurface` | `SphericalProjectionSurface` | `TerrainProjectionSurface` |
| `vt::TileTransformer` — tiled content | `DefaultTileTransformer` | `SphericalTileTransformer` | `TerrainTileTransformer` |

Terrain **decorates** a base rather than replacing it: both terrain classes take the plane or the
globe and add elevation to it. So globe and terrain are no longer exclusive by construction — but
they are still exclusive by policy, because the displacement itself is planar. Terrain is refused
on a globe in `TileLayer::resetTileTransformer` and `VectorLayer::getElementProjectionSurface`, and
`MapRenderer` gates the flatten rule, the depth pre-pass, the camera clearance and the terrain
surface on `RENDER_PROJECTION_MODE_PLANAR`. Those five conditions go together, once the two
displacement sites below are surface-aware.

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
   plane and inlining planar tile math. The structural exclusion is gone; both call sites still
   refuse a spherical base by an explicit condition, because the displacement below is planar.

   Two things this turned up. The tile transformer barely displaces anything: since the GPU-draping
   commit, `calculateLocalHeight` returns 0 and tile geometry is built FLAT, so the composition
   there is a pure forward of point, normal, vector and metres-to-tile-local, and what terrain adds
   is only subdivision and a bbox grown by the elevation range. And `TerrainProjectionSurface` now
   takes the light `ElevationProvider` interface rather than `ElevationManager`, which is what lets
   `tests/api/TerrainSurfaceTest.cpp` drive it from a synthetic height field.
3. **The terrain surface itself.** `TerrainRenderer` builds a unit-square grid with a z
   displacement and an affine tile matrix, in four separate copies of the planar tile math; it has
   to consume the layer's `TileTransformer` instead. The GPU half is `applyTerrain` in
   `GLTileRendererShaders.h`, which ends `return vec3(pos.xy, z)` — it needs to displace along the
   surface normal, which covers the terrain surface, the drape, the skirts and the shadow casters.
4. **Picking and the camera.** `ElevationManager::intersectRay`, `CameraClearance` and
   `AutoFlatten::parallax` are all expressed along the Z axis.
5. **Space.** `Options::setZoomRange` clamps the minimum to `0`, so there is no zoom at which the
   whole planet is small on screen. Beyond the clamp: a planet-relative atmosphere (mapbox's globe
   atmosphere is a shell the ray is tested against, not a hemisphere overhead), fog that fades with
   altitude, back-face culling of the globe and a tile budget floor.

Spans, bridges and 3D extrusions are **not** in that list. They carry their own anchor and chord
machinery built around a flat frame ([3D bridges](17-bridges.md)) and will be wrong on the globe
until they are done separately.

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
