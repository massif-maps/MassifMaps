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

**Below 6 m per texel the gradient is the normal map's.** The quadratic term takes second differences
of the DEM, which amplify texel noise (white noise of variance s2, CPU model: the gradient's variance
is 1.04 s2 on average and 2.04 s2 at a texel edge, against 0.11 s2 for bilinear Sobel/8), and it jumps
at every texel edge across the axis it does not differentiate. On a 0.4-1.2 m DEM (Mapterhorn z16-17,
lidar) this reads as speckle and facets that the 2D path, which interpolates Sobel/8 normals, does not show.
`uPaintParams.z` (`terrainPaintSmoothFlag`) switches `terrainPaintSampleSmooth`: Sobel/8 at the four
texel centres round the fragment (a 4x4 block, 16 fetches, hence the 2-texel texture border),
bilinear between them, and `elevation` bilinear too. Coarser DEMs keep the quadratic, which follows
features of 4-8 texels more closely (CPU model, clean DEM, slope amplitude 0.4: RMS slope error 0.07
and 0.02 against 0.18 and 0.08 for Sobel/8) and costs 9 fetches. With 0.15 m white noise on a
smooth DEM, slope error quadratic / Sobel: 0.088 / 0.032 at 2.4 m, 0.25 / 0.082 at 0.84 m, 0.50 / 0.16 at 0.42 m; shading
roughness (RMS second difference) 12x lower at 0.84 m. Not measured on device.

Two things the port had to get right:

- **The relief boost follows sampling density, not a tile id.** The low-zoom boost was keyed off tile
  zoom × bitmap resolution; the terrain's grids are 516² at z11 (38.2 m/texel, the density of an old
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

## Slope units against MapLibre

`HillshadeRasterTileLayer::createVectorTile` scales the DEM into tile pixels and boosts it below z15
exactly as MapLibre's `hillshade_prepare.fragment.glsl` does; `NormalMapBuilder` bakes the Sobel/8
slope and the Mercator `cos(lat)` into the normal. At `heightScale` 1 the normal therefore carries
MapLibre's derivative, which `tests/vt/NormalMapSlopeTest.cpp` pins. The lighting shader
(`TileRenderer::LIGHTING_SHADER_NORMALMAP`) is MapLibre's `hillshade.fragment.glsl` with
`u_intensity` = the layer's contrast = MapLibre's `hillshade-exaggeration`, and `u_exaggeration` an
extra multiplier MapLibre does not have. The mapping for apps is in
[the feature page](../../features/hillshade.md#matching-maplibre).

**The default `heightScale` is 0.05**, a twentieth of MapLibre's slope, chosen by eye for the demo's
IGOR shading over imagery. A style tuned in MapLibre and copied as `exaggeration` draws twenty times
flatter.

**MapLibre changed its own strength.** Up to 5.1, `getElevation` returned `dot(data, u_unpack) / 4.0`
and the slope was `atan(1.25 * length(deriv))`; with the hillshade methods it lost the `/ 4.0` and
became `atan(0.625 * length(deriv))`. The newer one is twice as strong for the same paint. The SDK
ports the newer one.

Measured in the style preview (web build, MapLibre 5.24 pane beside the SDK pane, 1024×694 readback
right after each frame; relief = mean luminance the hillshade takes off, with it minus without,
Matterhorn; correlation of the two relief maps):

| scene | view zoom | DEM zoom SDK / MapLibre | MapLibre | SDK | ratio | correlation |
|---|---|---|---|---|---|---|
| white ground, hillshade only | 13.2 | 13 / 13 | 55.01 | 54.71 | 0.995 | 0.989 |
| full Massif outdoor, `#hillshade` slot | 13.2 | 13 / 13 | 33.24 | 31.48 | 0.947 | 0.922 |
| full Massif outdoor, `#hillshade` slot | 12.6 | 12 / 13 | 26.54 | 28.41 | 1.07 | 0.932 |

The last row is the tile zoom: at `tileDrawSize` 512 the SDK floors the view zoom where MapLibre
rounds it, so between x.5 and x+1 it shades one DEM level coarser, with the larger below-z15 boost.
Kept on purpose: rounding would change which tiles every raster layer loads.

**A composite hillshade slot draws exactly like a stand-alone layer**: the same values give a
bit-identical frame. The "composite is 5× weaker" report of 2026-09-30 was the style preview's
stand-alone layer, built with no `dem_encoding`: the Terrarium DEM went through the Terrain-RGB
decoder (base unit 0.1 m against 1/256 m), 25.6× the slope.

**Slot colours were dropped.** The CartoCSS translator writes a colour as `rgb(r,g,b)` /
`rgba(...)`, and `CompositeVectorTileLayer` only read `#hex`, so every `hillshade-*-color` fell back
to black / white / black: grey shading, up to 1.44× darker than the brown the style asked for (255
against 177 luminance of headroom). It now reads them through `mvt::tryParseColor`, which returns
`rgba()` premultiplied — the layer divides that back out. The facade still reads only `#hex` or an
ARGB number, so `massif:sdk-layer` writes hex.

**Two measurement traps**, both of which produced numbers that looked like findings:

- `tileDrawSize` changes the SDK's view SCALE, not only the tiles it picks: at 256 the SDK at a
  given zoom number shows a level more ground than MapLibre at the same number (correlation 0.36
  against 0.98 at 512). An SDK/MapLibre comparison is valid only once the two relief maps correlate.
- Mean darkness depends on how much steep ground is in the frame, so numbers from panes of
  different sizes do not compare.

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
