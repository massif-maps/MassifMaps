---
title: Hillshade & contours
description: Normal maps, shading methods, on-the-fly contour lines, contour labels and hypsometric tint.
sidebar_position: 7
---

# Hillshade, contours, hypsometric tint

Scope: everything painted *from* the elevation data rather than from vector geometry.

The design principle is tangram's: `res/scenes/hillshade.yaml` computes hillshade, contour lines and
the hypsometric tint in the `color:` block of the terrain raster draw the renderer is already doing —
**zero extra tiles, zero extra draws**. Ours converges on that.

## Why it matters

Every layer with its own tile set multiplies the frame. Measured on the north pan, render tiles per
one-second interval:

| configuration | render tiles | surface draws |
|---|---|---|
| base only | 132 | 265 |
| base + hillshade (as its own tile layer) | 693 | 926 |
| base + hillshade + contours | 492 | 618 |

Adding the hillshade as a tile layer multiplied render tiles ~5×.

## The terrain paint

A `HillshadeRasterTileLayer` in **paint mode** holds no tiles at all. It shades the elevation texture
the terrain has already bound, as one quad (or, under the shared ground, one grid draw) per terrain
tile, at its own position in the layer order. What disappears: the DEM tile set with its cull, fetch,
decode, normal-map build and upload, its stencil mask and its share of the render tiles.

- `vt`: `GLTileRenderer::setTerrainPaint`, `renderTerrainPaintSurfaces`, the `terrainPaint*` shaders.
- `all/native`: `HillshadeRasterTileLayer` decides paint mode, `TileRenderer::setTerrainPaint`
  carries it, `TileLayer::drapeStackSignature` watches its appearance.

**The lighting is the same code**: the normal-map lighting shader (built-in or a custom one) is
injected over a prelude that reads the terrain DEM and is handed a normal rebuilt from the DEM
gradient. All hillshade methods, colours and custom `getElevation()` shaders work unchanged.

### When it engages

3D terrain, and the layer's data source is the terrain's own. Anything else keeps the normal-map tile
path: a different DEM must not be silently replaced by the terrain's.

**It is not pixel-identical, by construction.** The sampling is the terrain's elevation grid, so the
layer's own zoom bias no longer reaches it, and the gradient is recomputed per drape/ground texel in
floats instead of interpolating an 8-bit-packed 256² normal map — crisper, and blockier where the DEM
grid is coarse. A custom shader reading `getRawColor()` sees the terrain's re-encoded DEM texel;
`getElevation()` is the portable one.

Device measurement with a background-only style (`--es minimal true`, north pan, interleaved):
**22.0 fps against 17.0 for the normal-map path**, frame 38.7 vs 49.9 ms. With a full style the two
are a wash, because the base map's own geometry is the frame; the hillshade's cost there is tile
loading, which the frame timer does not see.

Two things the port had to get right:

- **The relief boost follows sampling density, not a tile id.** The low-zoom boost was keyed off tile
  zoom × bitmap resolution; the terrain's grids are 514² at z11 (38.2 m/texel, the density of an old
  z12 256² tile), so keying off the grid's own zoom made the paint ~1.5× too strong. It now derives
  the zoom from metres per texel.
- **A paint has no per-tile fingerprint.** Its appearance rides `TileLayer::drapeStackSignature` and
  it reports no tiles at all. Reporting the previous frame's cover instead made every tile that had
  just entered it look incomplete (surface draws up 12%).

### The DEM level

The terrain caps the elevation grid at what the **mesh** can express, which drops two zoom levels.
Shading is per fragment and resolves far more than that, so on the paint the cap is visible as blur
from z15 up. `ElevationTextureCache::requestDetailLevels(n)` lifts it by `n` levels
(`ElevationManager::getDetailDataTile`), asked for by a paint with
`HillshadeRasterTileLayer::setTerrainPaintFullDetailEnabled` (default true) — but `n` is
`TileRenderer::DEFAULT_PAINT_DETAIL_LEVELS` = **0**, so by default shading and geometry read the
same elevation tile, tangram's arrangement. The texture pipeline cannot pay for more: full detail
measured 2.5 fps against 6.7 on device, with the working set jumping ~16× past the cache (96
textures then, `MAX_CACHED_TEXTURES` = 128 now). `debug.massif.paintdetail 0..4` sets `n` in demo
builds ([runtime switches](10-performance.md#runtime-switches-no-rebuild)). Fixing the cost is the
elevation-texture port described in [04-terrain.md](04-terrain.md#the-elevation-texture).

## Contour lines

Drawn as a fragment block on the terrain draw, from the same DEM: distance to the nearest contour in
metres divided by the per-pixel elevation change (`fwidth`), giving a screen-width anti-aliased line
(`u_contourInterval`, `u_contourWidth`, `u_contourColor`). Measured **free**: 7.25 fps without
contours against 7.57 with.

Turning them on for a layer that would otherwise fall back to its own DEM tile set moved render
tiles 494 → 216 and `layers` 18.4 → 16.1 ms.

**In a composite base the settings come from the style**, not from the `HillshadeRasterTileLayer`
setters — those only reach a stand-alone layer. The style properties are
`hillshade-contour-interval`, `hillshade-contour-width`, `hillshade-contour-color`
([09-composite-layer.md](09-composite-layer.md)).

## Which contours a traced tile carries, and which of them are drawn

Two separate decisions, and confusing them empties the map.

Both are app settings now, not constants: `setIntervalMultiplier(maxZoom, multiplier)` and
`setResolutionForZoom(maxZoom, resolution)`, each a table of `(maxZoom, value)` rungs with `-1` for
"everything above". Defaults: interval `(9, 50) (11, 10) (13, 5) (any, 1)`, resolution table **empty**.

`getIntervalForZoom` decides what the tile **carries**. It is a cost rule:
a low-zoom tile covers a huge area and its DEM is sampled far too coarsely to place a 10 m line
meaningfully. Two constraints on the rungs:

- **They must nest** — every interval a multiple of the finer one. 200 m and 500 m share no
  elevation, so a z10 tile's 600 m line had nothing to meet in the z9 tile beside it and stopped
  dead at the border. `10 | 50 | 100` nests.
- **They must stay usable when zoomed out.** The original ladder went to 50× the base (500 m) at
  z ≤ 9, which is two or three lines on a mountain: zoomed out, and across the whole far half of any
  tilted frame (those tiles are z6–z9), the map read as *no contours at all* while the hillshade
  drawn from the same DEM stayed fully detailed. That was reported as a rendering bug; it was the
  ladder. Now 50×/10×/5×/1× over z9 / z11 / z13 / above — measured on a mid-range phone at ~10% less
  tile-generation CPU than a uniform fine ladder, for the same picture under a `div`-filtered style.

The **grid** is a different matter and must NOT scale with zoom. A tile is drawn at roughly the same
screen size whatever its zoom, so the grid is what fixes the shape on screen: at z9 a 48-sample grid
puts contour vertices 1.6 km apart and the far half of a tilted view — which is made of exactly those
tiles — reads as long straight chords. Uniform 128 measured both **faster and smoother** than the
DEM's own 512 (12.0 vs 14.7 CPU-seconds over 25 s, and contours complete at t=4 s instead of blank).

What is **drawn** is the style's decision, per camera zoom, keyed on `div` (the largest nice divisor
of the elevation — 1500 → 500, 250 → 250, 130 → 10), exactly as the pre-baked tileset is filtered.
It has to be a **width (or opacity) ramp**, not a filter: a CartoCSS filter is evaluated per tile at
decode time and cannot see the camera, while `linear([view::zoom], …)` is evaluated per frame. A
width of 0 draws nothing — the quad is degenerate — so the pattern is

```
#contour {
  line-width: 0;
  [div>=10]  { line-width: linear([view::zoom], (13, 0), (13.5, 0.8)); }
  [div>=50]  { line-width: linear([view::zoom], (11, 0), (11.5, 1.0)); }
  [div>=100] { line-width: linear([view::zoom], (8.5, 0), (9, 1.2)); }
  [div>=500] { line-width: 1.6; }
}
```

**Diagnosis trap:** two frames at different zooms that look *identical* are not proof the ramp is
dead — below z9 only the `div>=500` rank has a width, so every frame from z6 to z8 legitimately
shows the same lines. Take the positive control at z13.8, where the finest rank fades in.

## Contour labels without contour geometry

Contour *lines* are free, but labels need something to lay text along. Tangram generates that from
the elevation texture (`core/src/style/contourTextStyle.cpp`) and carries no contour geometry, no
contour source and no contour tiles at all. That generator is ported into
`ContourTileDataSource` as **label stubs** (`setLabelStubsEnabled`):

- a 4×4 grid of seeds per tile, aligned across zoom levels so a label does not jump when a finer
  tile replaces the one it came from;
- each seed walks **down the elevation gradient** onto `round(elev/interval)·interval` — at most 12
  iterations, interpolating straight onto the level once it is bracketed, to a position error of
  `0.25/256` of a tile;
- then along the contour **tangent** in steps of `2/256` until the stub is `1.25 × 32/256` long —
  about 20 points, exactly enough to carry the text.

The features keep the layer name and the `ele`/`div` attributes, so existing `#contour` **text**
rules style them unchanged, and they carry `stub` (1 for a stub, 0 for traced geometry) so a style
keeps its **line** rules with a `[stub=0]` filter. Both modes set the attribute: an undefined
attribute does not compare equal to 0, so a one-sided property would silently drop the traced lines.

Emulator counters at the ridge camera: geometry draws 2035 → 1217, indices 51.6M → 29.4M, render
tiles 1197 → 712. Device A/B still to take.

**The trap:** the stub levels must be the levels the shader draws, or labels sit between the lines.
Set `LabelInterval` to the layer's contour interval (tangram carries the same warning in their
source).

## Hypsometric tint

Currently a `CustomRasterTileLayer` with a shader over the DEM source (the demo's
`DemoStyles.hypsometricShader()`). It has **not** been converted to a paint kind; doing so is the
same quad and the same prelude as the hillshade paint, and is the obvious next step for it.
