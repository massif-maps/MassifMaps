---
title: Frame & threads
description: What runs on which thread, the order of a frame, and what triggers a redraw.
sidebar_position: 1
---

# The frame: threads, order, and what triggers a redraw

Scope: what happens between two `onDrawFrame` calls, on which thread, in which order.
For what is drawn inside the layer pass see [03-vt-renderer.md](03-vt-renderer.md).

## Threads

| Thread | Runs | Notes |
|---|---|---|
| **GL render thread** | `MapRenderer::onDrawFrame` and everything it calls | the only thread allowed to touch GL, except the two below |
| tile loading pool | `TileLayer::loadData` → data source fetch → decode → `vt::Tile` | `Options::setTileThreadPoolSize`, default **1** (tangram uses 2) |
| `CullWorker` | visible tile calculation per layer | `all/native/renderers/workers/CullWorker.cpp`, one per layer, debounced |
| `VTLabelPlacementWorker` | label placement for every vector layer | see [06-labels.mdx](06-labels.mdx) |
| `BillboardPlacementWorker` | billboard placement/visibility | kicked from `onDrawFrame` when `_billboardsChanged` |
| `TerrainDepthWorker` | terrain occlusion depth render + read-back | **its own EGL context, deliberately not shared** — see [04-terrain.md](04-terrain.md#occlusion-depth) |
| `ElevationTextureCache` encode worker | DEM → padded RGBA texture payload + `Bitmap` | GL thread only uploads |

Rule of thumb that has held for every perf round: **anything that allocates or frees megabytes, or
walks a whole tile, does not belong on the render thread.** The current hot list is in
[10-performance.md](10-performance.md).

## `MapRenderer::onDrawFrame` (all/native/renderers/MapRenderer.cpp:808)

In order:

1. **View state** — animation/kinetic handlers, camera clamp against the terrain height range, then
   `ViewState::calculateViewState` (projection, frustum, near/far — see
   [04-terrain.md](04-terrain.md#near-and-far-planes)).
2. **Optional offscreen bind** — only when a `PostProcessEffect` is set
   ([14-post-process.md](14-post-process.md)).
3. **Sky** — `SkyRenderer::onDrawFrame`; if it drew, the legacy sky band is skipped.
   `BackgroundRenderer` then draws the flat z=0 plane that fills the view past the terrain.
4. **`drawLayers`** — the whole map. Detailed below.
5. **Post-process** — the effect resolves, then any layer that opted out of it
   (`Layer::setPostProcessed(false)`) is drawn on top into the same depth buffer.
6. **Capture callbacks, billboard placement kick, idle notification.**

`PROF` timing sections (only in a `-PprofileRender` build) map onto this:
`sky` (which is mostly the swap-buffer wait, not work) `prelude` `prepare` `cover` `drape`
`layers` `layers3D` `billboards`.

## Inside `drawLayers` (MapRenderer.cpp:1755)

### a. Terrain prelude

When 3D terrain is on and the projection is planar:

- Every `TileLayer` is told its **stacking order** (`setTerrainRenderOrder`) and whether it is the
  **depth-writing** layer (the first visible, fully opaque tile layer).
- The **global terrain base fill** is drawn before all tile layers, so it shows through translucent
  layer content regardless of stacking order. With a depth-writing tile layer it is colour-only.
- The **cover** is computed: `collectTerrainCover` merges the terrain's own visible tiles
  (`TerrainRenderer::collectVisibleTiles`) with what the ground layers actually have, producing one
  shared list of tile ids, their **proxy depths**, and whether each is standing in for a finer tile.
- A cover leaf whose DEM has not arrived **walks up to the coarsest loaded ancestor** instead of
  being drawn flat. Without that, every tile on screen flashes in the bare ground colour during a
  zoom.
- Each layer is given `setTerrainGroundTiles`, an **ordinal base** (its slice of the depth order)
  and the stack's total **ordinal span** — see [05-depth-model.md](05-depth-model.md).
- Sun and shadow state are resolved **before** the ground draws, because each layer otherwise sets
  the sun from its own `onDrawFrame`, which runs later — the ground would light itself with the
  previous frame's sun.

### b. The ground

One draw pass over the shared cover, by the layer that *paints* it if any layer does (the hillshade
paint lives on that layer's renderer), otherwise by the first layer:
`groundDrawer->renderTerrainGround(groundColor)`. This is the only depth writer for the terrain
surface. Details in [04-terrain.md](04-terrain.md).

### c. The layers

Each layer's `onDrawFrame` in stack order, 2D pass first, then the 3D pass (`onDrawFrame3D`), then
billboards. For a vector tile layer this reaches `TileRenderer::onDrawFrame` →
`vt::GLTileRenderer` ([03-vt-renderer.md](03-vt-renderer.md)).

## What causes a frame

The renderer is **not** a free-running loop. A frame is drawn when something calls
`MapRenderer::requestRedraw()`: camera events, animations, a tile arriving, a label fade in
progress, a drape/ground cover change, an elevation version change. `MapRenderer::logRedrawSources`
exists to find out who is asking when the map will not settle.

**One requested frame is not enough to change what is on screen.** The surface is double-buffered
and `RENDERMODE_WHEN_DIRTY` draws exactly as many frames as were asked for, so a single frame lands
in the back buffer and the front one — the previous state — is what stays visible until something
else happens to draw again. `requestRedraw()` therefore owes a **follow-up frame**
(`_redrawExtraFrames`, taken at the end of `onDrawFrame` before the idle callback).

This was found on a state change with no camera movement behind it: toggling `FogOptions` from adb
(`DemoLive`, see the root `CLAUDE.md`), about one change in eight never appeared, permanently — a
second screenshot four seconds later was byte-identical — while a probe on the render thread logged
the correct fog for the frame it did draw. The misses correlated exactly with the changes that drew
**one** frame; every change that drew two appeared. The others only worked because a cull pass
happened to request a second redraw behind them, which is why the symptom looked intermittent and
partial ("only the sky updated"): different passes cache differently, so a stale front buffer shows
a mixture.

### An animation owes itself every frame, not just the first

`AnimationHandler` is stepped from `onDrawFrame` and, until 2026-08-23, **never requested a frame**
— unlike `KineticEventHandler`, which always has. It advanced only as far as something *else*
happened to redraw, and what happened to redraw was the cull pass behind `viewChanged`. So an
animation over a map with layers looked fine, and the same animation with no layers yet, or before
the first frame, stopped where it was.

A flight made it visible because its first frame does no moving: `setFlightTarget` defers the whole
path to `calculateFlight`, which sets it up against the view state the flight actually starts from
— not known until there is one — and emits progress 0. With no second frame the camera stayed
exactly where it started, so a camera pointed as a screen opened produced the default view (
measured on Android: `lon=0 lat=0 zoom=1.84 tilt=90` — null island, straight down) and looked like a
call that had never run.

Two fixes, both needed:

- `AnimationHandler::calculate` ends with `if (isAnimating()) requestRedraw()`.
- `BaseMapView::flyTo` requests the kick-off frame, the way `calculateCameraEvent` does for every
  other camera call.

**The rule for a flight before the first frame is therefore: it runs FROM that first frame.** It is
not dropped and it does not snap. `durationSeconds = 0` on `flyTo` still means *derive the duration
from the path*, not *immediate* — `BaseMapView::moveTo` is the immediate one, and it needs no frame
at all, which is what the facade's `camera.moveTo(…)` uses when no `animate(…)` was asked for.

### Order the camera against the clamp, not against the code

With `restrictedPanning` on, `ViewState::clampFocusPos` pushes the focus back until the **viewport**
is inside the pan bounds — so the same target is clamped hard at a world view and not at all up
close. Setting the focus and then the zoom therefore pins the focus to the middle of the bounds (the
equator, on an opening map) and the zoom that follows does not undo it; this is why a Matterhorn
camera opened over western France.

`BaseMapView::moveTo` applies **the zoom first when zooming in**, and last when zooming out, so the
pan is always judged at the tighter of the two zooms. Anything setting the camera by hand has the
same obligation — which is the reason to call `moveTo` instead.

Two more consequences worth knowing:

- **fps is meaningless when the map is idle** — the bench scripts drive a scripted pan for exactly
  this reason ([10-performance.md](10-performance.md)).
- A subsystem that requests a redraw every frame without changing anything is an endless render
  loop, and it will not look like a bug — it looks like battery drain. `TileRenderer` logs when it
  has been waiting many frames on a pending elevation rebuild for this reason.

## Camera animations

`AnimationHandler` (all/native/renderers/components/) runs one animation per camera property — pan,
rotation, tilt, zoom — each on its own clock, each moving a share of what is left every frame.

`flyTo` (`BaseMapView::flyTo`, `MapView.flyTo`) is the exception: **one** animation driving pan and
zoom (and optionally rotation and tilt) from a single clock, along Van Wijk & Nuij's optimal path
("Smooth and efficient zooming and panning", 2003). The zoom pulls back over a long move and comes
down at the target, so the whole path stays in view instead of the camera crossing the map at the
final zoom. `durationSeconds` 0 derives the duration from the length of the path — their point is
that a move twice as far should not take twice as long. It stops the per-property animations and
the kinetic handler when it starts, and they stay out of the way until it finishes (`isFlightActive`,
`stopFlight`). ρ is fixed at their 1.42.

The **viewpoint's height travels with the move**: the target `MapPos`'s Z is where it ends, and the
`climbHeight` overload adds a parabola on top of it — highest halfway, nothing at either end, a
plane's flight, which is also how the camera clears whatever stands between the two ends.

The platform `MapView`s are hand-written wrappers over `BaseMapView`, not generated, so each one has
to forward the flight API itself: `flyTo` (all three overloads), `stopFlight`, `isFlightActive` and
`getFlightProgress` are exposed on both `android/java/com/massifmaps/ui/MapView.java` and
`ios/objc/ui/MapView.{h,mm}`. A method missing from one of those two files is missing from that
platform's API however complete the C++ is — iOS had no flight API at all until the peak-finder
demo needed it.

An app animating its own state alongside the move reads `getFlightProgress()` (0..1 while flying,
-1 otherwise) rather than running a second clock beside it. The demo enters the peak-finder view
this way (`DemoMap.flyToPeakFinder`): 3D terrain and the mode switch on FIRST, so the terrain, the
relief surface and the names load and fade in during the flight, and the camera, the tilt and the
viewpoint's climb are all one animation.
