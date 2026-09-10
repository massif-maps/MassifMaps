---
title: Globe mode
description: What spherical render projection shares with the planar one, the two scale traps between them, and what still has to be seen on a device.
sidebar_position: 18
---

# Globe mode

`Options.setRenderProjectionMode(RENDER_PROJECTION_MODE_SPHERICAL)` draws the map on a sphere
instead of the Mercator plane. It arrived with CARTO's `feature/globe` and was carried unexercised
for years. 2D tiled content, vector elements, the camera and the sky reach it and look right on a
device. **3D terrain draws on it too** - the relief, its content, its lighting, its shadows and the
2D/3D switch, after the fixes below, and the same zoom frames the same ground on both. 3D buildings
match the plane with the terrain on or off, at the same height and in the same light.

This page is the shared conventions and the traps. What is missing is at the bottom.

## Two hierarchies decide the shape of the world

| | plane | sphere | terrain, over either of them |
|---|---|---|---|
| `ProjectionSurface` — vector elements, camera, celestial | `PlanarProjectionSurface` | `SphericalProjectionSurface` | `TerrainProjectionSurface` |
| `vt::TileTransformer` — tiled content | `DefaultTileTransformer` | `SphericalTileTransformer` | `TerrainTileTransformer` |

Terrain **decorates** a base rather than replacing it: both terrain classes take the plane or the
globe and add elevation to it, and every condition that used to refuse terrain on a globe is gone.
`RENDER_PROJECTION_MODE_PLANAR` no longer appears in `MapRenderer` or `VectorLayer` at all.

**Seen on emulator-5554** at Mont Blanc (z11 tilt 40, z13 tilt 60): 2D content, the sky, the limb,
the terrain relief, its fills and its contours. **A planar run of the same build is unchanged** at
the same camera - but the shipping map deserves a closer A/B than one frame.

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
   the shader's existing `cosh` equal 1. The scale itself does need a spherical case — it was
   assumed not to, and that was the flat globe below.

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
4. **Picking and the camera.** ~~The clearance and the focus~~ are done: both read a position
   through `ProjectionSurface::calculateMapPos` and put the focus back through
   `calculatePosition`, so they work on either surface, and `ViewState::worldPerInternal` bridges
   the ORBIT (a world distance) and a height (an internal one), which differ by 2 here.
   `ElevationManager::intersectRay` is still along the Z axis. `AutoFlatten::parallax` compares a
   height range against a camera distance, and those are internal and world units respectively, so
   the caller now converts — see the 2D/3D switch below.
   Vector ELEMENTS take the terrain surface here too now - `TerrainProjectionSurface` delegates
   every position to its base and only adds the height, so the one thing that stays planar is its
   `calculateHitPoint`, which marches the height field in the planar frame. Picking on terrain over
   a globe therefore falls back to the base surface: it hits the sphere, not the relief.

   The camera's ZOOM was wrong here in a way that made everything look wrong:
   `ViewState::calculateZoom0Distance` calibrated on `Const::WORLD_SIZE` whatever surface was
   active, so the globe's camera sat at half the distance its zoom meant - content a zoom level
   too large, and the camera inside the relief by zoom 12.
   `ProjectionSurface::getWorldWidth` answers per surface now. Two traps came with it: the width
   has to be read from the INCOMING surface in `calculateViewState` (the member is still the old
   one on the frame the projection changes, which made the globe jump a zoom on startup), and the
   demo's own zoom readout is derived from the camera distance, so it moves with the fix.
5. **Space.** `Options::setZoomRange` clamps the minimum to `0`, so there is no zoom at which the
   whole planet is small on screen. Beyond the clamp: a planet-relative atmosphere (mapbox's globe
   atmosphere is a shell the ray is tested against, not a hemisphere overhead), fog that fades with
   altitude, back-face culling of the globe and a tile budget floor.

Spans, bridges and 3D extrusions are **not** in that list. They carry their own anchor and chord
machinery built around a flat frame ([3D bridges](17-bridges.md)) and will be wrong on the globe
until they are done separately.

## Terrain on the globe was WRONG on a device - two bugs, both found by reading

Reported from emulator-5554 at Mont Blanc, zoom 13, tilted, with Mapbox Standard: the terrain
surface was **flat**, and **large tile-sized quads floated in the sky** at assorted angles. Both
causes were settled by reading the code rather than by logging, and both are fixed. **The device
check is still owed** — nothing below has been seen on a screen.

### The quads: the globe was drawing the flat shared grid as its ground

`TileRenderer` turns the tangram-style shared ground on whenever there is a terrain texture
provider, and `buildRegularGridSurface` builds ONE unit grid — `(u, 1 - v, 0)`, normal `(0, 0, 1)` —
reused for every tile through that tile's matrix. A spherical tile matrix only scales and
translates, so it cannot curve that square or orient it: each tile got a flat quad hung at its own
origin. That is the quads, exactly.

The GEOMETRY is now planar-only, through `GLTileRenderer::terrainGridSurfaces()`. The globe takes
the per-tile surfaces instead, which is the path the rest of the spherical work was already built
for: `SphericalTileTransformer` curves them, `TerrainTileTransformer` subdivides them for the relief
on top of that, and `aVertexSkirt` only exists on that path — `buildRegularGridSurface` passes no
skirts at all.

Only the geometry changes. `_terrainRegularGrid` itself stays on, because tangram's PAINTER DEPTH
MODEL hangs off the same flag — the first attempt turned the flag off in `TileRenderer` and put the
renderer in the adaptive depth configuration while `MapRenderer` was still driving the shared
ground. The lattice clamp, the edge stitching, ground shadow casting and the terrain paint follow
the geometry (all four read the grid mesh, and all four were already out of scope on the globe).

### The flatness: a metre is not the same length in every vertex frame

A spherical tile-local unit is `EARTH_RADIUS / 2 ^ zoom` — 778 m at zoom 13 — so 4000 m of relief is
about 5.1 of them. `SphericalVertexTransformer::calculateHeight` returns exactly that, in the tile's
OWN frame. `setupTerrainUniforms` then divided it by `frameScaleZ`, which is right for the planar
line beside it (its `metersToInternal` is in INTERNAL units and the division converts internal to
frame) and is a second conversion here: at zoom 13 it made 4000 m read as 0.13 units instead of 5.1.

Three frames reach that function, and the conversion is the ratio between the tile's own frame and
theirs — 1 for the grid path, the coordScale for tile geometry, and one tile-local unit (about 40 at
zoom 13) for the shared ground, whose vertices are internal coordinates. It is
`vt::sphericalMetersToFrame` now, in `TerrainElevationScale.h`, split out so
`tests/api/GlobeElevationScaleTest.cpp` can pin it: the test displaces a real vertex in each of the
three frames and checks the world point moves by one metre, and that one earth radius of height puts
it twice as far from the planet's centre. The old formula fails four of its checks.

### The lines: a tile clip that reads a curved xy as a unit square

With the ground fixed, every LINE was still missing on the globe - contours, roads, labels - while
the same camera with terrain off drew them all. `lineFsh` discards a fragment outside the target
tile, from `vTileUnit = pos.xy * uTileUnitScale + uTileUnitOffset`, and `uTileUnitScale` is non-zero
exactly when the tile HAS elevation. That affine form is the plane's: there tile-local xy IS the
unit square, on a sphere it is a curved position, so the test threw away nearly every fragment.

`vTileUnit` now comes from the same sphere inversion the DEM node uv uses -
`terrainSphereTileUnit` against `uTerrainSphereTileUV`, the tile's own Mercator extent in RADIANS,
antimeridian wrap included. `tests/api/GlobeElevationScaleTest.cpp` pins the corners, the centre, a
neighbour's vertex landing outside, and the last tile of a row.

The clip is worth keeping rather than switching off on a sphere: heights would now agree between
two tiles drawing the same road, but a semi-transparent line drawn twice still blends twice.

### The fills: the globe drapes now, through the same inversion

Fills came out SHREDDED - torn edges with the ground colour through them - because a fill is carried
at source density on purpose (tangram's model: not subdivided, the depth slack pays for the chord)
and on the globe it had no drape to be baked into. `MapRenderer` refused the RTT drape there because
the bake maps a tile's UNIT SQUARE onto the bake target and a sphere vertex is a curved position, so
every tile baked blank.

That is the same problem the line clip had, and it takes the same answer: `drapeBakeClip` positions
a baked vertex by `terrainSphereTileUnit`, and the surface samples the result with the same unit
instead of its vertex xy. GEOMETRY only - backgrounds and rasters bake through
`buildCompiledFlatSurfaces`, a flat quad whose xy already IS the unit square. A spherical bake
therefore keeps its TERRAIN program (that is where the sphere helpers live) and takes the drape
matrix whole, without the coordScale the planar path folds in, and the geometry ortho is built from
the layer's TARGET tile, the one its sphere uv was uploaded for.

`drapeFills` is no longer forced off on the globe, in `MapRenderer` and in
`TileLayer::resetTileTransformer` alike - the two MUST agree or tiles stay tesselated for the other
mode.

### What the device says

emulator-5554, Mont Blanc, the local French tiles, z11 tilt 40: **the relief, the fills, the water
and the contours all land**, in perspective, with no shredding. A planar run of the same build is
unchanged.

Open, in what is visibly left:

- **the ground reads grey** where the plane has it near-white, so something in the drape's clear
  colour or the terrain lighting differs on the sphere;
- **no labels**. They are not draped - they are screen-space, anchored through the elevation - so
  this is its own path and its own bug.

Two further gaps seen at the same time, both already on the list rather than new: the camera sits
INSIDE the mountain at z13 (camera clearance is planar-only, step 4), and picking is still planar.

Two further gaps seen at the same time, both already on the list rather than new: the camera sits
INSIDE the mountain at z13 (camera clearance is planar-only, step 4), and there is no RTT drape on
the globe by design (`MapRenderer` forces it off - the bake maps a tile's unit square).

### Labels: sized in screen space, and in the globe's own world

Two faults, one after the other. The constant-on-screen-size rule and the pixel-grid snap were
gated behind `vt::ViewState::planarProjection`, so on the globe labels fell back to scaling with
the perspective divide - they grew and shrank as you zoomed. Both corrections are pure screen space
(view depth over focus depth; a snap in NDC), so nothing in them needs a flat world and the flag is
gone.

That exposed the second: a label's world size is `2^-zoom` scaled by vt's own `_scale`, which
`VTRenderer` sets to `Const::WORLD_SIZE`. The globe's world is twice as wide, so every label came
out half size. `vt::ViewState::zoomScale` now carries `ViewState::worldPerInternal()` at all five
construction sites, the culler's included - its envelopes have to match the glyphs that are drawn.

### The tile LOD measured a meaningless area

Tiles refined far too late on the globe: you had to be nearly on top of one. Tangram's rule
(`TileLayer::calculateVisibleTilesRecursive`) projects the four corners `(0,0)...(1,1)` through the
tile MATRIX and compares the enclosed screen area - but that matrix only scales and translates, so
on a sphere those points land off the surface entirely. The corners go through
`createTileVertexTransformer()->calculatePoint()` now, which is the same unit square on a plane.
The rule's other planar assumptions went with it: the LOD elevation is applied through
`calculateElevatedPos` (radial on a sphere), the incidence cosine measures against the tile's own
normal rather than the z axis, and the two view-distance limits convert metres through the
surface's own world width.

**This is the third fault of one family**, after the camera distance and the tile extents: a length
in WORLD units compared against a planar constant. Anything that feels off by exactly 2x, or by one
zoom level, on the globe is worth looking at with that in mind.

`TerrainRenderer::calculateVisibleTiles` was the fourth: its depth pre-pass mesh subdivides while
`tileW * 2^zoom < WORLD_SIZE * SQRT_2`, and `tileW` is a world length, so on the globe the mesh
stopped a level short of the plane's everywhere. It takes the surface's own world width now.

### The extrusions: drawn all along, and discarded by their own tile clip

3D buildings were missing on the globe while the same style drew them on the plane. They were being
submitted - the draw is there, at `blend 1.00`, with a sane height scale - and scaling every
extrusion 50x made one roof appear as a slab, which is what said the geometry existed and the pixels
did not. Three planar assumptions, all in the extrusion path:

- **The tile clip.** `polygon3DVsh` builds `vTilePos` from `aVertexPosition.xy * uUVScale`, and the
  fragment shader discards anything outside `[0,1)`. Same fault as the line clip above and the same
  fix: `terrainSphereTileUnit`, taken BEFORE the extrusion moves the vertex. On its own this is why
  nothing was visible.
- **The base.** `basePos = vec3(pos.xy, baseZ)` rebuilds a z-up vertex. On a sphere the base is an
  offset ALONG the surface normal from the undisplaced position, as `applyTerrain` itself does.
- **`uBaseScale`.** It converted an internal z to the vertex frame as `1 / frameScaleZ`, which is
  the plane's ratio; measured on the device, that is exactly half what the sphere needs. It goes
  through metres now (`sphericalMetersToFrame / metersToInternal`), the same route the DEM heights
  take, so a building's base rides the ground its walls stand on.

Verified at the Louvre, `zoom 17 tilt 60`: extrusions, streets and labels match the planar frame.

### Picking and panning fell through to sea level

`ElevationManager::intersectRay` marches the height field in the PLANAR frame, so on a globe base it
answers nothing and `TerrainProjectionSurface::calculateHitPoint` handed the question to the sphere
at height 0. Every gesture anchored on sea level rather than on the ground under the finger, which
is felt as the map sliding out from under a pan.

The fallback now bisects on the height above the terrain - positive at the camera, negative under a
slope - which needs nothing but the base surface and the height field, so it serves any base.
`tests/api/TerrainSurfaceTest.cpp` pins it: a ray straight down over a 300-unit plateau stops 600
WORLD units early, the 2x again.

### The same zoom framed 1/cos(latitude) more ground on the globe

Two lengths look alike and are not. A **world** length is uniformly twice the plane's on the globe —
that is the trap at the top of this page, and `calculateDistance` and the local frame both carry it.
An **internal** length is not: Mercator stretches its own coordinates by `1/cos(latitude)` and a
sphere has none of that, so one internal unit covers `2 cos(latitude)` of the globe's world and a
flat 2 only at the equator.

The camera's zoom is calibrated in internal units, on `getWorldWidth()` — the equator. So the globe's
camera sat `1/cos(latitude)` too far: **zoom 16 over Paris framed 1.52x the ground planar zoom 16
did**, and 1.44x at Zermatt. It also read as "buildings are taller on the globe", which it is not —
the extrusion geometry is 1:1 on both surfaces. To frame the same view you zoomed further in, and
`building-height-scale` ramps on the zoom NUMBER, so the buildings rose.

`ProjectionSurface::calculateLocalScale` is that per-position number (1 on a plane at every
latitude, `2 cos(latitude)` on a sphere), `ViewState::worldPerInternal` answers it at the FOCUS, and
the calibration follows it — mapbox-gl's globe model, matching Mercator at the centre latitude.
Because the distance a zoom means now moves with the focus, `calculateViewState` re-derives
`_zoom0Distance` and re-places the camera whenever it shifts by more than 0.01%; a plane returns the
same number every time and never enters that branch.

**The ramp.** Straight `cos(latitude)` would put a world view at the +-85 clamp eleven times too
close. The local scale therefore fades back to the equatorial one as the planet fills the frame,
measured as the ORBIT against the planet's own radius — full local within one radius, fully
equatorial past four. A zoom threshold would have been a screen height and a DPI in disguise.

Pinned by `tests/api/SphericalSurfaceTest.cpp`; device-checked at the Louvre, where globe zoom 16
now lands on the planar zoom-16 frame street for street, and at zoom 3 over latitude 65, where the
limb still frames the planet.

### A tile LOD that measured a quad with no area, so the globe never refined

Tangram's rule projects a tile's four corners and compares the enclosed screen area. On a sphere the
COARSE tiles' corners land on top of each other — the root tile's are all on the antimeridian — so
the area is exactly 0, the tile is never subdivided, and the recursion stops at zoom 0. The whole
map was then one zoom-0 tile.

Terrain hid it: `_terrainMinTileZoom` forces subdivision whatever the area says. Turn the terrain
off, or flatten it, and a globe map went blank — measured, `PROBE cull visible 1 (zoom 0..0)`
against the plane's `visible 8 (zoom 11..11)` at the same camera.

The rule now samples the patch on a 3x3 grid and sums its four cells when the transformer is
spherical. That is the same number on a plane, so `steps = 2` there keeps the old four-corner
arithmetic exactly.

### Labels sized by the camera's height above a plane that is not there

`Label::calculateTerrainScaleFactor` keeps a label the same size on screen by dividing the view
depth by the camera-to-focus distance. When `ViewState::focusDistance` is 0 it falls back to
`origin(2) / -viewDir(2)` — the camera's height above the z=0 PLANE. That is the right number on a
planar map and the camera's world z on a globe, which at Zermatt is thousands of kilometres: every
label came out at the 0.05 scale floor. They were placed, culled and drawn — 1989 of them in the
pass — and invisible.

The 0 came from `TileRenderer`'s `prepareViewState`, the state installed before the cross-layer
drape, which set `zoomScale` and `lightBrightness` but never `focusDistance`. It does now. Labels on
a planar map at a high tilt change size slightly with it, and that is the correction: the fallback
measured the height above SEA LEVEL, not the distance to a focus sitting on the terrain.

### Shadows: a light box that could not be fitted, and two passes that never drew

Two independent things had to change, and finding the second cost a build cycle because the first
one *looked* fixed.

**The fit.** `GLTileRenderer::calculateShadowViewProj` built an axis-aligned world RECTANGLE from
the drawn tiles' corners plus a world-Z slab, and took the sun as a world vector. None of that means
anything on a ball. The spherical path drops the rectangle entirely — it only ever trimmed a box
that comes from the view frustum's bounding sphere anyway, which is projection-free — rotates the
sun from the map's east/north/up into the world with the same view anchor `uLightingFrame` uses,
converts the height slab from internal to world units, and culls casters against
`TileTransformer::calculateTileBBox` grown radially by that slab. `texelMeters` came out at
21.50 m / 62.11 m on both surfaces at the same camera, which is the check that the sides are right:
the globe's box is twice as wide in world units and its metre is twice as long.

**The passes.** `renderShadowCasters` and `renderTerrainShadowMask` both opened with
`terrainGridSurfaces()`, which is false on a globe — the shared regular-grid VBO is planar-only.
So the caster pass drew nothing, the mask pass drew nothing, and the mask was left cleared to
white: every fragment fully lit, and the shadow strength had no effect on the picture at all. The
guard now asks for terrain alone; the mask goes through `renderTileSurfaceFill`, which already
picks between the shared grid and a globe's per-tile surfaces, and the caster ground pass gained
the same second branch. The grid's one-bind-for-every-tile fast path is untouched — it was written
against a real emulator cost and the globe cannot use it anyway.

Measured on emulator-5554, terrain-3d at Zermatt: shadow strength 0 vs 1 moves 19-40% of the ground
pixels on the globe, against 17-29% for the planar control. Before the second fix it moved 0.0%.

**The caster cull, a third time.** "Grown radially by that slab" was written as an isotropic grow —
the tile's box pushed out by the slab on ALL THREE axes. A z16 tile is 20 world units wide at Paris
and the slab is 26, so one tile's box covered a dozen and the cull kept everything. It only showed
when the SUN MOVED, which is when the light box is refitted every frame: dragging `day-cycle-light`'s
hour slider on the globe took the caster set to 350 tiles against a 29-tile cover, the shadow pass to
200-1175 ms and the frame to 264-1648 ms, against 45-140 ms for the planar control. The map froze for
seconds at a time and jumped several hours of palette between frames, which is what "the ground
flashes to change colours" was. The offset is now along the tile's own radial — and against the
PATCH, not against an axis-aligned box of it: a tile at Paris is tilted 49°, so its world AABB is
far bigger than the tile, and bounding that kept 76-99 / 154-239 tiles per cascade where sampling
the patch itself (4 corners + the centre, which carries the bulge, each raised and lowered along its
own radial) keeps 59-60 / 90-92, against the planar control's 38 / 55. That is the plane's z-slab
hull exactly, one dimension over. Same drag afterwards: shadow pass 14-22 ms, frame 20-90 ms, every
frame different. Shadow
strength 0 vs 1 at the Louvre, hour 10, still moves 1.8-13.9% of the pixels against the planar
control's 0.8-4.9%, so nothing was culled away that used to cast.

**And a fourth: the caster pass TESSELATED inside the frame.** The cull fix alone did not settle it -
some drags still froze - and the counter said why: a caster tile is usually OFF SCREEN, so the ground
caster's `buildCompiledTileSurfaces` missed the surface cache and built one, `102` and `212` real
tesselations in a single pass at ~5 ms each. The plane never pays this because its ground caster is
the ONE shared regular-grid mesh drawn with each tile's MVP; a sphere's tile-local xy is a curved
position, so it cannot use a shared grid and needs a mesh per tile. New tesselations are now rationed
to `SHADOW_CASTER_SURFACE_BUDGET` per pass and the rest of the ring is drawn from what is already
cached, which fills in over the following frames - the same shape as the drape bake's budget. Same
drag again: caster pass 21-24 ms with 0 builds, every frame under 81 ms, no plateau anywhere in the
recording. Shadow strength 0 vs 1 still moves 2.4-5.0% against the planar control's 0.8-4.9%.

### The light was in the wrong frame, so every building was lit from the pole

A lighting shader — including an application's own — is written against the MAP's frame: `normal.z`
is "how much this face points up", `uSunDir.z` is the sun's height, and `applyLighting3D` uses both
apart from the `N.L` term. On a globe a geometry normal is the SPHERE's: at Paris a roof points at
`(0.43, 0.02, 0.75)`, not at `(0, 0, 1)`, and `LIGHTING_SHADER_3D` read that as a wall facing north.
Buildings came out dark and flat while the ground around them looked right.

The renderer now hands the shaders `uLightingFrame`, the rotation from the sphere's world into the
VIEW's own east/north/up, and every normal passes through it before it reaches a lighting function
(`lightingNormal` in the vertex stage, `groundLightNormal` for the DEM slope in the fragment one).
The sun uniform is untouched, which is what makes this the terminator model rather than a per-tile
copy of the planar picture: one fixed direction in space, matching the plane exactly at the focus
and falling off towards the limb. It re-anchors when the view moves — an app that wants a
terminator pinned to a date and a place has to say so, and there is no property for that yet.

The ground had a second bug in the same area: `setTerrainLightVaryings` built `vElevUV` with the
affine planar form `uElevationUV.xy + pos.xy * uElevationUV.zw`, the exact mistake the displacement
path already had a spherical inversion for. Shading and shadowing read the DEM at the wrong texels.
`uTerrainSphereElevUV` is that inversion for the FULL elevation texture, next to the node one.

### A globe with no terrain drew no buildings at all

`TERRAIN_SPHERICAL` used to be compiled in only alongside `TERRAIN_VTF_FLAG`, on the assumption that
nothing curves a vertex except the DEM displacement. Two things do. `polygon3DFsh` discards any
fragment outside `[0, 1)` of its target tile, and `polygon3DVsh` fed that clip
`aVertexPosition.xy` — which is a curved position on a sphere and not a tile coordinate, so with the
terrain off **every extrusion was discarded**: the map drew its roads, its water and its labels and
not one building. The same gate is why buildings on a terrain-less globe were lit in the sphere's
frame rather than the map's.

No new flag bit was needed — bit 31 already was the spherical one, it was just being withheld. It is
now set whenever the transformer is spherical, and the shader block splits in two: the geometry
helpers (`terrainSpherePoint`, `terrainSphereToMercator`, `terrainSphereTileUnit`, `drapeBakeClip`
and the three uniforms behind them) moved OUT of `#ifdef TERRAIN`, while everything that reads the
elevation texture — `aVertexSkirt`, the node/elev uv inversions — stayed inside it. On the C++ side
`setupSphericalUniforms` is the part of `setupTerrainUniforms` that a terrain-less draw also needs,
and `useProgram` uploads `uLightingFrame` on every bind, since a program that never touches the
terrain path still has to light its normals in the view's frame.

### Buildings stood 1/cos(latitude) too tall on a globe with terrain

The report was "buildings are taller in globe mode", and the measurements said the opposite: at the
Louvre, world units per metre of height matched world units per metre of ground on BOTH surfaces
(planar `0.0397833` against `32` world per tile-u at z15; spherical `0.0523307` against `42.0929`,
the same ratio), and the camera sat at 446.4 m on the plane against 446.6 m on the globe at the same
zoom and tilt. Geometry and camera were identical; only the picture was not.

The difference was the extrusion BASE. A building's base is resolved on the CPU
(`ElevationTextureCache::getDisplayHeight`) in internal z units, and those carry Mercator's own
`1/cos(latitude)` stretch. `uBaseScale` converts them back through metres, and the divisor it used —
`terrainTexture.metersToInternal` — is documented as the value *at the equator*: the planar shader
recovers the rest per vertex through `vElevCosh`, and the radial spherical path has no `vElevCosh`
to recover it with. So the base rose `1/cos(48.86°) = 1.52×` the ground under it while the wall's
bottom ring stayed on that ground, and the walls stretched — about +18 m at Paris, which reads as
"one storey became three". `uBaseScale` now divides by `metersToInternal * cosh(mercatorY)` at the
tile centre. Verified on emulator-5554, `day-cycle-light` at the Louvre, z17.2 tilt 45: globe and
plane now draw the same block at the same height.

### The 2D/3D switch was turned off on the globe, and the map went blank

`MapRenderer::updateTerrainFlatten` bailed out on any non-planar projection, because
`AutoFlatten::parallax` was believed to read double there. It reads HALF: the height range comes
from `ElevationManager::getDisplayHeightRange` in INTERNAL units and the camera distance is a WORLD
one, and only the second is doubled by the globe's world.

The bail-out took the whole switch with it, not just the rule — the seeding, the ratio ramp and
`setDecodeActive`. An app opening `flattened` in `TERRAIN_FLATTEN_MODE_FULL` therefore never got its
3D decode back: `terrain-2d-3d` on the globe showed the ground at something like a zoom-3 density
with an empty drape (`RTT drape EMPTY GROUND … blank 2, of 2 drawn`) and stayed there. The caller
now converts the height range with `ViewState::worldPerInternal` and the switch runs on both
surfaces.

Running it turned up a second fault, on either surface: the ramp is on a CLOCK, its first step has
no delta yet and so moves nothing, and `updateTerrainFlatten` returned without asking for a frame
whenever neither the ratio nor the decode had changed. On a map that only redraws on demand the
switch froze mid-`RAMPING` — asked for, never arriving. It now requests the redraw while ramping.

### The ground lattice was left to a curvature test that never fired

The plane draws every ground tile with one shared 64×64 grid. The globe cannot — a spherical tile
matrix will not curve a flat unit square — so it takes the per-tile surfaces, whose subdivision came
from `SphericalTileTransformer`'s `DEFAULT_DIVIDE_THRESHOLD`, `EARTH_CIRCUMFERENCE / 64` ≈ 626 km.
A zoom-14 tile is 2.4 km. **From zoom 6 up nothing was ever split**, and a ground tile was two
triangles. `TerrainTileTransformer` does subdivide on top of that, but only when the DEM tile is
already cached, only when its relief exceeds 1 mm, and not at all in area source-density mode — so
the mesh the plane has unconditionally was, on the globe, three gates deep.

`TileSurfaceBuilder::setGridResolution` lays the tile out on the plane's own lattice instead, at the
same `TerrainOptions::MeshResolution`, whenever the globe has terrain
(`GLTileRenderer::updateSurfaceGridResolution`). Positions are still built per tile on the CPU, in
double, relative to the render origin: a shared grid curved in the vertex shader is not available
here, because the sphere point is O(1) and fp32 cannot hold the tile inside it — the same limit the
section below is about. maplibre floors the equivalent number at 32 for the same reason
(`vertical_perspective_projection.ts`, *"visibly warped at high zooms"*).

This is a mesh-density fix, not the cause of the three artefacts below.

### The drape swam and every tile border broke, because fp32 cannot hold a sphere point

Three symptoms, reported together: the drape "shaky" and terraced, a visible break along every tile
border, and 3D buildings cut wrong at one. All three are one function.

`terrainSpherePoint` rebuilt the **absolute** unit-sphere coordinate — `uTerrainSphereOrigin + pos *
uTerrainSphereScale` — and `terrainSphereToMercator` inverted it. That is the tile clip, the drape
bake target, `vTilePos` in `polygon3DVsh` and both DEM samplings. But a tile is `2^-zoom` of the
sphere: at zoom 16 it spans 9.6e-5 of an O(1) coordinate whose fp32 ulp is 1.2e-7. **The tile-local
detail is below the ulp before the inversion runs.** Running the GLSL verbatim in float, worst error
in the recovered tile uv, measured as texels of a 1024-texel drape tile:

| zoom | 10 | 12 | 14 | 16 | 17 | 18 | 19 |
|---|---|---|---|---|---|---|---|
| absolute point | 0.02 | 0.15 | 0.46 | 2.38 | 2.37 | 8.83 | 17.76 |
| relative offset | 0.0002 | 0.0002 | 0.0002 | 0.0001 | 0.0002 | 0.0002 | 0.0003 |

`uTerrainSphereOrigin` is itself a `float` uniform, and its own ulp is a **per-tile** shift — 1.3
texels at z16, 5 at z18, different for each tile. That is the break at the border, exactly.

`terrainSphereMercatorDelta` returns the Mercator offset from the **vertex frame's own origin** and
never forms the absolute point: `atanh(a) - atanh(b) = atanh((a-b)/(1-ab))` with `a-b` expanded so
it comes out of the small displacement alone, `|o+d|-1` written as `(2 o·d + d·d)/(|o+d|+1)`, and
the longitude as `atan2` of the cross and dot of `o` with `o+d`, both expanded the same way. The
small-argument `atanh` takes its series: `log(1+x)` at `x = 1e-4` throws away four of the seven
digits. Every uv uniform (`uTerrainSphereTileUV`, `uTerrainSphereNodeUV`, `uTerrainSphereElevUV`)
now carries its origin **relative to the frame origin**, computed in double on the CPU
(`GLTileRenderer::sphereFrameMercator`), so no O(1) quantity reaches the shader at all — which is
also why none of them wraps the antimeridian any more; `wrapRadians` does it once, on the CPU.

The error is now flat across zoom, which is the signature worth checking if this ever regresses.
`testTheShaderInversionSurvivesFloatPrecision` pins both forms; the older
`testTheShaderInversionRecoversTheTileUV` runs in double and by construction could never see this.

`baseUp = normalize(terrainSpherePoint(pos))` is left alone: a direction needs no more than fp32.

### Every building read its base kilometres from where it stood

The one still standing after the fp32 fix: buildings stepped at tile borders, and the step grew as
the integer zoom dropped.

An extrusion is a rigid prism at ONE elevation, so `TileLayerBuilder` stores the point its base is
read at and `GLTileRenderer::resolveExtrusionBases` reads it back through `SpanResolver::tileMatrix2D`
— a **flat** tile matrix, tile unit square to normalised Mercator. What was stored was
`_transformer->calculatePoint(centroid)`, which on a plane is exactly `(u, 1 - v)` and on a globe is
a **curved position in the tile's own frame**. Measured at Paris, distance from the tile square:

| zoom | 14 | 16 | 17 |
|---|---|---|---|
| planar | 0 | 0 | 0 |
| spherical | 4.28 tiles (10.5 km) | 4.28 tiles (2.6 km) | 4.28 tiles (1.3 km) |

A constant offset in TILE units, so it is a different world position for every tile — two halves of
one building read two different hills — and the metres scale with the tile, which is why the gap
grew as the zoom fell. The anchor rule in `ExtrusionAnchors.h` was doing its job: both sides agreed
on the anchor, then both looked it up somewhere else.

Stored as the flip written out, `(u, 1 - v)`. On a plane that is bit-for-bit what `calculatePoint`
returned, so nothing planar moves; `calculateHeight` reads the same slot and its own `pos(1)` term
is unchanged too.

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

Any gallery example runs on the globe with `--es globe true` on `.ExampleActivity`, which is how the
sphere gets exercised against real content (`day-cycle-light` covers terrain, shadows and buildings
at once). The demo has a `globe` checkbox under **BASE MAP**, and `--es globe true` at launch. Terrain is on
there like anywhere else; `BenchActivity` overrides `--es zoom` and `--es tilt`, so drive the camera
by hand with `--es ui true` - and terrain's `minZoom` is 5, below which `0 ground draws` is correct
and says nothing.
