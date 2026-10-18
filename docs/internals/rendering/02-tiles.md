---
title: Tiles & LOD
description: Which tiles are chosen, how they are fetched and decoded (MVT and MLT), and the caches in between.
sidebar_position: 2
---

# Tiles: which ones, how they load, what comes out

Scope: from the camera to a decoded `vt::Tile`. The GL side is [03-vt-renderer.md](03-vt-renderer.md).

## The pipeline

```
CullWorker (per layer, background)
  └─ TileLayer::calculateVisibleTiles          visible + label + preloading tile lists
       └─ TileLayer::buildFetchTiles           what is missing from the caches
            └─ tile loading pool: data source → decoder → vt::Tile
                 └─ TileRenderer / GLTileRenderer::setVisibleTiles  (render tiles, labels)
```

- `TileLayer::loadData` (all/native/layers/TileLayer.cpp:313) runs a cull state through the layer.
- `VectorTileLayer` decodes with mapnikvt + cartocss into a `vt::Tile`: a list of `vt::TileLayer`s,
  each with geometries (`TileGeometry`) and labels (`TileLabel`).
- `RasterTileLayer` decodes to a bitmap; `HillshadeRasterTileLayer` additionally builds a normal map
  (`vt::NormalMapBuilder`) — except in paint mode, where it has no tiles at all
  ([07-hillshade-contours.md](07-hillshade-contours.md)).

## Which tiles are visible (the LOD rule)

`TileLayer::calculateVisibleTilesRecursive` walks the quadtree from the root, and on a **plane a tile
splits while its edge, seen from its nearest point, spans more than `2 * tileSize * TileLODFactor`
pixels** (`DistanceTileLOD::nodeCritical`, `focal * edge / distance`): the level of a tile is a
function of its **distance from the eye**, so it does not change when the camera looks around, and
falls one level per doubling of the distance. Every planar layer, vector or raster, in every mode
uses it - [the zoom follows the distance](#the-zoom-follows-the-distance).

```cpp
// nearest point of the tile's box (DEM range included) to the eye, edge in world units
bool split = tileEdge * focalPixels / (nearestDistance * maxEdgePixels) > 1;   // maxEdge = 2 * tileSizePixels * lodFactor
```

`Options::TileLODFactor` scales it: 1 is tangram's rule, larger keeps tiles coarser everywhere at a
tilt - fewer tiles, fewer far labels, less far detail - and 0 turns the pixel test off, refining
everything to the camera zoom.

:::note The area rule is the globe's now
The sections down to "Free roam" describe tangram's **projected screen area** rule
(`View::getTileScreenArea`: four corners to screen space, shoelace area, split at
`(2 * tileSizePixels * lodFactor)^2`) with maplibre's two corrections on top. It decided every planar
tile until the distance rule replaced it, and **a globe still uses it** (`_distanceLod` is planar
only), with `TileLODMaxZoomLevelsOnScreen` and `TileLODTileCountRatio`, which planar maps now ignore.
The measurements are kept: they are why the area rule was shaped as it was, and what the distance
rule trades away. The reason it went is in [the zoom follows the distance](#the-zoom-follows-the-distance):
a tile's projected area depends on where it lands on the screen and on the incidence angle, so
turning the camera moved far tiles between levels and, with them, which features exist there.
:::

:::warning Our default is a level finer than every reference
`TileLODFactor` defaults to **0.5**, which asks for **one whole zoom level finer than tangram,
mapbox and maplibre alike**. It costs nothing in the near field, where the discrete zoom bound is
what stops refinement anyway - it costs where the AREA rule is the binding constraint, up to 4x the
tiles there, and at a grazing tilt that is most of the frame. It was never a measured decision.
`Options::setTileLODProfile` names the three densities so a platform picks one instead of
discovering this by tuning: `REFERENCE` (1.0), `MOBILE` (0.71, half a level), `DESKTOP` (0.5,
today's default). Not yet measured against fps on a device - see the PR.
:::

### maplibre's two numbers, on top of the area rule

The area rule and maplibre's `calculateTileZoom` decompose to the same thing (see
[bounding what the grazing angle may cost](#bounding-what-the-grazing-angle-may-cost)), so porting
theirs is not a new walk - it is two corrections to ours, both in `all/native/layers/TileLODRule.h`:

- `Options::TileLODMaxZoomLevelsOnScreen` (maplibre's `maxZoomLevelsOnScreen`, default **9.314**)
  sets the power `p` on `cos(incidence)`. The area already carries one power of it, so the walk
  applies `p - 1`. At maplibre's default `p = 1.000`, which is our area rule exactly - **adopting
  their formula changes nothing until the number is moved**. Higher decays faster toward the
  horizon and costs fewer tiles; lower keeps far ground finer.
- `Options::TileLODTileCountRatio` (`tileCountMaxMinRatio`, default **3**) caps how many more tiles
  a pitched view may ask for than a flat one, shedding levels uniformly across the frame when it
  would be exceeded.

**The cap is inert at the default, and that is not a bug.** At `p >= 1` the integrand `cos^(p-1)` is
at most 1, so a pitched view never counts as asking for more tiles than a flat one. The cap exists
to bound the bill of asking for a *gentler* far field (`p < 1`); it does not rescue a view that is
slow for another reason - for that, raise `TileLODMaxZoomLevelsOnScreen` or the profile.
`TileLODForeshorteningLimit` is gone: it bounded the same term from the other side with a number
that had no reference behind it.

**Lowering it is expensive, and most expensive at a LOW tilt.** Measured on a Crosscall with the
day-cycle example over Paris (z16.5, mapbox-standard, terrain and shadows on), `6.0` against the
default `9.314`: **0.48 fps against 4.30, 97 tiles a frame against 23, and 30.9 M indices against
6.5 M** — and the two frames are indistinguishable on screen. At tilt 65 the same change is worth
nothing (3.8 fps against 4.3): a tilted view coarsens its far field anyway, while at 20° the whole
screen is at near-constant scale, so a slow decay holds every one of those tiles at the camera's
own level.

:::danger The pitch gate does NOT port, and it was tried
Both references keep the whole frame at one zoom below ~60 degrees of pitch - maplibre's
`allowVariableZoom` (`78.5 - fov/2`, capped at 60), mapbox's `MIN_LOD_PITCH` (60). Adopting it made
the demo unusable at **tilt 30**, which is pitch 60 exactly: constant zoom means every tile refined
to the camera's zoom, and where they stop drawing we keep going to tangram's view distance. That is
the whole visible ground paved at full detail - buildings to the horizon and a map that does not
move.

It is affordable for them because **their `maxPitch` IS 60**: at the gate's threshold the horizon is
never on screen, so constant zoom covers a bounded patch. We allow a lower tilt than that and draw
much further. Do not re-add it without also bounding the view distance.
:::

Three bounds apply on top of all of it: the data source's `getMaxZoom()`, the camera's discrete zoom
(`viewState.getZoom() + bias + DISCRETE_ZOOM_LEVEL_BIAS`; on a plane the zoom of the tile's own distance instead, see below), and,
in terrain mode, `TerrainOptions::MaxTileZoomOffset`.

In the near field this is the same density as the distance rule it replaced (refinement stops when a
tile covers between one and two tile sizes on screen, which is what the discrete zoom bound gives
anyway). The difference is at a **grazing angle**, where a tile's screen area collapses with the
foreshortening while its distance barely grows: at tilt 10 the old rule kept refining the whole
horizon band, and measured at 45.187/5.719 z16.2 t10 the visible set went from ~66 tiles a frame to
~24, with the submitted index count down 4x. That band is also where every one of those tiles
contributed a full set of labels for a few pixels of screen.

### The style zoom is the camera's, not the tile's

The LOD decides which **tile** to draw. It must not decide which **rules** that tile draws with, and
for a long time it did: `TileReader::readTile` set the expression context's adjusted zoom from the
tile id, so a converted MapBox style's `#building[zoom >= 16]` — a *view*-zoom gate in the original,
mechanically turned into a *tile*-zoom filter by `mapbox2css` — stopped matching the moment the LOD
handed back a z13 tile under a z16 camera. One level of coarsening, and the far half of a tilted
view lost every building along a straight tile edge, with the whole region flipping at once as the
ring moved during a pan.

`TileLayer::calculateVisibleTiles` therefore publishes `_targetTileZoom`, the zoom the camera asked
for, and each `VectorTileLayer::FetchTask` snapshots
`calculateStyleTileZoom(tile.zoom, target, lift)` (`all/native/layers/TileStyleZoom.h`) for the
decoder to match rules at. **Which tiles are drawn does not change at all** — the area rule, the
levels and the tile count are identical; only the rules a coarse tile matches move. A tile at the
target zoom is unchanged too, so the near field is byte-identical.

`Options::TileStyleZoomLift` bounds how far above its own zoom a tile may be styled, and that bound
is the whole balance. Unbounded (the first attempt) a horizon tile is styled at the camera's zoom
and emits the entire near-field content — every label, every arrow, every building — over ground
tens of times wider; measured on the demo at Paris that was buildings visible to the horizon and a
visibly slower map. The default **2** covers the first two coarsening steps, which is where the
visible edge sits (foreshortening alone costs 1.0–1.5 levels at 75°–83° of incidence, so the first
ring is one to two levels down). Past it the lift is not partial - the tile styles at exactly its
own zoom, so the horizon band costs precisely what it did before the lift existed. `0` is the old
tile-zoom behaviour everywhere.

On a plane the target is not the camera's: each tile is styled at its own view zoom
([the zoom follows the distance](#the-zoom-follows-the-distance)).

Two consequences worth knowing:

- **The style zoom is part of a decoded tile's identity, and the cache key is not.** So a decoded
  tile carries the style zoom it was decoded at (`VectorTileLayer::TileInfo`), and `tileValid`
  compares it against what the current target would produce (`isStyleTileZoomCurrent`) — a stamp,
  not a clock. Only lifted tiles go stale: one at or above the target, or past the lift, is
  untouched, and at lift 0 nothing is. A stale tile keeps drawing as a substitute while its
  replacement decodes. mapbox instead keys tiles on `OverscaledTileID.overscaledZ` and keeps both
  versions cached. The target is clamped at `getMaxZoom()` (plus the overzoom levels in terrain
  mode), so above that it never moves.
- **Both cache-level versions of this failed.** #238 had `onTargetTileZoomChanged` call
  `invalidateTiles(false)` plus `clearTiles(true)`, which threw away every decoded tile and the whole
  preloading cache on each integer zoom crossing — including tiles the crossing could not stale. A
  zoom-out from 19 to 11 crosses eight, so the map re-decoded eight times and tiles arrived one by
  one where 5.x had them instantly (bisected to #238, fixed 2026-09-13; the hook is now empty).
  Invalidation alone was wrong too, because it cannot see a decode in flight: `timed_lru_cache::put`
  clears the entry's expiration, so a tile queued under the old target landed *after* the change
  looking fresh and was never refetched — a tile decoded for zoom 13 survived a zoom-out to 11 and
  kept drawing its `#contour[zoom>=12]` lines.
- **The style zoom is read when the tile decodes, not when it was queued (changed 2026-09-13).**
  `isStyleTileZoomCurrent` catches a task that landed under a moved target, but only *after* the
  decode is paid for, and a zoom-out makes that expensive: zooming out fast from 19, every z17 tile
  queued while the target was still 19 was styled at 19, emitting the whole z19 near field — house
  numbers included — over `4^2 = 16x` the ground, and was then binned as stale. Reported as "zoom
  out from 19 and I end up at 17 with house numbers in every tile on screen; they disappear after".
  `FetchTask::loadTile` therefore reads `calculateStyleTileZoom` itself, from the target the camera
  has when the decode actually starts, so the common case decodes once at the right zoom instead of
  twice. Zooming *in* is unaffected: the target runs ahead of the tiles the LOD has, which is the
  case the lift exists for.
- **So the crossing has a margin** (`TileLayer::TARGET_TILE_ZOOM_HYSTERESIS`, 0.15 of a level,
  `calculateTargetTileZoom`). Since we re-decode rather than keep both versions, a zoom that only
  *wobbles* across a boundary re-decodes the map for nothing — and in terrain mode the focus rides
  the ground, so the zoom moves whenever the elevation under it does. Measured at Zermatt, one 2D/3D
  switch at zoom 12.05 drifted to 11.95 and back and cost 10 re-decodes instead of 4, on a camera
  that never moved. Neither reference has this, because neither has to: they key the cache on the
  parse zoom and a crossing costs them a lookup, not a decode.
- **...but only while the view moves** (`settleTargetTileZoom`). Kept at rest, the margin made the
  level depend on the zoom the camera came from: the terrain-3d example opens at 11.5 and the launch
  camera then sets 12.05, so on the Crosscall the same camera settled on 8 z11 tiles or 16 z12 tiles
  depending on whether a cull fell in between (2 of 6 cold launches). A held level now asks for one
  more cull, and a cull of the same view takes the camera's own level: 8 of 8 settled on z12, twice
  after a held z11, within 0.3 s, with no cull after the camera rests. The Zermatt wobble is a new
  view every frame, so the margin still holds through it.
- **What the margin still buys** (2026-10-08). The Zermatt switch no longer wobbles: the view zoom
  stayed at 12.050 through three 2D/3D cycles, with 0 target changes and 0 re-decodes with or
  without #379. A gesture near a boundary still does. Measured on the Crosscall, terrain-3d at
  Innsbruck (tilt 25), warm cache, the zoom stepped 11.97 ↔ 12.03 by broadcast, 24 steps:

  | | margin 0.15 | margin 0 |
  |---|---|---|
  | steps ~0.1 s apart (never at rest): target changes | 0 | 18–24 |
  | same: tiles re-decoded | **0** | **71–93** (65–87 vector) |
  | steps 1 s apart (at rest between): tiles re-decoded | 291 | 295 |

  At rest both follow the camera, which is the point of the settle above; moving, the margin saves
  about one full re-decode of the view per boundary crossing.
- **This goes beyond both references, deliberately.** mapbox's `reparseOverscaled` only raises the
  parse zoom of a tile already at the source max zoom (`covering_tiles`: `overscaledZ` is used only
  when `it.zoom === maxZoom`); a coarsened tile is parsed at its own zoom, and its layer `minzoom`
  is then gated on that zoom in the worker (`worker_tile.ts`) as well as on the view zoom at render
  time (`painter.ts`). So mapbox drops a coarsened tile's `minzoom` layers exactly as we used to —
  it just does not get there, because its LOD is far gentler than ours at a grazing angle (see
  [bounding what the grazing angle may cost](#bounding-what-the-grazing-angle-may-cost)) and its
  styles gate 3D buildings below the levels its LOD reaches. Ours are converted MapBox styles whose
  gates sit *at* the camera zoom, so they need the camera's zoom fed in at decode.

Three terrain-specific details, each of which was a bug once:

- **The LOD area is taken at surface level, not from the elevation-expanded bbox.** Otherwise
  subdivision decisions change as elevation streams in, and the visible tile set (and with it tile
  and DEM fetching) churns forever. Measuring a mountain tile as if it lay at sea level puts it
  further away and makes it smaller, so it stays coarse exactly where the terrain is high and steep
  — blurred hillshade, a blunt depth occluder that ridges leak through, and a wide LOD spread that
  tears at tile borders. "Surface level" is the **midpoint of the tile's own elevation band**
  (`ElevationManager::getMinMaxDisplayHeightCached`, never loads), falling back to the elevation at
  the screen centre (`_lodElevation`) where the DEM is not decoded yet. See
  [the per-tile height](#the-lod-height-is-per-tile-not-per-frame) below for why this differs from
  both references.
- **Target tiles may exceed the data source's max zoom** in terrain mode
  (`_terrainOverzoomTargets`, fed by the existing overzoom machinery). A z12 DEM-derived hillshade
  under a z15 camera otherwise renders surfaces many times coarser than the base map's, and those
  blunted ridges are leaky depth occluders - content and vector elements show through near crests.
- **The view distance stops the recursion**, tested against the nearest point of the tile. It is
  tangram's too (`ViewState::calculateViewDistance`): `2 * cameraHeight / cos(pitch + fovy/2)`,
  capped at 127 tile widths (their `MAX_LOD` 6), scaled by `TerrainOptions::ViewDistanceFactor`
  (1 = their rule verbatim, 0 = as far as the visible ground goes). `cameraHeight` is the larger of
  the zoom-derived camera-to-focus distance and the camera's height above sea level: they are the
  same thing for tangram's camera, but a viewpoint standing on a 2600 m summit is high above the
  ground while its zoom says it is close to it, and the zoom-derived quantity alone ends the
  panorama a few kilometres out - the closer to the terrain, the less of it you see.
  **Above 1 the factor also deepens the far plane** (`calculateViewDistances`), because otherwise
  the extra tiles the walk fetches are drawn and then clipped, and raising the factor appears to do
  nothing. It costs depth precision — the depth model is calibrated on the far/near ratio (see
  [05-depth.md](05-depth-model.md)) — so it is an explicit trade, not the default. A style may pin an absolute
  distance in metres with `terrain-max-visible-distance`. Pair a short one with fog
  ([08-lighting-sky-fog.md](08-lighting-sky-fog.md)) or the ground simply ends.

### The LOD height is per-tile, not per-frame

Both references use **one** height for every tile in a frame. Tangram takes the terrain depth under
the screen centre (`View::getTileScreenArea`, "use elevation at center of screen"). Maplibre's
`coveringTiles` uses each tile's real min/max elevation AABB — but only for the **frustum test**:
its LOD distance is `Aabb.distanceX`/`distanceY`, purely horizontal, and the vertical term
(`distanceToTileZ`) is the camera-to-center distance, the same for every tile
(`src/geo/projection/covering_tiles.ts`). Neither refines by per-tile height.

One height per frame is wrong whenever the screen centre is not at the height of the tile being
judged, and at a low tilt that is most of the frame. Measured on the Crosscall at
45.2185/5.7346 z15.37 t29 looking north into the Chartreuse, the focus sits at **1389 m** while the
near valley is at ~250 m: every near tile was projected a kilometre too high.

The tile's own band midpoint is used instead — the midpoint, not the ceiling, because projecting the
whole quad at the band's top refines a valley tile for one high corner. Measured at the same camera,
3 interleaved pairs, `--es contour true --es hs true`, panning north:

| tile height | fps (median of 63 windows) | tiles/frame |
|---|---|---|
| screen centre (tangram) | 25.7 | 42.0 |
| band top | 21.8 | 57.0 |
| band midpoint | 25.8 | 38.8 |

The midpoint redistributes rather than adds: slopes refine, the near valley coarsens. Measured
effect on the visible set at 45.1852/5.7220 z15.71 t29, a coarse mid-field patch splitting:
`z10=3 z11=1 z12=4 z14=3 z15=7` → `z10=3 z12=4 z13=2 z14=4 z15=6`.

**What this does not fix: the flat footprint.** At that camera the accepted tiles' areas run
13k–83k px² against a 212k px² threshold — 3 to 16× under it — so the elevation term moves areas
without moving levels. The symptom is a mountain that stays coarse after you pan closer to it than a
tile that was already fine, while flat ground at the same distance behaves.

The cause is not the rule. Both candidate rules decompose identically:

```
level = const − log2(distance) + (p/2)·log2(cos θ)     θ = incidence angle of the view ray on the ground
```

with `p = 1` for the area rule, and `p = 2·((maxZoomLevelsOnScreen − 1) / log2(cos(H − fov)/cos H) − 1)`
for maplibre's `calculateTileZoom` — which is **1.000** at its defaults (`maxZoomLevelsOnScreen`
9.314, `maxMercatorHorizonAngle` 89.25°, fov 36.87°). Same rule. Swapping to it buys nothing here;
it only becomes gentler than ours above fov ~37° (p 0.83 at fov 50) and harsher below.

What is wrong is the **input**: the four corners are projected as a flat footprint quad, so a
mountain face standing toward the camera is given the incidence angle of flat ground in the same
place. At 85° flat incidence a 30° slope facing the camera really presents cos 55° / cos 85° ≈ 6.6×
the area — **1.4 levels**. Each corner is projected at its own DEM height now
(`getDisplayHeightCached` per corner, falling back to the tile mean where the DEM has not landed),
which recovers it and only where the ground is actually tilted toward the viewer. The blunt
alternative, `Options::TileLODFactor` 0.71, buys the same level everywhere and costs 2× the tiles.

**The floor stops following the camera past the DEM's limit.** `_terrainMinTileZoom` is
`cameraTileZoom - MaxTileZoomCoarsening`, and it overrides the LOD's own answer for every tile
however far away, so a ridge 20 km out went from z11 at camera z14 to z14 at camera z17 - three
levels better for zooming in without moving towards it. It is now also capped at
`ElevationManager::getDetailZoomLimit` (the source maximum plus the levels `clampTileZoom` drops for
an oversized grid). Past that a finer tile resamples the same heightfield, so the floor's purpose -
an accurate occluder silhouette, neighbouring layers in step - is already served, and distance is
left to decide. Below it nothing changes.

**The step that made it urgent.** One flat quad per tile is not just 1.4 levels of lost slope: the
quad floats at the tile's MEAN height, so a tile holding a summit sits at ~1100 m while the city
tile in front of it sits at 220 m. At a low tilt a quad whose height nears the camera's own altitude
is seen nearly edge-on and its area collapses, so that one tile drops several levels while its
neighbour is fine — "one good mountain tile, then a terrible one", with the step moving as the
camera descends and no way back except flying almost onto it. Per-corner heights remove the mean,
and with it the collapse.

At **tilt 90** none of this applies: the areas there (408k–464k px²) are above the threshold, so the
level is the `targetTileZoom` cap, `floor(cameraZoom)`, for every tile. Tangram short-circuits the
same case explicitly (`if (m_pitch == 0) return FLT_MAX`).

### Bounding what the grazing angle may cost

The area rule folds two independent things into one number. Write it as a level:

```
level = const − log2(distance) + ½·log2(cos θ)      θ = incidence of the view ray on the ground
```

The first term is what LOD is *for*. The second is foreshortening, and at a low tilt it dominates
everything: measured per accepted tile on the Crosscall at 45.1852/5.7220 z15.71 t29, **every tile in
the frame sits at 79°–89° of incidence** and loses **1.2 to 3.0 levels** to it — the near ones as much
as the horizon. That is why a mountain 10 km out stays coarse while ground at the same distance
under a steeper angle is refined, and why panning closer to it does so little: you are buying back
one level per halving of distance against a 2-level foreshortening debt.

`Options::TileLODForeshorteningLimit` — since removed, `TileLODMaxZoomLevelsOnScreen` replaced it
([above](#maplibres-two-numbers-on-top-of-the-area-rule)) — bounded the second term only, as a floor
on `cos θ` (`limit` levels ⇒ `cos θ ≥ 2^(−2·limit)`). The distance term was untouched, so genuinely
far ground stayed coarse — and it *must*, because beyond ~15 km those tiles would need `cos θ > 1`
to refine at all. `0` (its default) was the area rule exactly as tangram wrote it.

Measured at that camera, `TileLODFactor` 0.5 (the demo's value), threshold 212 337 px²:

| limit | visible set | n |
|---|---|---|
| 0 (off) | `z10=3 z12=4 z13=2 z14=4 z15=6` | 19 |
| 1.25 | `z10=2 z12=4 z13=2 z14=2 z15=9` | 19 |
| **1.0** | `z11=2 z12=3 z13=6 z14=2 z15=9` | 22 |
| 0.75 | same as 1.0 (saturated) | 22 |

At 1.0 the 46–66 km `z10` patches are gone, `z13` triples, and the ladder is continuous `z11`…`z15`
instead of jumping `z12`→`z10`. Cost while panning (3 interleaved pairs, 63 windows):
**40.0 → 50.4 tiles/frame, 26.7 → 24.6 fps** — +26% tiles for −8% frame rate.

Do not size this from a spreadsheet. Simulating the clamp per accepted tile predicted 19 → 40 tiles
at limit 1.0, **7× the real cost**: boosting a parent boosts its children by the same factor, so a
tile that splits produces children that stop one level down instead of cascading.

### The coarsening floor times the view distance

`TerrainOptions::MaxTileZoomCoarsening` floors how coarse a far tile may get (default: 3 levels
under the camera), so that far surfaces stay usable as depth occluders. It **overrides the
screen-area rule**, which would happily coarsen the horizon on its own — and that is fine until it
is multiplied by a long view distance. The cost of a long view is not the distance, it is the
distance paved in tiles no coarser than the floor.

Measured at the demo's default camera, a 170 km pinned view distance:

| coarsening floor | tiles fetched | deepest zoom |
|---|---|---|
| 3 | **550** (all z13) | z13 |
| 8 | 50 | z15 |

The 550-tile case is a map that loads for minutes and blinks one tile at a time as each arrives,
because every tile is fetched, decoded, draped and re-baked. Note the near field gets *finer* with
more coarsening allowed (z15 against z13): the budget goes where it is visible instead of paving
the horizon.

`TileLayer::calculateVisibleTiles` therefore **relaxes the floor rather than shortening the view**:
whatever the app configured, it allows enough coarsening for the covered ground to fit in
`TERRAIN_COVER_TILE_BUDGET` (256) tiles, and logs when it does. Two settings that each look
reasonable can be ruinous multiplied together, and an app has no way to see that coming.

### The zoom follows the distance

A tile's level has to be a function of **how far the tile is from the eye** and of nothing else about
the view: not the heading, not the pitch, not where on the screen it lands. Otherwise looking around
from one spot moves far tiles between levels, and with them the features that exist there: a tree
point only lives in a vector tile decoded from zoom 15, so far trees line a street in one look and
are gone in the next, though the eye never moved. Three things in the old cut depended on the look:

- **The projected area.** A tile's area is its width times its foreshortened depth, and it shrinks
  with the angle off the camera axis (cos^3, 35 % at the edge of a wide screen) as well as with the
  incidence on the ground. Turning the camera moved the same tile through the threshold. A flat
  synthetic sweep (eye fixed, heading every 30 degrees, pitch 90 down to 12 up) had **9 % of the ground
  points** judged by two looks land on different levels, and 1 % of them across the trees line (z14/z15).
  Looking along the ground it also falls with the cube of the distance (worked out on a plane, not read
  off a device frame: eye 300 m up, z17 inside ~1 km, z15 at 1.6 km, z13 at 3.2 km, z11 at 6.4 km), two
  levels per doubling, a hard arc where the ground steps from crisp to blurry.
- **The camera's discrete zoom**, the cap (`_targetTileZoom`, no tile finer), the floor (zoom minus
  `MaxTileZoomCoarsening`) and the pitch drop of `calculateTileLODRule`. Under a free roam camera
  (`FreeRoamMode` other than `OFF`) the zoom is the distance to a focus that moves with the look: ~14
  looking at the horizon from a summit, 20 and more looking down. With terrain a classic camera is no
  different: a gesture keeps the eye and slides the focus onto the ground along the view ray.
- **The style zoom** each vector tile is decoded at (below).

#### The rule

On a plane (every layer, vector or raster, with or without terrain, in every mode) a tile splits while
`edge * focal / nearestDistance > 2 * tileSizePixels * TileLODFactor * allowance`, up to the data's max
zoom (plus `MaxOverzoomLevel` with terrain). The edge is in world units, the distance runs from the eye
to the nearest point of the tile's box - DEM range included, so a summit tile is near - and the focal
length in pixels is the projection's.

The **allowance** is `(slant / vertical)^0.65`, from the slant distance to the vertical one of that nearest
point (at least 1, with the vertical floored at 1/32 of the slant: 3.2 levels at most). It is the area
rule's grazing slope, a pure function of the tile and the eye: a ground seen head on is judged by
pixels alone, one seen along the surface may span more, so the level falls ~1.65 per doubling of the
distance over the ground and one per doubling straight below the eye. The exponent and the floor are
**fitted, not derived** (`FAR_COARSENING`, `MIN_GRAZING`): the foreshortening of a tile's area gives 0.5,
but the area rule also lost the tile's depth and the screen angle, and had no floor at the horizon, and 0.5
with a floor of 1/16 asked for 1.7x its tiles at tilt 10. On the Crosscall (z16.3, tilt 10, the 2D bench
scene) 0.65 and 1/32 give the area rule's cut tile for tile, `z10:1 z12:2 z13:4 z14:1 z15:2 z16:6`; the
sweep in `DistanceTileLODTest::testTheTileCountIsTheOldRules` finds 0.9-1.1x its count from tilt 90 to 10.
0.6 held 21 tiles there against 16, 0.7 drops two far tiles of the Bastille scene a level (18 -> 12).
Free roam imagery has none (`farCoarsening` 0): its panorama is cut by pixels alone, as it always was.

The flat top-down map keeps its cut: at tilt 90 the nearest point and the projected area measure the same
thing and the allowance is 1; the sweeps in `DistanceTileLODTest` find the two rules within a level.
`TileLODFactor` 0 turns the test off and refines to the cap; free roam imagery has no cap, so it falls
back to 1 there.

Every bound on a level reads the **view zoom of the tile's own nearest distance** (`viewZoom`: the zoom a
top-down camera there would be at, `log2(zoom0Distance / distance)` plus the zoom offset and bias), and a
classic map has a second cap on top, **the level of a reference distance that no look moves**:

| | cap | floor (terrain) | style zoom |
|---|---|---|---|
| classic planar layer | `floor(view zoom)`, plus `MaxTileZoomOffset` if set, the data's max zoom (+ overzoom with terrain), and the **reference level** | `floor(view zoom) - MaxTileZoomCoarsening`, under the DEM's detail limit and the view distance's | the view zoom, floored and held between the tile's zoom and the lift |
| free roam imagery | the data's only | none | n/a (raster) |
| globe | the camera's zoom | as classic | the camera's target; **the area rule** |

The reference level is the camera's zoom as it was, with the one thing that moved it taken out: without
terrain the **camera distance** (the focus stays on the plane, so it is the zoom the app set); with terrain
the **eye's height over the ground under it**, the zoom a top-down look from the eye would have, which is the
most the camera's zoom reaches over every look from there (equal to it at tilt 90, 0.2 level over at tilt 60,
a level over at tilt 30). Without it the per-tile cap alone lets ground nearer than the focus refine past
the camera's zoom: at tilt 10 (horizon in view) z17 and z18 tiles at the bottom of the screen, 2.5x the
tiles; under terrain a ridge nearer than the focus took z16 where the camera's z15.8 gave z15, 1.9x. That
is the cost of the view-zoom cap, and the old cap was what paid for it: ground nearer than the focus was
never finer than the camera's zoom.

Why not the camera's zoom itself, even for a classic map with terrain: the camera's zoom is the distance to its focus, and
with terrain on a gesture keeps the eye and slides the focus along the view ray onto the ground
(`recalculateZoomAndCenter`), so the zoom follows where the view ray lands. Tilting or turning the view
from one spot moved the zoom, and with it the cap, the floor and the style zoom of every tile at once:
far trees (tiles styled at z16, data from z14) were there in one look and gone in the next, and the
drape of the far ground, baked at a fixed size per tile, went from crisp to pixelated with the level.
The eye's height does not move with the look (`DistanceTileLODTest`: no ground point changes level across
looks with the cap on), and moves with the ground under the eye when the app pans, as the zoom did. The ground
is read from the cached DEM: until it lands the height is the eye's altitude, a cap too coarse that the
re-cull on the DEM's arrival lifts.

#### The tile budget

The cut is read off the whole tree, not walked with a threshold. Every tile records the scale at which
it stops splitting (`critical`, which only falls down the tree), the walk keeps the tree down to the
rule (scale 1), and `DistanceTileLOD::fitScale` reads the finest scale whose visible leaves fit the
budget: the count only changes where some tile stops splitting, so the answer lands on one of those.
At scale 1 - the usual case - the cut is the rule itself and depends on nothing but the eye. Over
budget every tile coarsens together by `log2(scale)` levels (at most five), which is the one place the
cut depends on the look, as the count is taken over the visible tiles. So only free roam imagery takes
the cache-sized budget, which binds on every view (a drape cache of 48 entries is 33 tiles); every other
layer keeps `DistanceTileLOD::DEFAULT_BUDGET`, 112, as a ceiling its rule rarely reaches. The log line
`TileLayer: distance cut ... edge scale S` appears when a budget binds.

- **Free roam imagery**: the drape cache's budget: the cover is the visible tiles plus the stand-ins
  and the generation behind it, so a layer asks for 70 % of the cache's entries
  (`DistanceTileLOD::budget`, from `DrapeResolution` and `DrapeCacheSize`; 112 for an automatic
  resolution). 160 MB of 512 px drapes holds 160 entries and takes 112 tiles; `TerrainDrapeCache` caps
  its entries at 256 (the cover's own ceiling), so 320 MB takes 179.
- **Everything else**: 112, a ceiling and not a measurement. A vector layer's cache (512 MB) never binds,
  and a cache-sized budget put every classic terrain layer at scale 2 or more, moving with how many
  tiles the view held.

Mirrored on a flat plane (`DistanceTileLODTest`, 1000 px screen, TileLODFactor 0.5, z16, the view
distance of `ViewState::calculateViewDistance`), tilt counted from straight down:

| tilt | area rule | distance rule |
|---|---|---|
| 0 | 28 | 24 |
| 30 | 32 | 26 |
| 45 | 36 | 28 |
| 60 | 37 | 37 |
| 70 | 42 | 43 |
| 75 | 39 | 37 |

The cut as first shipped (no reference cap, exponent 0.5) held 1.2x the area rule's tiles at tilt 60,
1.4x at 70 and 1.7x at 80 (mean over zooms, screens and densities), and **the Crosscall measured it at
2.2x the tiles drawn** in the 2D scene (z16.3, tilt 10) and 1.7x in the Neron terrain scene
([performance-log](../performance-log.md#the-tile-cull-by-eye-distance-held-twice-the-tiles-2026-10-17)).

`TileLODFactor` is the knob (`MOBILE` 0.71, `REFERENCE` 1).

#### Vector layers: the style zoom of a tile

A converted MapBox style gates its layers on the VIEW zoom, so a tile is decoded at the zoom the view
would be at ([the style zoom is the camera's](#the-style-zoom-is-the-cameras-not-the-tiles), lifted by
at most `TileStyleZoomLift` over the tile's own). On a plane each tile is styled at **its own view zoom**
(`DistanceTileLOD::viewZoom`, `calculateViewStyleTileZoom`), as pure a function of the distance as the
level is: a far tile keeps the same rules whichever way the eye turns, a near one gets the rules of the
near field. The cut records each kept tile's view zoom (`_tileViewZoom`), which the decoder and the
staleness test read by tile id; a decoded tile goes stale when its view zoom leaves its style zoom by
more than `TARGET_TILE_ZOOM_HYSTERESIS`. A tile the cut does not hold (a stand-in) is styled at its own
zoom. The view zoom is **bounded by the reference level** (`DistanceTileLOD::styleZoom`: under `zoomCap + 1`):
a tile the cap holds at z17 is bigger than the ground its nearest point reads (zoom 19 at tilt 55, z17.6),
so styled at that zoom it carried the z18-z19 POI pins over its whole area, and the street names went
(Crosscall, z17.6 tilt 55: "Rue de Palanka" and "Rue Felix Viallet" gone, Burger King and BB Smash in; style
zoom 17-19 against the parent's 17; clamped to the cap the names came back. That the pins displaced the names
is likely, not shown). The data decides the rest: a z13 tile of a source that puts trees from z14 has none, however it is
styled.

Not a function of the cut, so still the camera's: any style FUNCTION of `view::zoom` evaluated per frame
(a model's `linear([view::zoom], ...)` opacity, a line width), and the zoom term of the drape bake.

#### The drape resolution

With a fixed `DrapeResolution` a drape tile is that many texels whatever its level, so its sharpness is
the level's. With the automatic one (0) the texels follow the leaf's on-screen size, which used to be
projected through the camera (the on-screen part of the leaf, so turning it resized every drape): it is
now `focal * width / nearestDistance`, a distance. The fit to the cache (`DrapeTuning::leafResolutions`)
still halves the largest first while over budget, which depends on the set of leaves in view.

#### Reading it on a device

At debug level the cut logs once a second the level over four fixed ground points (N, E, S, W) at the
distance where the view zoom reads 13 to 17, plus the budget scale, tile count, eye height and camera
zoom (`TileLayer: cut probes ...`). Looking around from one spot must change none of the `z` values (a
`-` is a point outside the view); the camera zoom is allowed to.

#### What is kept for free roam imagery

For a raster layer in terrain mode under free roam on a plane (`usesDistanceTileZoom()`), the stand-in
ancestors (`STAND_IN_ZOOM_DELTA` up) are fetched BEFORE the view, so a not yet loaded tile has a blurry
picture instead of a bare one. Vector layers keep stand-ins at their old priority. A still camera is
re-culled when the DEM data version moved: a tile's box carries its DEM height range, and a summit
view that culled before its DEM landed (every tile 4800 m below the eye) kept that coarse cut until the
first touch. Past 5 levels coarser than the rule (scale 32) the cut stops coarsening. The cut is
reported every 3 s as `TileLayer: distance cut - N visible tiles of BUDGET ... edge scale S (L levels
coarser than the rule) ... zooms zA:n zB:m` for that imagery, and for any layer whose budget binds: a
scale well over 1 means the budget, not the rule, sets the detail.

Not covered by the pure rule, and why:

- **`extendTilesToNearPlane`** (buildings, `castsExtrusionShadows`) adds the strip below the frame at
  the cover's finest level, a function of the cut and no longer of the camera's zoom.
- **The budget scale** (over budget only) is taken over the tiles in the frustum.
- **The globe**, above.
- **Seamless panning copies** of the world, which are the same tile at another wrap: the one with the
  larger view zoom styles it.

### Tiles below the frame, for buildings

A tile is culled as a flat box on the ground, so one just below the bottom edge was dropped while its
buildings rose into view: a tilted city showed roofless walls along the bottom of the screen. A layer
that may hold extrusions (`castsExtrusionShadows`) adds mapbox's near-plane strip
(`extendTileCoverToNearPlane`, ported in `layers/NearPlaneCover.h`): the tiles at the target zoom
under the frustum's two bottom edges, from the near plane to the ground, within the 3x3 around the
near corner, skipping any the cover already overlaps.

Raising every tile's cull box by 100 m of building instead was tried first and dropped: it pulled in
the whole band below the frame, ~14% of the frame rate at Grenoble z19.2 tilt 30 on the Crosscall
(7.9 -> 6.8 fps, two interleaved pairs).

## Substitution, preloading, caching

- `TileSubstitutionPolicy` decides whether a missing tile is stood in for by a parent/child.
- **Stand-in depth is not overzoom depth.** `MaxOverzoomLevel` (6) says how far up the SDK may go for
  the *data* of a tile the source does not have. `MaxStandInLevel` says how far up it may reach for
  something to *show meanwhile*, and they were one number until it became clear they pull in opposite
  directions: a stand-in must cover a zoom-in of several levels or the map goes empty exactly when the
  user asked for detail (a depth of 1 blanks every layer on a two-level zoom), while a deep stand-in
  redraws the same area from ever coarser tiles. Default 6; lower it per layer for a source whose look
  changes with zoom.
- **The preview parent is fetched last, not first.** When several visible tiles share a missing parent,
  `TileLayer` fetches that parent too "to provide quick rendering". Its `PARENT_PRIORITY_OFFSET` used to
  be **+1** and the fetch list sorts by priority descending, so the coarse preview went out *before* the
  tiles actually wanted — the map showed a ladder of coarser redraws even when the real tile would have
  arrived just as fast, and for a source that generates tiles (traced contours) each preview was a full
  pass thrown away seconds later. It is **−1** now: wanted tiles first, preview after.
- Preloading tiles are those inside an enlarged frustum (`PRELOADING_TILE_SCALE`) but not visible;
  they are fetched at lower priority so panning does not start from nothing. Preloading is **off by
  default**, so by default nothing outside the viewport reached the renderer at all.
- **Label tiles** are the third bucket, between the two: outside the viewport but inside
  `ViewState::getLabelFrustum()`, the band the label culler places against. They are fetched and
  handed over whatever `isPreloading()` says, because a label that does not exist cannot be placed
  before it scrolls in — the measured A/B is in
  [06-labels.mdx](06-labels.mdx#a-label-is-placed-before-it-reaches-the-screen).
- **A tile outside the frustum is handed over for its labels, never for its geometry.**
  `TileRenderer::refreshTiles` splits the draw datas on `isPreloadingTile()` — true for the label
  bucket and the preloading ring alike — and `GLTileRenderer::setVisibleTiles` takes that half as
  `labelOnlyTiles`: it joins `buildLabelMaps` and skips `buildTileSurfaces`/`buildRenderTiles`.
  Drawing them instead cost 4.3% of the frame rate on the Crosscall for pixels nobody sees.
- **Two memory caches, split by use, not by bucket.** A layer keeps every tile a cull asks for - the
  view, the label band, the shadow casters and the preloading ring - in `_visibleCache` (512 MB,
  never meant to fill), and only tiles no cull asks for any more in `_preloadingCache` (an LRU sized
  from the screen, below).
  A fetched tile always lands in the visible cache; `holdTilesInUse` (`layers/TileCacheHold.h`)
  moves the unused ones out on every refresh. Until 2026-09-30 only the view was held and the other
  kinds were cached by their fetch flag: once the label band and the casters outgrew 10 MB, each
  arrival evicted another member of the set and the next cull refetched it, forever, with the map
  redrawing on each arrival. Holding them at refresh alone was not enough either: on the iOS
  simulator several arrivals landed between two culls and pushed each other out first
  ([performance log](../performance-log.md#34-shadow-casters-and-the-label-band-refetched-forever-2026-09-30)).
- **A raster layer's visible cache grows to the set in use.** `RasterTileLayer` used to cap it at
  128 MB, "never meant to fill" - but a hillshade layer on a 512 px DEM (mapterhorn) holds a 1 MB
  normal map per tile, and ~150 tiles at z18 filled it: each arrival evicted a tile in use, the next
  cull fetched that one again, ~130 persistent-cache loads and an RTT drape re-bake per second on a
  still camera, tiles blinking between crisp and parent stand-in. `putVisibleTile`
  (`layers/TileCacheHold.h`) now grows the capacity to take an arrival; the hold bounds the content.
  Logged as `RasterTileLayer: visible cache A -> B MB`. The layer only has tiles when the terrain
  paint is not active, i.e. when the terrain's DEM source is not the layer's own
  (`HillshadeRasterTileLayer::isTerrainPaintActive`).
- **The preloading cache follows the screen** - maplibre's `TileManager.updateCacheSize`: five
  viewports of tiles (`(ceil(w / tile) + 1) x (ceil(h / tile) + 1) x 5`, the tile shrunk by the
  layer's zoom bias) at the average size of the tiles in view, never under the old 10 MB, resized
  on every refresh before the hold. A fixed 10 MB held ~40 decoded satellite tiles, so turning
  round and back re-fetched the whole view: web, terrain-3d at the maplibre 3D-terrain example's
  camera (47.28324, 11.39146 z12.05 tilt 25), 1200x800, two 360 degree turns of 6 s, the second
  turn re-fetched 220 visible tiles with a blank tile in 109-114 of ~305 culls; sized from the view
  (73 MB satellite, 54 MB labels), 52-68 fetches and 9-19 culls with a blank, the rest left over
  from a first turn that had not finished loading. `setTextureCacheCapacity` /
  `setTileCacheCapacity` fix the capacity at the app's number instead.
- **Preloading is a thin border now.** The label band already fetches the tiles within 20-100 px of
  the screen edge; the ring (`PRELOADING_TILE_SCALE`, a tile grown by a quarter each side) adds the
  next sliver: 5-8 tiles, +14-21% loads, at Grenoble z15.5 tilt 45. Neither maplibre nor mapbox
  preload neighbours (they keep parents instead). Kept, off by default, for sources that are slow
  to arrive or raster layers panned fast.
- **Coarse stand-ins are kept under the view** - maplibre-native's `prefetchZoomDelta`
  (`DEFAULT_PREFETCH_ZOOM_DELTA` 4, `tile_pyramid.cpp`). Every cull also fetches each visible
  tile's ancestor `STAND_IN_ZOOM_DELTA` (4) levels up (`layers/TileStandIns.h`; per tile, as the 3D
  cover mixes zooms), at parent priority, never drawn by itself: a tile turned into view finds one
  of them through `findParentTile` and draws on it while it loads, instead of drawing blank. A few
  tiles a view (one per 256 of the cover). Web, terrain-3d at the maplibre 3D-terrain example's
  camera (47.28324, 11.39146 z12.05 tilt 25), 1200x800, WASMFS, a first 360 degree turn of 6 s: a
  blank tile in 159 of 309 culls with 2 tile threads and 10 MB caches, 86-107 of ~235 with 6
  threads and screen-sized caches, **0 of ~249** with the stand-ins too; the second turn 0-4.
- **...so a zoom out looks for the children first** (maplibre-native's `updateRenderables`). `findParentTile` runs before `findChildTiles`, and with the stand-ins above
  a parent is always there: crossing z17 -> z16, every z16 tile still decoding was drawn from its
  z12/z13 stand-in instead of the z17 tiles just on screen, which the cache still held. The map
  dropped to a decode four levels coarser (no buildings, a handful of POIs, coarser point positions)
  until z16 landed, then popped back - on terrain the drape baked that picture over the finer
  generation it had seeded from, since a covering tile counts as complete. Measured on the emulator,
  Grenoble 5.72722/45.19172 tilt 47, an animated z17.49 -> 15.3 zoom over 10 s, frames whose detail
  (mean horizontal gradient) falls under 75% of their neighbours': 12, 6 and 21 dip frames before,
  1, 0, 0, 0 and 0 after.

  **Whichever children are loaded, with the ancestor under the rest**, as maplibre-native does. At
  tilt a z16 tile at the old view's edge often has only the children that were on screen, and asking
  for all four sent it back to the stand-in: its buildings went, and the POIs standing on their roofs
  (`symbol-z-elevate`) fell ~27 m to the street and climbed back once z16 landed. The two overlap
  safely: the 2D stencil masks let a child own its pixels, the drape cover keeps the finest tile per
  area. Same zoom out: roof POIs dropping 6-9 per run before, 0 and 3 after (the 3 at exactly z16.00,
  where Massif's `building-height-scale` ramp reaches 0).
- Tiles live in the layer's memory cache plus an optional persistent cache
  (`PersistentCacheTileDataSource`). The persistent cache is why a device re-run is not a cold run —
  `pm clear` is the only reliable reset ([10-performance.md](10-performance.md)).

## Overzoom: one source tile, many targets

Past a source's max zoom every target tile decodes its own copy of the source tile: at z18 over a
z14 source that is up to 256 decodes of the same data, each clipped to its target plus a 1/8 tile
buffer (`LayerFeatureDecoder`'s clip box). Two things keep that copy small
(`mapnikvt/MBVTGeometryBounds.h`, shared by the MVT and MLT decoders):

- **Out-of-clip features are skipped before any rule reads them.** The clip used to run inside
  `getGeometry`, after every style had evaluated its filters on every feature of the source: 1.2 M
  feature x style evaluations per z18 tile at Grenoble, to keep ~300 features. Overzoomed, the
  decoder now bounds each feature once per source layer and the iterator steps over the ones that
  miss the clip.
- **A merged feature keeps only the parts that reach the tile.** Sources such as OpenFreeMap merge
  features across their tile: every housenumber of a z14 tile in one multipoint, footpaths into one
  multi-line, ~250 buildings per multipolygon. The clip is per feature, so each z18 tile kept them
  whole: 2657 housenumber labels and 300 KB of paths in a tile showing ten. Overzoomed, points, line
  parts and polygons (outer ring) whose bounds miss the clip are dropped. A part that crosses the
  tile is still kept whole; the per-vertex clip in the shaders handles the rest. A kept point
  carries its index in the source feature, which its label id is built from
  ([06-labels.mdx](06-labels.mdx#building-the-label-set)).

The unclipped pass the extrusion anchors read is untouched. Measured in
[performance-log.md 37](../performance-log.md).

## Geometry density: what gets subdivided, and why

Draped content is baked flat into the drape texture, so it decodes at **source density**: with the
defaults (`DrapeFillsEnabled` and `DrapeLinesEnabled` both true) neither fills nor lines are
subdivided. What follows is content drawn LIVE — fills when `DrapeFillsEnabled` is false (tangram
content mode, which also leaves lines at source density), lines when only `DrapeLinesEnabled` is.

With the shared ground, live content is displaced per vertex by the same elevation function the
ground uses, so it does **not** need to be tesselated to follow the terrain — tangram does not
subdivide at all. Ours subdivides live **area fills**, to two surface cells
(`TerrainTileTransformer.cpp`, `AREA_THRESHOLD_CELLS = 2`, overridable in a demo build with
`adb shell setprop debug.massif.areathreshold N`).

The reason is not the displacement, it is the depth model: an un-subdivided fill chords across the
displaced surface, and the constant clip-space `depth_shift` has to cover that chord
([05-depth-model.md](05-depth-model.md)). Measured on device at the mountain camera:

| fill subdivision | fps | geometry indices per render tile |
|---|---|---|
| 1 cell | 16.6 | 158k |
| **2 cells (shipped)** | **20.6** | **48k** |
| 4 cells | 21.2 | 19k |
| source density (none) | — | shows floating-fill patches |

Lines are **not** subdivided by density. They were cut exactly where they cross a surface-lattice
line (`x = k·cell`, `y = k·cell`, `x + y = k·cell` in tile uv), so every sub-segment lies inside one
surface triangle; they are now cut by their **sag** — only where the chord would leave the terrain
by more than `DEFAULT_LINE_SAG_METERS` (2 m) — for 3.4× fewer indices
([04](04-terrain.md#cutting-a-line-by-its-sag-instead-of-by-the-tiles-cell-count);
`debug.massif.linesag 0` restores the lattice cut). Tangram displaces line vertices
in the shader instead and subdivides nothing; that costs **13× the index throughput** here, and a
coarse or proxy tile's roads chord straight across the terrain until the finer tile arrives — the
"roads go straight when zooming out" report.

**Fills used to be subdivided even with draping on**, deliberately, and that was reversed —
`TileLayer` now gates fill subdivision on `isDrapeFillsEnabled()`
([04](04-terrain.md#a-draped-fill-is-not-subdivided-any-more)). The old argument is the risk the
reversal still owes a device check for: draping is decided **per tile at render time** while the
density is decided **globally at decode time**. An un-subdivided fill that then does not get
draped sags below the surface and leaves the bare background colour (the "landcover
holes"), and tiles fall through the drape cover constantly: the cover is capped at the camera zoom
and a hillshade contributes its DEM-limited zoom, so render tiles finer than the cover are normal.
Suppressing those tiles instead was tried — the map becomes a stretched coarse drape. The
subdivision is not what costs: emulator, `meshResolution` 128, drape on, scripted 3-level zoom gives
median 58–60 fps and the same worst-case bake spike either way.

The decode-time density flags must match between `TileLayer::loadData` and
`resetTileTransformer`, or tiles decoded for the other mode stay in the cache forever.

## Where tiles come from

Standard sources (HTTP, MBTiles, PMTiles, assets) plus two of interest here:

- **`ContourTileDataSource`** — generates contour MVT from a DEM source on the fly, sharing the DEM
  with the hillshade so terrain tiles are fetched once. It has a label-stub mode that makes it emit
  only what labels need; see [07-hillshade-contours.md](07-hillshade-contours.md).
- **elevation sources** — decoded by `MapBoxElevationDataDecoder` / `TerrariumElevationDataDecoder`
  into `ElevationTileGrid`s held by `ElevationManager` ([04-terrain.md](04-terrain.md)).
- **`MergedMBVTTileDataSource`** — concatenates two MVT sources' tiles. A merged tile holds one
  zoom, so the layer cannot overzoom one source alone: past a source's max zoom its last tile is cut
  into the requested one (`MBVTSubtile.h`). The cut stops at that source's `maxOverzoomLevel` when
  one is set; past it the source answers nothing, so the other source's "replace with parent"
  reaches the layer. Unset, it cuts at any zoom — which hides that answer: a z0-6 bathymetry
  archive merged over a chain whose last tile is z7 drew empty tiles at z10 instead of the
  overzoomed z7 one.

## Source meta data, and what a tile carries

A `TileDataSource` holds a `std::map<std::string, Variant>` of settings — `getMetaData` /
`setMetaDataElement`, the same shape `VectorElement` and `Layer` already use — and **attaches it to
every `TileData` it loads**. That is how a consumer learns something about a tile it did not fetch
itself: `dem_encoding` (`"mapbox"` / `"terrarium"`) is the one that matters today, read by
`ElevationDecoder::Resolve` for the terrain, the hillshade and the contours alike.

Three properties are load-bearing:

- **Per TILE, not per source.** Two DEM sources of different encodings behind one
  `OrderedTileDataSource` each stamp their own map, and only the tile knows which one answered. A
  source-level lookup cannot: the wrapper has no encoding of its own.
- **Shared, not copied.** The map is held as a `shared_ptr<const map>`, so attaching it is one
  atomic increment; `setMetaDataElement` on a tile copies first. Cheaper than the per-tile map it
  replaced, which allocated a node and a `Variant` for every tile loaded.
- **Persisted with the blob.** `PersistentCacheTileDataSource` stores it as JSON in a `metaData`
  column. Without that, a cache hit above a wrapper source would resolve the wrapper's (empty) map
  and silently fall back to MapBox — the bug this column exists for. An older database is missing
  the column and is dropped and recreated on open.

Distinct from it, and deliberately so: **`getContainerMetaData(key)`** reads the *container's* own
metadata — the MBTiles metadata table, the PMTiles header JSON — returns a plain string, and is
**not** attached to tiles, because an MBTiles `json` field is tens of kilobytes. `getMetaDataElement`
falls back to it, so a tileset that already declares `dem_encoding` or `format` in its own table
needs no application code. Archives rarely do: they name it **`encoding`** (`{"encoding":"terrarium"}`
in a PMTiles DEM), so `MBTiles` and `PMTiles` adopt an `encoding` of `terrarium` or `mapbox` into
the map at construction, where it is stamped on every tile like an app-set one. Any other value is a
vector archive's tile format and is left alone. `setMetaData` keeps the adopted entry unless the new
map names `dem_encoding`. The wrapper sources (`Cache`, `Contour`, `Ordered`, `Combined`) forward
both: own entries first, then the first child that declares any.

## Two binary formats: MVT and MLT

`MBVectorTileDecoder` reads either. `setTileFormat` takes `TILE_FORMAT_AUTO` (the default),
`TILE_FORMAT_MVT` or `TILE_FORMAT_MLT`. Everything downstream of the decode — CartoCSS,
symbolizers, style parameters, the `vt::Tile` — is shared; only the bytes-to-features step differs.

```
MBVectorTileDecoder ─ createFeatureDecoder() ─┬─ MBVTFeatureDecoder  (protobuf, pbf.hpp)
                                              └─ MLTFeatureDecoder   (libs-external/mlt)
                                                        │
                             LayerFeatureDecoder ───────┘  ← what LayerTileReader reads
```

`LayerFeatureDecoder` is the seam: it owns the tile-to-target transform, the clip box and the
feature-id override, and declares the four layer operations (`getLayerNames`, `hasLayer`,
`createLayerFeatureIterator`, `findFeature`). `LayerTileReader` — formerly `MBVTTileReader` — reads
through it, so it serves both formats unchanged. `TorqueFeatureDecoder` stays on the plain
`FeatureDecoder` base: its features are addressed by frame, not by layer.

### Detecting the format

There is no magic number, but the two are separable by **framing**. An MLT tile is a sequence of
`(varint layer length, varint layer tag, body)` that has to tile the buffer exactly, and the layer
tag is 1 or 2. MVT is protobuf — its first byte is `0x1A` (field 3, wire type 2) and it does not
fit that shape. `MLTFeatureDecoder::isTileData` runs that check; `TILE_FORMAT_AUTO` calls it.

Measured against maplibre-tile-spec's own fixture corpus — 663 `.mlt` and 134 `.mvt`, the same
OpenMapTiles and Bing tiles in both formats plus the synthetic set:

| rule | MLT detected | MVT false positives |
|---|---|---|
| framing, layer tag = 1 | 654/663 | 0/134 |
| framing, layer tag ∈ {1,2} | **663/663** | **0/134** |

The 9 tag-1 misses are `test/synthetic/0x02/`, an experimental layer tag `mlt::Decoder` skips
anyway. Note what the corpus does *not* cover: every `.mvt` in it comes from one encoder family, so
an exotic MVT producer — leading extension fields, say — has not been tested against this. Set the
format explicitly when the source is known and none of that matters.

MapLibre itself does not detect: its style spec puts `encoding: mvt|mlt` on the source, defaulting
to `mvt`. `TILE_FORMAT_MVT`/`TILE_FORMAT_MLT` are the equivalent, for a source that declares.

**A declaring source wins over detection.** The `VectorTileLayer` constructor asks the source for
`encoding`, then `format`, runs them through `MBVectorTileDecoder::parseTileFormat`, and pins the
decoder when either is conclusive; a decoder the app already set explicitly is left alone. It asks
through `getMetaDataElement`, so either the app or the container can declare it — see
[the meta data section](#source-meta-data-and-what-a-tile-carries) for which is which.

`parseTileFormat` matches case-insensitively by substring, because generators spell this
differently. **`pbf` is deliberately inconclusive**: MapLibre's own demotiles declare
`"format": "pbf"` with `"encoding": "mlt"`, so treating `pbf` as proof of MVT would force the wrong
decoder on a tileset detection gets right. Only an explicit `mvt` / `…mapbox-vector…` pins MVT;
`maplibre` or `mlt` anywhere pins MLT; everything else falls through to the framing check.

Note this pins one format per decoder. A decoder shared between layers whose sources differ in
format should be left on `TILE_FORMAT_AUTO`, which costs 1-19 ns a tile.

Detection needs uncompressed bytes, so `MBVectorTileDecoder::createFeatureDecoder` inflates first
and hands the plain buffer to whichever decoder it picks — the decoder's own `inflate_tile` then
only re-checks three magic bytes.

Behaviour differences worth knowing:

- **MLT decodes the whole tile.** `mlt::Decoder::decode` returns every layer and every property
  column; the format has no lazy path and its C++ API takes no layer filter. MVT decodes per layer,
  on demand, and only the attributes the style asked for (`fields`). On an OpenMapTiles schema with
  a `name:*` column per language that is a real difference, and it has not been benchmarked yet.
- **No FeatureData cache on the MLT side.** `MBVTFeatureDecoder` dedups identical attribute sets by
  their tag-index vector; MLT properties are columnar and carry no equivalent key, so each feature
  gets its own `FeatureData`. The geometry cache (keyed by feature index) is kept for both.
- **Buffer features are clipped identically.** MLT `place` features in the OMT fixtures reach
  ±8000 on a 4096 extent; both decoders drop what falls outside the clip box, so the same features
  survive.
- Pre-tessellated triangles (`mlt::geometry::Geometry::getTriangles`) are **ignored** — vt still
  tessellates with tess2. Wiring them through would skip that work and is the obvious next win.

The decoder library itself is vendored decoder-only; what it costs in bytes is in
[../build-and-size.md](../build-and-size.md).

## GeoJSON tiles: the on-demand pyramid

`GeoJSONVectorTileDataSource` cuts MVT out of an in-memory GeoJSON layer through
`mbvtbuilder::MBVTTileBuilder`. It used to do this the direct way, and both halves scaled badly:

- `encodeLayer` walked **every feature of the layer for every tile**, keeping only a bounding-box
  test — so a request cost O(features) no matter how little of the layer the tile held;
- geometry was **re-simplified and deep-copied per zoom level** into `_cachedZoomLayers`, leaving one
  full copy of the dataset (geometry *and* every feature's `picojson` properties) resident per zoom
  the camera had visited.

It now uses mapbox's **geojson-vt** (three submodules under `libs-external/geojsonvt`), the same
library tangram-es uses for its `ClientDataSource`. Two of its ideas do the work:

- **Simplification importance is computed once at import.** `detail::convert` runs Douglas-Peucker
  against the tolerance of the deepest zoom and stores each vertex's importance on the point; a tile
  then only filters `p.z > sq_tolerance`. The per-zoom copy is gone entirely.
- **Tiles are cut from the slice their parent already made**, axis-separated (one x pass feeds both
  children), so a feature is walked once per *level* instead of once per tile.

Properties never enter the index: features carry a slot index and the `picojson` values stay in
`MBVTLayerData::infos`, so slicing never touches one.

### What is ours and not geojson-vt's

The pyramid is driven by our own drill rather than `geojsonvt::GeoJSONVT`, because four things about
that class cost measurable time in an on-demand SDK (it is written for a batch tiler):

| | geojson-vt | here | why |
|---|---|---|---|
| root | always z0 | deepest tile containing the layer | a city-sized layer paid ~9 levels of whole-dataset copies before the first real cut (209 ms, device) |
| int16 tile | built for every node walked | only for the requested tile | internal nodes are never drawn |
| built tiles | cached forever | not cached | the SDK caches the encoded MVT above us |
| stop condition | `indexMaxPoints` (100k) | nothing in the node is bigger than the **target** tile | see below |

**The stop condition is the load-bearing one.** Splitting costs a pass over the whole node per level
and only helps features that must be *cut*; a feature smaller than the tile is thrown out by the
clipper's per-feature bbox test for two comparisons either way. Splitting a layer of small scattered
features therefore just copies them down the tree: 5000 short routes cost **583 ms** over 256 tiles
at z14 where the old scan-every-feature builder took **209 ms**. The test is against the **target**
zoom, not the next level down — a piece that fits one child can still span dozens of tiles at the
zoom actually wanted, and stopping on that cost the long-route set 575 ms instead of 274 ms.

One more trap, because it inverted a result twice: `detail::clip` does
`clipped.reserve(features.size())`. Running it straight over a coarse node allocates for the entire
layer on **every** tile, which made serving from a coarse node cost more than the full scan it
replaced. Features touching the tile are picked by bbox first, and only those go to the clipper.

A builder pinned to one zoom (`minZoom == maxZoom`, which is how `ContourTileDataSource` uses it)
skips the index and cuts its single tile directly — geojson-vt's own `geoJSONToTile`, minus the
variant round-trip. There is nothing for an index to amortise over one tile.

### Measured (Crosscall HLTE556N, Adreno 610, `--es geojsonBench`)

640 tiles over z8–z17, four per side per zoom:

| dataset | before | after |
|---|---|---|
| 5000 short routes, 165k points | 1319 ms | **1176 ms** |
| 8 routes of 100–250 km, 303k points | 1709 ms | **513 ms** |

256 tiles at z14 alone (what panning at one zoom does):

| dataset | before | after |
|---|---|---|
| 5000 short routes | **209 ms** | 328 ms |
| 8 long routes | 362 ms | **272 ms** |

So: long lines win everywhere (up to 3.3x), and the many-small-features case is better across zooms
(where the old builder re-simplified per zoom) but **still 1.6x slower at a single zoom**, because
the old path clipped geometry already simplified for that zoom while this one clips at full
resolution and filters by importance afterwards. That gap is open.

### Binary size

geojson-vt is header-only, and the code it replaces went away, so the cost is small but not zero —
`libmassif.so`, arm64-v8a, debug build, stripped:

| | bytes |
|---|---|
| before | 16 281 408 |
| after | 16 385 664 |
| | **+104 256 (+0.64%)** |

That is template instantiation: the clipper, `InternalTile` and the geometry variant are stamped out
per geometry type. No new runtime dependency ships — nothing is linked, only included.

(The demo APK also grows ~10 MB from the two bench GeoJSON assets. That is the test bench, not the
SDK; nothing in `all/native` reads them.)

### Render cost is a different question, and simplification owns it

Loading the 5000-route set as a real layer over 3D terrain drops the device to **4 fps**. That is
**not** the tiler: the old builder measures the same (4.1 fps, 24.8M `geomIndices` per interval,
against the pyramid's 4.2 fps / 24.4M). It is the demo asking for `simplifyTolerance = 0`.

At tolerance 0 nothing is ever dropped, at any zoom, so all 165k source points reach the line
tesselator — twice, because the route style draws a casing under the fill with round joins — and
then get subdivided again at every terrain-lattice crossing. Panning at z13 / tilt 35, `--es
geojsonLayer many`, ten swipes:

| | fps | geomIndices / interval |
|---|---|---|
| no layer | 20 | 5.1M |
| layer, tolerance 0 | **4.2** | 24.4M |
| layer, tolerance 1 (SDK default) | 14.9 | 8.9M |
| layer, tolerance 2 | 15.7 | 7.7M |

The GL thread is not the limit at 4 fps — frames average 42–46 ms but only five are issued per
second, so most of the wall clock goes to the tile threads tesselating that geometry, and
`renderTiles` collapses from 714 to 212 because tiles cannot be built fast enough to keep the set
full.

`MBVTTileBuilder` already defaults `_simplifyTolerance` to 1.0; the route test sets 0 deliberately
(vertex-dense input is what its join cases need), and the bench layer used to inherit that. It has
its own `--es geojsonBenchSimplify` now, defaulting to the SDK's 1.0. **An app that leaves the
default alone does not hit this.** The tile-build table above was taken at tolerance 0.

The bench is `DemoTests.runGeoJSONBench` — it times `loadTile` directly, no renderer in the way, over
a tile set derived from the data extent so two builds are comparable. `--es geojsonLayer many|long`
adds the same datasets as a real styled layer instead, for render-side comparison.
