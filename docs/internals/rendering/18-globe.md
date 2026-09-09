---
title: Globe mode
description: What spherical render projection shares with the planar one, the two scale traps between them, and why 3D terrain, shadows and the sky do not reach the globe yet.
sidebar_position: 18
---

# Globe mode

`Options.setRenderProjectionMode(RENDER_PROJECTION_MODE_SPHERICAL)` draws the map on a sphere
instead of the Mercator plane. It arrived with CARTO's `feature/globe` and has been carried,
unexercised, ever since: 2D tiled content, vector elements and the camera work; **3D terrain,
terrain shadows and the sky's horizon do not**.

This page is the shared conventions and the traps. What is missing and in what order it is being
fixed is at the bottom.

## Two hierarchies decide the shape of the world

| | plane | sphere | terrain |
|---|---|---|---|
| `ProjectionSurface` — vector elements, camera, celestial | `PlanarProjectionSurface` | `SphericalProjectionSurface` | `TerrainProjectionSurface` |
| `vt::TileTransformer` — tiled content | `DefaultTileTransformer` | `SphericalTileTransformer` | `TerrainTileTransformer` |

In both, **terrain is a sibling of the plane rather than a decorator over it** —
`TerrainProjectionSurface` derives from `PlanarProjectionSurface`, and `TerrainTileTransformer`
inlines the planar tile math. Globe and terrain are therefore mutually exclusive by construction,
and the exclusion is enforced explicitly: `TileLayer::resetTileTransformer` picks the spherical
transformer over the terrain one, and `MapRenderer` gates the flatten rule, the depth pre-pass, the
camera clearance and the terrain surface on `RENDER_PROJECTION_MODE_PLANAR`.

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

1. **The sky's up vector.** `SkyRenderer`'s fragment shader reads `rayDir.z` as the elevation angle
   and takes the camera height from `cameraPos(2)` — both flat-earth. The sky is drawn with a
   global up on the globe, so the horizon, the star fade and the ground wedge are all wrong.
2. **Terrain as a decorator.** Composing `TerrainProjectionSurface` and `TerrainTileTransformer`
   over a *base* surface instead of over the plane. The vt vertex transformer already has
   `calculateHeight` implemented spherically, so the composed point is
   `base->calculatePoint(pos) + base->calculateNormal(pos) * height` — which reduces to today's
   planar output exactly.
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
- There is no globe render check anywhere. `tests/api/SphericalSurfaceTest.cpp` covers the surface
  arithmetic; that the globe *draws* is still an unverified device check.

## Trying it

The demo has a `globe` checkbox under **BASE MAP**, and `--es globe true` at launch. Terrain is
dropped while it is on, so the globe shows 2D content alone until step 2 above lands.
