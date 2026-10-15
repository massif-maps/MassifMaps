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
| tile loading pool | `TileLayer::loadData` → data source fetch → decode → `vt::Tile` | `Options::setTileThreadPoolSize`, default **2** (tangram uses 2); on a Mac or Mac Catalyst half the cores, 2 to 8. Workers are created at the lowest priority, which `ThreadUtils.mm` keeps out of the macOS background band |
| `CullWorker` | visible tile calculation per layer | `all/native/renderers/workers/CullWorker.cpp`, one per layer, debounced |
| `VTLabelPlacementWorker` | label placement for every vector layer | see [06-labels.mdx](06-labels.mdx) |
| `BillboardPlacementWorker` | billboard placement/visibility | kicked from `onDrawFrame` when `_billboardsChanged` |
| `TerrainDepthWorker` | terrain occlusion depth render + read-back | **its own EGL context, deliberately not shared** — see [04-terrain.md](04-terrain.md#occlusion-depth) |
| `ElevationTextureCache` encode worker | DEM → padded RGBA texture payload + `Bitmap` | GL thread only uploads |

Rule of thumb that has held for every perf round: **anything that allocates or frees megabytes, or
walks a whole tile, does not belong on the render thread.** The current hot list is in
[10-performance.md](10-performance.md).

## `MapRenderer::onDrawFrame` (all/native/renderers/MapRenderer.cpp:1301)

In order:

1. **View state** — animation/kinetic handlers, camera clamp against the terrain height range, then
   `ViewState::calculateViewState` (projection, frustum, near/far — see
   [04-terrain.md](04-terrain.md#near-and-far-planes)).
2. **Optional offscreen bind** — only when a `PostProcessEffect` is set
   ([14-post-process.md](14-post-process.md)).
3. **Sky** — `SkyRenderer::onDrawFrame`; if it drew, the legacy sky band is skipped.
   `BackgroundRenderer` then draws the flat z=0 plane that fills the view past the terrain - unless
   the bottom tile layer's opaque, fully blended tile backgrounds already paint all the flat ground in
   view (`TileLayer::coversGround`, `vt/GroundCover.h`; planar, no terrain, sky drawn or not in view).
4. **`drawLayers`** — the whole map. Detailed below.
5. **Post-process** — the effect resolves, then any layer that opted out of it
   (`Layer::setPostProcessed(false)`) is drawn on top into the same depth buffer.
6. **Capture callbacks, billboard placement kick, idle notification.**

`PROF` timing sections (only in a `-PprofileRender` build) map onto this:
`sky` (which is mostly the swap-buffer wait, not work) `prelude` `prepare` `cover` `drape`
`layers` `layers3D` `billboards`.

## Inside `drawLayers` (MapRenderer.cpp:2837)

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

Which pass a vector layer's own content lands in is two independent settings, `LabelRenderOrder`
and `BuildingRenderOrder` (`LAYER` = the 2D pass, `LAST` = the 3D one), and they do **not** default
to the same pass: labels to `LAYER`, buildings to `LAST`. Inside one pass the order is
2D geometry → flat labels → extrusions → billboard labels, which is the order those four want.

A **translucent extrusion** (a fill opacity below 1, per feature, as Mapbox Standard's
`building_opacity` sets it) is drawn twice: depth only, then colour with the depth pulled one
unit towards the camera, so exactly one fragment per pixel blends — the nearest, and the first
drawn among equals (`GLTileRenderer::renderGeometry3D`). Blended straight in, every wall showed
through every other and two coincident party walls fought for the pixel. mapbox does the same with
a depth prepass and a stencil that admits each pixel once; the stencil here belongs to the 2D
pass, and the depth offset gives the same result without it. A *layer* opacity below 1 already
took the overlay path (opaque in its own buffer, composited once) and is unchanged.

A **billboard label follows the later of the two** (`TileRenderer::drawsBillboardLabelsHere`).
It stands out of the map, so with the default pair — labels at `LAYER`, buildings at `LAST` — its
own layer's extrusions were drawn a whole pass after it and painted over every one: POI names cut
mid-word by a roof, and the icon beside them gone entirely. A flat label keeps the label order; it
lies on the ground and a building over it is correct.

The pass split is the one place where a layer's content leaves its own place in the stack, and this
puts one more thing in the later pass: a lower layer's billboards now draw over a higher layer's 2D
geometry, as its extrusions already did. An app that wants everything strictly inside the layer
sets `BuildingRenderOrder` to `LAYER` as well — the rule then puts both back in the 2D pass.

An app's own billboards (markers, popups) are drawn after every layer, over every tile layer's
labels. A layer with `Layer::setBillboardsUnderLabels(true)` has them drawn between the two passes
instead: over the layers' ground, under the labels and extrusions a tile layer renders `LAST`.

### The ghost pass: line-occlusion-opacity

A line cannot be seen through an extrusion by drawing it later: the extrusions are the last thing in
the 3D pass and overwrite it. A rule that sets `line-occlusion-opacity` (CartoCSS, mapnik
`occlusion-opacity` on a `line` symbolizer) is therefore drawn twice, and does not drape:

1. `LineSymbolizer` puts the value in `LineStyle::occlusionOpacity`. `TileLayerBuilder` copies it to
   `TileGeometry::StyleParameters::occlusionOpacity`; it is one value per geometry, so a different
   value splits the batch (as `elevationMode` does), and a fill never joins a ghosted batch.
   `TileLayer::hasOcclusionGeometry()` answers once, at decode, whether a layer has any.
2. `GLTileRenderer::isDrapeableGeometry` refuses such a line, so it is neither baked into the drape
   nor skipped by the 2D loop: it draws live in the 2D pass like any line of a `NoDrapeLayerFilter`
   layer (decal offset, line clearance, the layer's ordinal).
3. `TileRenderer::onDrawFrame3D` queues its renderer (`GLTileRenderer::queueOccludedGeometry`) after
   the layer's extrusions, and `MapRenderer` calls `GLTileRenderer::flushOccludedGeometry()` once after
   the last layer's 3D pass (and after the overlay layers'), which draws the queue in stack order. It draws only the flagged `LINE` geometries again with the state the 2D loop
   used, `glDepthFunc(GL_GREATER)`, depth writes off, and a polygon offset one slope unit and four
   depth units further forward than the decal's `(-2, -8)`, with alpha times the property. Under
   the shared ground its ordinal is the stack's TOP one, whatever the layer's own (`ghostLayerOffset`,
   `GhostDepth.h`; `MapRenderer` publishes the top beside each layer's base).

`GL_GREATER` passes where the depth buffer holds something *nearer than the line*: an extrusion
written by this or an earlier layer's 3D pass, or a ridge. Where the line is visible, the buffer
holds the line's own 2D depth (terrain) or the clear value (flat), the same fragment fails by the
extra offset, and the ghost does not double the line. Depth is not written, so billboards and
later layers are unaffected.

What decides how much is ghosted is the same ordinal model as the hiding
([05-depth-model.md](05-depth-model.md)): the ghost covers exactly the pixels where an extrusion
beat a line of the topmost layer. Every 2D pass has run before the first 3D pass, so the depth buffer
holds every layer's decals, each pulled forward by its ordinal. A ghost at its own layer's ordinal
passed against all LATER ones (the route's ghost over a maneuver arrow, an arrow's casing ghost over
its own fill) and tinted the visible line with its translucent colour; at the top ordinal nothing in
the stack is nearer, so only an extrusion or a ridge can pass it. The price is the pull of the
ordinals above the layer, a few clip-space steps for the navigation layers, which sit at the top.
Skipped: a layer with a `comp-op` (overlay buffer), an extrusion layer whose buildings went to the
overlay (layer opacity below 1: not in the main depth buffer), a span. Not honoured: the 2D pass's
tile stencil masks and the drape coverage mask. Not used: a stencil bit set by the
extrusion pass. The 2D pass's tile masks and the draw-once groups own the stencil bits, and a ridge
would need the ground to set it too.

**Where it is wanted: the extrusion's stamp.** The ghost's depth is the stack's TOP ordinal, while the
building was drawn at its own, lower one. Each ordinal is a flat 0.02 clip units, ~0.8 m of eye depth, so
the depth test only passes where the building is nearer than the line by the difference of the two pulls
(several metres for a navigation layer over a base map) while the building already HIDES the line where
it is nearer than the line's own pull. In that gap the line is gone with no ghost, and the gap is
crossed by height / cos(angle off the vertical): a style that flattens buildings near the top-down view
(`building-height-view-scale`) shows the ghost at some tilts and not at others. So an inline extrusion
now stamps stencil bit `0x40` wherever it passes the depth test (`renderGeometry3D`; it passed against
every decal, so it hides whatever line is there), `MapRenderer` clears the bit before the first layer's
3D pass (`clearBuildingStamp`), and the ghost pass sweeps twice: first the stamped pixels with the depth
test off, then the depth test as before for what no extrusion stamped (a ridge). The draw-once bit makes
a pixel belong to one sweep. An extrusion drawn into an overlay (translucent layer) does not stamp.

**One blend per pixel.** The ghosts are translucent, so drawing them in style order let each one
show through the next: a maneuver arrow's casing ghost (blue) under its fill ghost (white) tinted the
white inner blue, the head's casing the head's, and a round join or a tile edge blended twice. The
ghosts of the frame therefore run as ONE draw-once group ([03-vt-renderer.md](03-vt-renderer.md#translucent-layers-draw-once-groups-opt-in)):
a core and a rim pass over the WHOLE stack, the topmost renderer first and its top style layer first,
stencil bit `0x80` cleared once at the start of the flush (that bit only; the 2D passes clear it again
themselves). A pixel gets the colour of the topmost ghost that covers it, at the one faint opacity, so
the ghost reads as the visible line seen through glass: white inner, blue edge. Like every draw-once
group, a line with `line-border-*` draws in the rim pass only, and without an 8-bit stencil buffer the
ghosts keep style order, renderers bottom first.

**One draw call holds several styles.** The tile builder batches the lines of several rules into one
geometry (a style index per vertex) and the index buffer draws them in style order: a maneuver arrow's
case, fill, headcase and head, or a `back/` line and its main line. A draw-once pass stamps a pixel with the
FIRST fragment drawn, so ordering layers and geometries was not enough: the casing, first in the buffer,
won every pixel it covered, the fill's included, and the ghost came out a solid blue wedge (or, with a
`back/` rule also ghosted, only the white backing). The ghost pass draws such a batch once per style, the
topmost first, with the other styles' widths collapsed to zero in the width table (`_ghostStyle`).
A stand-in or fading tile that a live tile of the same style layer overlaps is not ghosted (its line is
hidden by depth in the 2D pass, and the ghost has no such test: a second, differently simplified copy of the
line showed beside the live one).

**After every layer, not inside each.** The ghost used to be drawn from each layer's own 3D pass. Then a
lower layer's ghost (the route) was drawn first and a higher one's (the arrow on it) over it, with nothing
shared between them, so behind a building the route's blue lay across the arrow's head whichever layer
was on top. The flush also sees the extrusions and stamps of every layer, not only those up to its own.
It draws over the 3D labels of the layers, which are faint lines under billboards anyway.

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

### What stops a frame: the animation that no longer moves

Between frames a still map owes nothing; the only thing that keeps asking is a layer's
`onDrawFrame` returning true. For a tile layer that is `GLTileRenderer::startFrame`, which says
"another frame" while a tile is still blending, a label still fading, a label was just anchored onto
terrain or a roof, or a spinning model was drawn. `MapRenderer::drawLayers` turns any true into `requestRedraw()`, and `requestRedraw()` owes
the follow-up frame described above — so a layer that keeps saying true never lets the map idle: the
iOS/Catalyst view is an `MGLKView` redrawn by `setNeedsDisplay` and nothing else (no display link, no
continuous mode), so the frame rate at rest is simply the display's refresh (about 113 fps observed on a
120 Hz ProMotion screen, 60 on a 60 Hz one) and every one of those frames comes from this chain. Measured
on the Catalyst app: `300 frames drawn, 300 asked for by a layer, layer mask 0x00000001`, for minutes, with
the camera still and nothing loading; mask bit 0 is the first layer, the base map.

A real fade moves a little every frame and ends. `vt/AnimationProgress.h` is the guard for the ones that
do not: `startFrame` hashes what its step moves (tile blends, label opacities, a spinning model's clock)
and keeps the last four hashes; **a frame whose state equals one of them has not progressed**, so it is
not asked for again (a stall asks nothing new, a flip between a few states is cut the same way). The
frame that notices is still drawn, so the pixels on screen are those of the state it stopped on. A step with `dt <= 0` is never judged, and the history is
dropped whenever a frame asks for nothing, so the next animation starts clean.

The 300-frame log line says which source asked, per tile-renderer frame (so up to 7 x 300 with seven
renderers):

```
MapRenderer: what kept the tile renderers asking, over 2100 of their frames - tile blend 0, label fade 0,
label anchor 2100, spinning model 0, stalled and not passed on 2090
```

A map that is idle logs neither this line nor the `frames drawn` one for as long as nothing draws. A
line with a non-zero `stalled` is the guard working: the source named on the same line is the bug to chase.
A non-zero `spinning model` is legitimate (a wind turbine's rotor, see
[19-models.md](19-models.md)) and is not capped yet — it redraws at the display rate.

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
`stopFlight`). ρ is fixed at their 1.42 and V at mapbox's 1.2 screenfuls per second.

The path itself is `FlightPath` (all/native/renderers/components/), free of the view state so
`tests/api/FlightPathTest.cpp` can run it against mapbox-gl's numbers.

### Three things their parameterisation is picky about

**One unit, and it is a SCREENFUL.** `w` is the visible span, `u` the ground distance, and `u/w`
decides how far the camera pulls back. `ViewState::getSpanPerZoom` is the VIEWPORT's span, mapbox's
`w0 = max(tr.width, tr.height)`. Until 6.1 it was `WORLD_SIZE * 2^-zoom` — one camera *tile* — so
every flight dived by a constant `log2(screenHeight / tilePixels)`, ~1.6 levels on a phone, and by
MORE on a bigger screen where it should be less.

**`u` is GROUND distance.** The viewpoint's height is its own track on the clock, as mapbox's
`_updateElevation(k)` is. Folded into `u`, a pure climb — same lon/lat, different elevation, which
is what the peak-finder switch asks for — flew an arc across a map it never crosses.

**`S == 0` means "nothing to say", not "done".** With neither a path nor a zoom change the closed
form degenerates and mapbox falls through to `easeTo`. `FlightPath::sample` interpolates on the
clock, so a tilt-only flight eases its position rather than snapping it on frame 1 (#179).

The clock is eased before anything reads it — one `k` for the path, rotation, tilt and climb, as in
mapbox's `frame(easing(t))`. The default is their `cubic-bezier(0.25, 0.1, 0.25, 1)`; `FlightEasing`
(all/native/ui/) names the CSS alternatives, `FLIGHT_EASING_LINEAR` being the constant speed the
paper prescribes and the SDK flew before 6.1. `getFlightProgress()` reports that eased `k`.

The **viewpoint's height travels with the move**: the target `MapPos`'s Z is where it ends, and the
`climbHeight` overload adds a parabola on top of it — highest halfway, nothing at either end, a
plane's flight, which is also how the camera clears whatever stands between the two ends.

The platform `MapView`s are hand-written wrappers over `BaseMapView`, not generated, so each one has
to forward the flight API itself: `flyTo` (all four overloads), `stopFlight`, `isFlightActive` and
`getFlightProgress` are exposed on both `android/java/com/massifmaps/ui/MapView.java` and
`ios/objc/ui/MapView.{h,mm}`. A method missing from one of those two files is missing from that
platform's API however complete the C++ is — iOS had no flight API at all until the peak-finder
demo needed it.

An app animating its own state alongside the move reads `getFlightProgress()` (0..1 while flying,
-1 otherwise) rather than running a second clock beside it. The demo enters the peak-finder view
this way (`DemoMap.flyToPeakFinder`): 3D terrain and the mode switch on FIRST, so the terrain, the
relief surface and the names load and fade in during the flight, and the camera, the tilt and the
viewpoint's climb are all one animation.
