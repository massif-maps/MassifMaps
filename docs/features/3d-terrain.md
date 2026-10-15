---
title: 3D Terrain
sidebar_position: 1
mdx:
  format: mdx
---

import LiveExample from '@site/src/components/LiveExample';

# 3D Terrain

Render the map draped over **real elevation**, with correct depth occlusion (near ridges hide
far slopes), fill draping and fast zooming.

<LiveExample id="terrain-3d" />

## How it works

Terrain consumes **RGB-encoded elevation tiles** (MapBox or Terrarium encoding) from any
`TileDataSource`. The renderer builds a per-tile surface mesh, displaces it by the decoded
elevation, and draws the map on top of it.

- **Fill draping** — polygon fills and the style background are baked *flat* into a per-tile
  offscreen texture (MapLibre-style render-to-texture) and used as the terrain surface's texture.
  Fills therefore follow the terrain exactly: zero holes, zero see-through, no depth slack. The
  bake is cached per tile, so steady-state panning does no offscreen work.
- **Depth occlusion** — the draped surface writes true depth and *is* the occluder, so a near
  ridge correctly blocks the far slope's raster, contours and route lines (shared depth buffer,
  painter-order model).
- **Sharp geometry** (contour and tile lines) is displaced and lattice-clamped to the surface,
  drawn `GL_LEQUAL` with zero depth bias so it hugs the terrain without leaking through ridges.

## Quick start

```kotlin
import com.massifmaps.components.TerrainOptions
import com.massifmaps.datasources.HTTPTileDataSource
import com.massifmaps.datasources.MemoryCacheTileDataSource

// 1. An RGB-elevation source (Terrarium or MapBox encoding).
val demSource = MemoryCacheTileDataSource(
    HTTPTileDataSource(0, 12, "https://your.tiles/dem/{z}/{x}/{y}.png").apply {
        // the "dem_encoding" meta data selects the decoder: "terrarium" or "mapbox"
        setMetaDataElement("dem_encoding", Variant("terrarium"))
    }
)

// 2. Build terrain options. Each tile resolves its decoder from its own "dem_encoding".
val terrain = TerrainOptions(demSource).apply {
    isEnabled = true
    isDrapeFillsEnabled = true        // render-to-texture fill draping (default on)
    meshResolution = 64               // grid cells per tile edge (2..1024, default 64)
    exaggeration = 1.0f               // 1.0 = true-to-scale
}

// 3. Attach to the map.
mapView.options.terrainOptions = terrain
```

```swift
let dem = MSFMemoryCacheTileDataSource(dataSource: httpDem)
let terrain = MSFTerrainOptions(dataSource: dem)
terrain?.setEnabled(true)
terrain?.setDrapeFillsEnabled(true)
terrain?.setMeshResolution(64)
mapView.getOptions()?.setTerrainOptions(terrain)
```

### With the surface API

The same three steps are one spec and one property path — and the knobs stay reachable as
`terrain.*` on the map afterwards:

```java
map.terrain(Spec.of("terrain")
    .set("source", Spec.of("memory-cache")
        .set("source", Spec.of("http")
            .set("url", "https://your.tiles/dem/{z}/{x}/{y}.png")
            .set("maxZoom", 12)
            .set("metaData", Spec.object().set("dem_encoding", "terrarium"))))
    .set("enabled", true)
    .set("meshResolution", 64)
    .set("exaggeration", 1.0));

map.set("terrain.drapeLinesEnabled", true);
map.set("terrain.autoFlattenParallax", 1.5);
```

Every terrain property: [`terrain` in the options reference](/docs/api/reference/options).
Runnable, on four platforms (live on the web): [the 3D terrain example](/examples#terrain-3d) and
[2D ↔ 3D switching](/examples#terrain-2d-3d).

:::tip Share the DEM with hillshade
`TerrainOptions` can share its elevation `TileDataSource` with a
[`HillshadeRasterTileLayer`](/docs/features/hillshade). Wrap the source in a
`MemoryCacheTileDataSource` so both features hit the same tiles instead of downloading twice.
:::

## Mixing two DEM sources of different encodings

`dem_encoding` is resolved **per tile**, not per source, so an `OrderedTileDataSource` may combine
a MapBox-encoded DEM with a Terrarium one. Every tile carries the meta data of the source that
actually answered for it, and the terrain, the hillshade and the contours each decode it with that
tile's own coefficients.

```kotlin
val mapboxDem = HTTPTileDataSource(0, 15, "https://a.tiles/dem/{z}/{x}/{y}.png").apply {
    setMetaDataElement("dem_encoding", Variant("mapbox"))
}
val terrariumDem = HTTPTileDataSource(0, 12, "https://b.tiles/dem/{z}/{x}/{y}.png").apply {
    setMetaDataElement("dem_encoding", Variant("terrarium"))
}
// mapboxDem answers first; terrariumDem fills what it does not cover.
val demSource = OrderedTileDataSource(mapboxDem, terrariumDem)
```

Two rules make it work:

- **Tag the leaves, not the wrapper.** A wrapper source has no encoding of its own; it forwards a
  child's only as a fallback for consumers that ask before any tile has loaded.
- **Cache each leaf, or cache above them — either is fine.** A `PersistentCacheTileDataSource`
  stores the tile's meta data alongside the blob, so a cache hit resolves the same decoder a fresh
  fetch would. A cache database written by an older SDK has no such column and is rebuilt on first open.

Where the two coverages meet, the terrain's border backfill notices the encodings differ and
resamples through metres instead of copying texels, so the seam is continuous.

## `TerrainOptions` reference

| Property | Default | Notes |
|---|---|---|
| `Enabled` | `true` | When off, the map renders flat but the DEM stays attached. |
| `Exaggeration` | `1.0` | Height multiplier. Changing it re-tesselates loaded tiles (costly). |
| `MeshResolution` | `64` | Grid cells per tile edge, clamped `2..1024`. Limited by DEM resolution. |
| `DrapeFillsEnabled` | `true` | Render-to-texture fill/background draping. |
| `DrapeLinesEnabled` | `true` | Drape tile lines too (softer, zero-cost hug). |
| `Bridges3DEnabled` | `false` | Lift `span` features onto the chord between their portals and draw span decks as extrusions. Off, they drape like the ground and none of the span machinery runs. |
| `DrapeResolution` | `0` | Drape texture size, clamped `128..2048`; `0` = derived from the screen. |
| `NoDrapeLayerFilter` | `^contour\|maneuver.*` | Regular expression on style layer names kept out of the drape (sharp geometry); empty drapes everything. |
| `SeamlessTileEdgesEnabled` | `true` | Backfill the 1-texel DEM border from the neighbour level — removes the ridge at tile borders. |
| `ElevationPrefetchEnabled` | `true` | Also request the neighbours of every visible terrain tile. |
| `TileEdgeStitchingEnabled` | `true` | Stitch the mesh across tiles of different levels. |
| `BackgroundColor` | transparent | Fill color drawn before tiles (works even with zero tile layers). |
| `BackgroundBitmapEnabled` | `false` | Drape `Options.getBackgroundBitmap()` over the terrain (world-anchored, repeats). |
| `ViewDistanceFactor` / `ViewDistance` | `1.0` / `0` | Where the ground ends; `ViewDistance` is a **minimum** in metres and only extends the factor rule. Pair it with fog ([FogOptions](/docs/features/sky-sun-shadows)); the gap past it shows the sky's own horizon, see [No band under the sky](/docs/features/sky-sun-shadows#no-band-under-the-sky). |
| `AutoFlattenParallax` / `AutoFlattenTilt` | `2` / `88` | Render flat once 3D stops earning its cost — see below. `0` disables each half. |
| `AlwaysDecode` | `false` | Decode tiles for terrain, and warm the elevation data, while terrain is disabled too, so `setEnabled(true)` re-decodes nothing. Costs the elevation fetches and about 2% frame time in 2D ([measured](../internals/rendering/04-terrain.md)). |
| `AutoFlattenDuration` / `Flattened` | `0.3` / `false` | Length of the flattening animation (seconds), and the 2D/3D state — set it to switch by hand, read it to know what auto-flattening chose. |
| `MaxTileZoomCoarsening` | `3` | How much coarser far tiles may get. |
| `BillboardOcclusionEnabled` / `…Tolerance` | `true` / `0.05` | Hide markers and popups behind a ridge. |
| `SurfaceShaderSource` | — | Replace the terrain surface shader ([post-processing](/docs/features/post-processing)). |
| `MinZoom` / `DepthBias` | `5` / `0.0002` | LOD floor and depth slack for draped geometry. |

## Rendering flat when 3D buys nothing

All the ways a map can be flat, and what each costs, are in [2D, 3D and everything between](/docs/features/terrain-2d-3d-states).

Zoomed far out, the displacement is smaller than a pixel; straight down, it shows nothing. Both
still pay for the drape pass, the terrain draw and the elevation fetches, so the map handles it
itself — **on by default**, at 2 px and 88°. Change or disable either half:

```java
terrainOptions.setAutoFlattenParallax(4);   // flatten sooner
terrainOptions.setAutoFlattenTilt(0);       // never flatten on tilt alone
```

The threshold is **screen parallax, not a zoom level**: how far the highest ground in view moves
because it is displaced. It therefore adapts to how mountainous the data is and to your
`Exaggeration` — over the Alps, 2 px lands around zoom 8, and flat country flattens earlier. The
transition is animated (`AutoFlattenDuration`), `isFlattened()` tells you what the map is doing, and
`isEnabled()` keeps returning whatever you set, so your own terrain toggle still works.

## Orbiting the point under the pointer

A tilt gesture turns the camera about the centre of the screen, as in maplibre and mapbox. With
`Options.OrbitAroundPivot` (off by default) it turns about the terrain point under the pointer
instead, and that point stays where it is on screen:

- **Two-finger tilt** (Android, iOS): about the ground between the fingers when they went down. The
  two-finger rotate already turns about that point.
- **Right-drag or ctrl-drag** (web): the rotation and the tilt both turn about the ground under the
  mouse where the drag started.

```java
mapView.getOptions().setOrbitAroundPivot(true);
```

```js
map.set('orbitAroundPivot', true);
```

`PIVOT_MODE_CENTERPOINT` keeps the centre as the pivot either way. The zoom is the distance to the
ground at the centre of the screen, so it is read again when the gesture ends: after an orbit towards
the horizon the picture stays put, but labels and tile detail can drop a level.

## Querying elevation

`TerrainOptions` can return heights for map positions:

```kotlin
val metres: Double = terrain.getElevation(mapPos)          // single
val many: DoubleVector = terrain.getElevations(mapPosVector) // batched
```

## Performance notes

- **Draped content skips terrain subdivision** (it is baked flat), so vertex buffers upload at
  source density instead of ~`meshResolution²` per tile. This, plus an LRU elevation-texture cache
  (no full flush), removes most of the fast-zoom render-thread stall.
- The default `meshResolution = 64` is a good quality/cost balance; go higher only if you see
  terraced slopes at close range.
- The shared regular grid and the painter-order depth model are always on where the GPU supports
  vertex texture fetch; there is no per-tile CPU tesselation to pay for there.

## Known limitation

At low zoom, `VectorLayer` **element** lines (e.g. long routes) can still leak through a ridge:
they are CPU-baked to a fine bilinear surface while the low-zoom occluder is coarse, and no depth
bias wins that case. The fix (render-to-texture element draping across layers) is on the roadmap.

On a face steeper than about 65 degrees the draped map is blurred (fully by 80 degrees), because a
top-down drape stretches along the fall line: a cliff line or hachure would otherwise hang down the
whole wall. A trail or road drawn across such a face fades with it. Contour lines fade the same way
from 55 degrees (gone at 70), since a face is where their levels pile into a solid band.

## Lines behind buildings: `line-occlusion-opacity`

With 3D buildings on, an extrusion drawn in front of a line hides it. A route or a maneuver arrow
that goes behind a building disappears. `line-occlusion-opacity` (mapbox's property of the same
name) keeps it as a ghost:

```css
#navigation[class='route'] {
    line-color: #2a6fdb;
    line-width: 6;
    line-occlusion-opacity: 0.35;   /* 0 (default) = hidden there, as before; 1 = as visible as the rest */
}
```

The line draws as usual, and where an extrusion (or a ridge) is in front of it, a second draw at
that fraction of its opacity shows through. Not a separate colour: it is the line's own colour and
alpha times the property.

- **The line stops draping.** A draped line is baked into the ground texture and has nothing to be
  occluded by, so a rule that sets the property is drawn live, like a layer matched by
  `NoDrapeLayerFilter`: sharper, but a live line over steep relief can show small gaps a draped one
  does not. Other lines in the same layer keep draping.
- **It is the extrusions that must be in front.** Use it on a line in a layer drawn *after* the
  buildings' layer (a navigation layer over the base map). The ghost is tested against the depth
  buffer at the end of that layer's 3D pass, which holds the buildings of every layer below it and
  its own.
- **At every tilt.** The ghost shows wherever a building actually hides the line, whatever the camera
  tilt or the building's height (a flattened roof included): the building stamps the pixels it wins.
- **Ridges ghost it too.** The test is "something nearer wrote depth here", so a line going behind
  a ridge shows faintly through it, exactly as through a building.
- **One faint colour per pixel, topmost layer first.** The ghosts of the whole map are drawn as one
  group after the last layer, so a casing under a fill (a maneuver arrow) never tints the fill and an
  arrow over the route behind a building hides the route's ghost under its own.
- **Not applied** to a layer with a `comp-op` (it draws into an overlay with no depth), nor to an
  extrusion layer faded by layer opacity below 1 (its buildings are in a separate buffer). Where
  two tiles of different zoom overlap during a zoom transition, the ghost is drawn once per pixel,
  by whichever tile comes first: the 2D pass's stencil masks do not apply to it.

### Cost of `line-occlusion-opacity`

A ghost is not free, and it is paid whether or not anything is in front of the line this frame. Read this
before putting the property on a layer with many features.

**What a frame pays.** Lines without the property cost nothing extra. For every tile that holds a ghosted
rule, the renderer draws the flagged batches again, after the last layer, in up to **four passes**: a sweep over
the pixels an extrusion covers and a sweep by depth (for ridges), each as a core and a rim pass. A batch holds
the styles of several rules (a route's casing and fill, an arrow's four attachments, a `back/` line and its main
line) and is drawn **once per style** in each pass, so the vertices are submitted `styles x passes` times. On top:
one stencil clear (bit 0x80) per frame, and a walk over the visible tiles of every ghosted layer per pass. The
passes are skipped when nothing can hide a line: no extrusion or model drawn this frame and no 3D terrain (flat
map, no buildings in view: nothing); buildings on a flat map: one sweep; 3D terrain without buildings: the
depth sweep only; terrain with buildings: both.

**Measured** (Apple M5 Pro, macOS 26.5, the Mac Catalyst demo on Metal-ANGLE, 800x600 pt window at 2x, Grenoble
centre with 3D terrain, hillshade, 3D buildings, trees and labels, offline tiles; route with 4 arrows in two
layers as the app draws them, plus N lines of the saved-items style, dashed, `back/` and main both ghosted, 4
vertices each, placed at random over the view; median of five 120-frame windows while the camera turns). The
ghost pass was timed with `glFinish` before and after it, so the numbers are its CPU time (encoding) and
CPU + GPU time (the whole pass, including the wait for the GPU); the frame column is `drawLayers` bracketed the
same way, with a PNG readback per frame, **other apps were using the GPU**, so read the frame columns to
about 1-2 ms. Not a vsync-limited frame rate: the frame work.

| view | lines | frame, ghost off | frame, ghost on | ghost pass CPU | ghost pass CPU+GPU | draw calls | indices submitted |
|---|---|---|---|---|---|---|---|
| z17.5 tilt 45 | route + arrows only | 26.0 ms | 26.3 ms | 2.2 ms | 2.7 ms | 100 | 6.9 k |
| | + 1 line | 25.8 | 28.4 | 2.6 | 3.2 | 124 | 7.4 k |
| | + 10 lines (300 m) | 28.0 | 28.3 | 2.9 | 3.6 | 425 | 38 k |
| | + 50 lines (300 m) | 26.1 | 29.7 | 3.2 | 4.1 | 654 | 54 k |
| | + 200 lines (20 m) | 26.0 | 28.7 | 3.1 | 4.1 | 506 | 207 k |
| | + 200 lines (300 m) | 26.7 | 30.2 | 3.6 | 5.0 | 768 | 213 k |
| z18.6 tilt 60 | route + arrows only | 22.1 | 22.6 | 1.3 | 1.9 | 173 | 7.2 k |
| | + 1 line | 20.4 | 22.0 | 1.4 | 2.0 | 205 | 7.8 k |
| | + 10 lines (300 m) | 21.1 | 21.9 | 1.9 | 2.6 | 471 | 28 k |
| | + 50 lines (300 m) | 22.0 | 24.8 | 2.8 | 3.6 | 778 | 95 k |
| | + 200 lines (20 m) | 20.6 | 21.9 | 2.3 | 3.2 | 665 | 228 k |
| | + 200 lines (300 m) | 21.3 | 27.5 | 3.1 | 4.7 | 976 | 425 k |

(Draw calls and indices are per frame, over the four passes. "Frame" is not the app's frame rate: it holds a
per-frame screenshot.) With the same 10 lines on a flat map (no terrain) the ghost pass took 0.25 ms CPU,
0.8 ms CPU+GPU on a 4.8 ms frame, about 15 %; on the 3D terrain about 3.6 ms on 26 ms, about 12 %.

**What it scales with** (measured trend, then the reasoning):
- *Per ghosted tile and style: the fixed part.* The route and its arrows alone are 1.9-2.7 ms: a few dozen batches
  times styles times passes, each draw call costing ~20 us of CPU (uniform tables, program switch between the
  core and rim shaders), nearly independent of how long the lines are. Reasoned: most of it is draw-call
  overhead, not fill.
- *Per feature: small until the lines are many and long.* 10 lines add ~0.5-0.9 ms, 50 add ~1.5-1.7 ms, 200 short
  lines ~1.3-2 ms. Batches are per tile and style, not per feature, so features that share a tile share draws;
  what grows is the vertices (indices column) and the pixels covered. 200 long lines at z18.6 (425 k indices
  submitted, mostly off the screen into neighbouring tiles' buffers) reach ~4.7 ms.
- *Pixels.* Every pass rasterises the line's full coverage whether or not a building is in front: the depth and
  stencil tests reject the pixel after the fragment is shaded for the core pass. A wide casing under a wide fill
  covers its area twice per pass. Reasoned from the passes; not isolated by a measurement.
- *Layers and renderers.* A layer is walked once per pass; a map with several ghosted tile layers pays the walk
  per layer. Reasoned.
- *Not on occlusion.* The cost is the same with nothing in front of the line (counters: the draw calls do not
  depend on what is hidden); it is only avoided when no extrusion, model or terrain exists at all, as above.

**Recommendations.**
- Use it on the few lines that must stay readable behind a building: the route being followed, its maneuver
  arrows, the selected item. In the measurements, route + arrows + ten lines was ~3.6 ms (about 12 % of a
  26 ms terrain frame on a desktop-class GPU), fifty lines ~4 ms, and 200 long lines ~5 ms.
- Do not put it on dense layers: roads, rails, contours, boundaries, or **all** saved items at once. A list of
  items is a handful of tiles but hundreds of features; if it is needed, keep it on the selected ones (a rule
  with `when ([param::selected_id] = [id])`), not on the whole layer.
- Put it on one rule per visual line. A `back/` backing and its main line each add a style to the batch and so a
  draw per pass; the main line's ghost alone is usually enough (a white backing's ghost is barely visible on a
  light roof).
- A line with `line-border-*` is drawn in the rim pass only.
- There is no per-feature or per-zoom switch: `line-occlusion-opacity` may use a zoom expression, and 0 skips the
  rule's ghost entirely (the rule never reaches the ghost pass).

**Requirements and fallback.** The grouping that keeps a casing from tinting its fill needs an 8-bit stencil
buffer on the framebuffer (the map's own packed depth-stencil buffer has one on every platform tested; no extra
memory is allocated, bit 0x80 and 0x40 are used for the frame). Without it the ghosts are drawn in stack order,
bottom first, as before: the tint (a blue casing under a white fill reading as light blue) comes back, and the
only cost is one draw per batch. A layer with a `comp-op`, or an extrusion layer drawn into the overlay (layer
opacity below 1), is not ghosted. The ghost pass shares its stencil bit with the draw-once groups (their passes
are over before it starts) and does not interact with line transparency: a ghost multiplies the line's own alpha
by the property.

**Not measured.** Android and iPhone GPUs (tile-based, where re-drawing a layer after the 3D pass can force an
extra resolve of the depth and stencil attachments) were not measured: the numbers above are one desktop GPU.
Treat the ratios as an indication, and measure a route with arrows on the target device before enabling it on
more. The numbers came from a Mac Catalyst demo harness with temporary timers, which are not in the SDK.

The renderer side is in [the frame](/docs/internals/rendering/frame#the-ghost-pass-line-occlusion-opacity).

## Styling differently in 3D

A style reads `[render::3d]` to branch on whether it is drawn on terrain — flat labels in 2D,
billboard ones in 3D, for instance. See
[Live Style Parameters](/docs/features/style-parameters#the-render-mode-variable-render3d).

## See also

- [Hillshade](/docs/features/hillshade) — shaded relief from the same DEM.
- [On-the-fly Contours](/docs/features/contours) — contour lines that drape over terrain.
- [Composite Vector Tile Layer](/docs/features/composite-vector-tile-layer) — mix terrain-aware sources into one style.
