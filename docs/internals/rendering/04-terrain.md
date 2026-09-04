---
title: 3D terrain
description: Elevation data, tile surfaces, and the shared ground pass that replaced the RTT drape.
sidebar_position: 4
---

# 3D terrain: elevation, surfaces, and the shared ground

Scope: how the ground is built and drawn. Depth relationships are in
[05-depth-model.md](05-depth-model.md); shading of the ground is in
[07-hillshade-contours.md](07-hillshade-contours.md).

## Elevation data

`ElevationManager` (all/native/terrain/) owns decoded `ElevationTileGrid`s (one per DEM tile) in an
LRU cache and answers height queries: `getTileGrid`, `getDisplayHeight`, `getMinMaxDisplayHeight`,
with a `LoadMode` (`CACHED_ONLY` never blocks). Every consumer — the mesh, label anchoring, element
placement, billboard occlusion — must go through it, or two parts of the frame disagree about where
the ground is.

**The grid cache is a tile count, not a byte budget.** A grid is the source raster (768 KB for a
512×512 RGB DEM, 192 KB for a 256×256 one), so a fixed 64 MB meant 85 grids for one source and 340
for another. One terrain view needs 122–167 of them — the cover pyramid, the contour source's finer
tiles, the border prefetch — and every grid past the limit evicted one still in use, which was then
decoded again on the next pass: **1525 loads of 167 distinct tiles, 32 s of WEBP decode per
startup** on a Crosscall. The cache now grows on the first decoded grid to hold `MIN_CACHED_GRIDS`
(192) of them, i.e. 144 MB for a 512² source and 36 MB for a 256² one; 192 was the first value where
loads equalled distinct tiles (128 still re-decoded ~20%). `TerrainOptions::setElevationCacheCapacity`
still wins over the rule, for an app that cannot spend the memory.

**A decoded tile asks for a frame.** Every consumer reads the elevation version from *inside* a
frame (`TileRenderer::onDrawFrame` compares it and invalidates the surfaces it covers), so a tile
that lands after the last drawn frame is never applied: the map goes idle on a half-displaced mesh —
cracks at LOD rings, flat far ground — and only catches up on the next gesture, which is why "pan a
pixel and it fixes itself" was the symptom. `ElevationManager::setDataChangedListener` closes that:
`MapRenderer` hooks it on the terrain path and every decoded grid requests a redraw. Measured on the
3D-terrain example, cold: frames stopped at elevation version 43 while loading ran on to 159; with
the listener the last frame follows the last decoded tile by 146 ms and has consumed it.

`setSurfaceResolution` no longer caps the elevation level (it did, at one texel per half cell, and
the cap blurred the hillshade). It sizes the **node field** every decoded grid carries instead —
the DEM box-filtered to one mesh cell, which is what the surface is displaced from and what every
display-height query answers with. See [The node texture](#the-node-texture).

**Which DEM tile a render tile uses** is tangram's rule verbatim
(`RasterSource::addRasterTask`): `subTileID = tileId.zoomBiasAdjusted(zoomDiff).withMaxSourceZoom(maxZoom)`
— the render tile's own z/x/y adjusted by the source's zoom bias (one level per doubling of tile
size, since a 512-texel tile at z−1 has a 256-texel tile at z's density), capped by the source's max
zoom. Nothing else: no cap against what the mesh can express, and no detail dial on top. Those were
this fork's, and they are what made the hillshade blurry.

`clampTileZoom` is deliberately **not idempotent** — it drops the bias on every call. Applying it to
an already-resolved elevation tile (`getTileGrid` on a `getDataTile` result, or on a neighbour of
one) costs another level per hop, which is why the elevation-tile entry points call
`clampDataTileZoom` instead.

### CPU height queries

`getDisplayHeight` answers with the node field (the drawn surface), `getElevationMeters` with the
DEM — see [The node texture](#the-node-texture). Both are point queries, and their callers are dense: the label
re-anchor walks every vertex of every label, the raycast marches a ray. Three things make one sample
cheap, and each of them was a measured frame cost before it existed:

- **The last grid, kept.** `getGridForInternalPos` remembers the last resolved grid per thread and
  reuses it while the point is inside its bounds. Without it every sample paid a projection
  transform, a tile id (`IntPow` alone was 21% of the render thread), a flip and the zoom clamp
  before reaching the cache — to find the tile the previous sample had just used. `lookupTileGrid`
  keeps a second memo for the callers that arrive with a tile id already.
- **The latitude scale, quantised.** `getDisplayScale` is `tanh`-based and was 21% of the render
  thread on its own (`tanh` + `expm1`). It is now memoised over a ~40 m latitude quantum, which
  moves a height by under two millimetres and — being a function of the position alone — keeps the
  same vertex at the same height frame after frame.
- **Two versions, and what they mean.** `getVersion` moves on *any* change; `getDataVersion` moves
  when the elevation DATA changes, **including a tile load**. A consumer tells an exaggeration ramp
  (heights scale on the GPU, surfaces stay valid) apart from new data by comparing the two.
  `_dataVersion` used to stand still for tile loads, so `TileRenderer` read every arriving DEM tile
  as scale-only and took the blanket invalidation path — a whole screen of label anchors resampled
  per tile, instead of the labels over that tile. Setting the same exaggeration twice is now a
  no-op for the same reason.

### The elevation texture

Displacement happens in the **vertex shader** (vertex texture fetch), so each tile needs its DEM as
a texture. `ElevationTextureCache` (all/native/renderers/utils/) turns a grid into one:

- keyed by the **grid's own tile**, so overzoomed tiles and all layers share one texture per DEM
  tile, and neighbours sampling the same level sample one continuous texture;
- the payload is a **padded (W+2)×(H+2) RGBA re-encode** with a 1-texel border taken from up to 8
  neighbour grids (cross-level backfill and an edge box filter), so shared tile edges agree
  bit-exactly and the surface does not crack;
- encoding **and** the `Bitmap` construction run on a worker thread; the render thread only uploads,
  under a per-frame budget (`MAX_UPLOADS_PER_FRAME`, `MAX_UPLOAD_MS_PER_FRAME`). A tile with no
  texture yet renders flat, which is the visible cost of a budget set too tight;
- a neighbour landing patches only the **2-texel ring** (`encodeTextureBorders` →
  `applyBorderPatches`, `glTexSubImage2D` into the live texture *and* into the bitmap behind it,
  which is what survives a context loss). Measured over a cold load: full re-encodes 353 → 24. It
  changes no frame rate — the encode was never on the render thread — so treat it as work removed,
  not speed gained. In a warm pan the whole pipeline is **idle**: zero encodes, zero patches;
- `_frameResolved` memoises the per-frame tile → grid resolution, because the provider is called
  once per tile **per render pass** and each miss costs 9 locked cache lookups.

**How tangram does it:** the DEM raster is bound as the tile's own texture, uploaded once when the
tile loads, ancestors addressed through uv sub-rects (`u_raster_offsets`), edges extrapolated in the
shader (`res/scenes/elevation.yaml`). No re-encode, no border machinery — but also no cross-level
edge filtering, which is a seam feature this fork wants. The port that keeps both is to upload the
grid's own samples and patch borders as small `glTexSubImage2D` strips.

### The node texture

The vertex stage does **not** displace from the texture above. It samples a second, small texture
per DEM tile — the **node texture** — holding one height per mesh node: the DEM averaged over a
box of `ElevationNodeField::DEFAULT_BOX_CELLS` (two) mesh cells centred on the node
(`ElevationNodeField`, `ElevationTileGrid::encodeNodeTexture`). 129×129 texels for a
512-texel grid under a 64-cell mesh, ~50 KB, same encoding as the DEM so the same decode uniforms
read it (`uElevationNodeTexture`, `uElevationNodeUV`, `uElevationNodeTexelSize`; the lattice clamp's
`uElevationLatticeCell` is in node-uv units).

Why: a lattice that samples the DEM point by point **aliases** every relief finer than its cell. On
a lidar-grade DEM (mapterhorn z16, 0.84 m texels) under a 6.7 m cell that is a road's cut and fill,
a wall, a terrace — and every road edge at a grazing tilt came out as a regular sawtooth with the
cell's period, plus a one-sided dark band (the cut face). Measured at Grenoble z16 33810/23522: the
64-lattice's residual against the DEM was p95 0.80 m, p99 1.53 m, max 13.7 m, and the >0.5 m
residuals traced the switchbacks as double lines. `demMaxZoom 13` made it vanish, 14 halved it;
`meshResolution 254` shrank the period 4× and kept the amplitude. The fragment stage (shading,
contours, the paint) keeps the full texture, which is what PR #49 removed the old level cap for.
Camera to judge it at: `--es lat 45.201650 --es lon 5.727543 --es rotation -15.12 --es zoom 16.27
--es tilt 20`, mapbox-standard, on emulator-5554.

**Why two cells and not one.** A node sits on a texel boundary, so the old bilinear at a node was
already a 2-texel box; the runs above were therefore a half-cell box (default), a one-cell box
(`demMaxZoom 14`) and a two-cell box (`demMaxZoom 13`) — and a one-cell node field rendered
pixel-identical to the `demMaxZoom 14` run (mean difference 0.01), still stepped. A one-cell box
removes what the lattice cannot sample but leaves a road's cut as a full step within one cell, and
a step of H over one cell is drawn as a staircase of H/2 at a grazing tilt. Roughness of the node
field (p95 of the per-cell Laplacian, Grenoble z15 DEM, mesh 64):

| box | half cell (old) | 1 cell | 2 cells | 4 cells |
|---|---|---|---|---|
| p95 | 5.0 m | 3.75 m | 2.36 m | 1.20 m |

Two is where the staircase stops reading as one; wider trades real relief for it. The box scales
with the cell, so a finer mesh gets its detail back. `adb shell setprop debug.massif.nodebox <cells>`
overrides it for a measurement.

Why not mipmaps of the elevation texture: the texture is padded to 514, and from level 2 up the
texel centres no longer sit on the tile edge, so two tiles sharing a DEM border would compute
different edge-node heights — a crack at every DEM tile border. The node texture puts a texel ON
every node, and the edge nodes are computed from both grids (a same-level neighbour texel-exactly,
a coarser one sampled at the texel centre, and widened to the coarser neighbour's cell so our node
meets what its lattice interpolates), so both tiles get the same number. A neighbour landing
patches the four edge rows/columns along with the DEM ring.

At the nominal zoom a surface vertex is a node and reads one texel; an overzoomed tile's vertices
interpolate the field bilinearly, so relief between nodes is not recovered there (the mesh could
not carry it without aliasing anyway). The CPU side samples the same field
(`ElevationTileGrid::sampleNodeHeight`, behind every `getDisplayHeight`), so label anchors,
extrusion bases, the raycast and the occlusion depth mesh sit on the drawn ground; its edge nodes
clamp where the texture reads the neighbour, a fraction of a texel step along DEM tile edges only.
`getElevation`/`getElevationMeters` stay the DEM itself. The full-detail terrain paint cache
(`setDetailLevels`) reuses the manager's grids, whose field is built for the nominal level, so in
that off-by-default mode its node texture is twice as dense as its mesh and filters half a cell.

mapbox never meets this: GRID_DIM 128 over a maxzoom-14 512-texel DEM is one texel per node at
z16 by construction of the data, not by a rule.

### The decoder is per tile, not per source

`ElevationManager::loadTileGrid` resolves the decoder from the TILE's `dem_encoding` meta data
(`ElevationDecoder::Resolve`, see [02-tiles.md](02-tiles.md)), falling back to the source's and then
to MapBox. Everything downstream already carried the coefficients per grid — `ElevationTileGrid`
keeps `_coeffs`, hands them to `sampleHeight` and to the GPU through `getDecode()` — so a mixed
`OrderedTileDataSource` of a MapBox DEM and a Terrarium one needs nothing else.

One place had to learn about it: the border backfill copies a same-level neighbour's texels
**bit-exactly**, which is only valid when the neighbour encodes heights the same way. `sameLevel`
compares `_coeffs` for that reason; a differently encoded neighbour falls to the resample path,
which goes through metres and re-encodes. Without the check the seam between two coverages reads
whole kilometres of bogus elevation, and only there — which is exactly the kind of bug that gets
blamed on the tileset.

## Surfaces

Two mechanisms exist; **regular-grid mode is what runs**, and it is no longer optional — the
adaptive path is only reached on a GPU without vertex texture fetch (no elevation texture provider):

- **Shared regular grid** (`TileSurfaceBuilder::buildRegularGridSurface`): ONE unit grid of
  `TerrainOptions::MeshResolution` cells, built once, drawn for every tile with the tile's own
  matrix and uniforms. `surfIndices / surfDraws` comes out at exactly `24576 = 64·64·2·3` — one grid
  per draw. This is tangram's `RasterStyle` arrangement (`core/src/style/rasterStyle.cpp`), and it is
  already implemented: there is nothing left to port here.
- **Per-tile adaptive surfaces** (`buildTileSurface`, red-green edge-local refinement over corner
  fans) — used only by the non-grid draw path and by ray-cast picking
  (`findTileBitmapIntersections`). In grid mode `_tileSurfaceMap` stays **empty**, and both
  `invalidateTileSurfaces` and `resetTileSurfaces` iterate nothing. Verified with counters:
  `surfBuilt=0 surfInval=0` for a whole pan.

Because the grid is regular, it also means picking through `_tileSurfaceMap` finds nothing in grid
mode — the pick path is the one consumer left that would need a lazily built surface.

`TerrainRenderer` keeps its own mesh cache keyed by `(tile id, mesh grid size)` — the occlusion
depth pass draws the same tiles at a coarser grid, and a tile-only key would make the two passes
rebuild every mesh in turn. It evicts **least-recently-used**, sparing anything the current pass
already drew. It used to `clear()` the whole cache on overflow, which rebuilt every mesh of every
pass whenever the working set crossed the cap — i.e. exactly during a multi-level zoom, for the same
reason `ElevationTextureCache` had already moved off a full flush.

### Edge stitching

A coarser neighbour interpolates the DEM between its own (2^k wider) lattice nodes, so a fine tile
must chord across the same nodes on that shared edge or the seam cracks open.
`buildTerrainEdgeCoarsening` computes, per tile, how much coarser each of its four neighbours is, and
the surface shader collapses the edge accordingly. Two things this got wrong once:

- the map must be built from **the cover that is actually drawn**, not from a layer's own visible
  tiles (`GLTileRenderer::terrainSurfaceTileIds`: ground cover, else paint cover, else own tiles);
- the lattices only line up when the resolution is a multiple of the level difference, which caps k.

Draped **content** takes the same coarsening, not just the surface: a road or a contour crossing the
seam has to land on the same stitched edge as the ground it lies on, or its two halves meet at
different heights — invisible from straight down, a step as soon as the camera tilts. The edge test
is `pos.x < 0.00001`, which is only meaningful for surface vertices (they *are* the unit square), so
content converts first with `uTileUnitScale`. Only the outermost cell is affected. Note the feature
is off by default (`TerrainOptions::TileEdgeStitching`), and on its own it does not fix content
mismatches at junctions — see the tile clipping in
[03-vt-renderer.md](03-vt-renderer.md#lines-over-terrain), which is the dominant cause.

**Open:** this conversion uses the scale alone, so for a **stand-in** (an ancestor tile serving a
finer target while it loads) it measures from the ancestor's origin rather than the drawn tile's,
and picks the wrong edge. The tile clip had the same bug and was fixed with a `uTileUnitOffset`; the
same offset applies here by the same argument, but adding it moves settled contour positions by
changing the elevation interpolation (2.8 % of the frame at the camera above), so it is left for a
deliberate on-device comparison rather than folded into the clipping fix.

### Skirts: deliberately absent

Tile border skirts (walls dropped at tile edges to hide cracks) are **disabled**. Their walls,
textured with stretched edge pixels, rasterize over neighbouring content wherever a displaced tile
edge leans off-nadir — solid fill-coloured patches that grow with the tile size. Tangram has none
either. Cross-LOD cracks are handled by stitching instead.

## The shared ground

One cover for the whole layer stack, one ground pass, then every layer composites onto it in layer
order. This replaced a per-layer depth pre-pass and a per-layer stencil mask
(4209 mask draws and 24.7 ms per interval → **0**), and it is what tangram does: one shared grid
mesh, one draw per tile, no pre-pass, no masks anywhere in `core/src`.

The cover comes from `MapRenderer::collectTerrainCover` and is seeded by the terrain's own visible
tiles — what the camera can see, not what the layers happen to have fetched. Two rules hold it
together:

1. **The ground is drawn at its true depth and is never pushed back.** Everything after it is
   `GL_LEQUAL` with no bias in either direction.
2. **Ground-shaped content is drawn on the cover tiles, not on the layer's own tiles.** Two
   tesselations of one height field do not agree; on the cover they are coincident to the bit. This
   is why a hillshade at z12 is drawn on the z14 cover.

### Stand-ins

A cover leaf whose DEM has not arrived walks up to the coarsest **loaded** ancestor and is drawn
there once (duplicates collapsed). Drawing it flat instead makes every tile flash in the bare ground
colour during a zoom. Tiles that stand in carry a proxy depth; live tiles carry zero, however coarse
they are — see [05-depth-model.md](05-depth-model.md#proxy-depth).

A tile with **no** elevation anywhere in its ancestry draws nothing at all. A false ground at sea
level next to displaced tiles is a slab of map hanging in space, and it writes depth, so it hides
what is behind it. Zooming out is when that happens wholesale: the elevation cache holds the finer
grids of the previous generation and its lookup only ever walks *up*, so every new coarse tile
misses until its own DEM tile loads. When *nothing* has elevation (cold start, or ground the DEM
does not cover) the scene is flat and internally consistent, so the flat draw stays.

### Normalizing the cover to a quadtree partition

The collected set is a **union across layers**, and layers do not agree on a zoom level — a
hillshade capped by its DEM max zoom yields coarser tiles than a vector tile layer. Drawing a
surface for every tile in that union stacks a coarse surface and the finer ones covering the same
ground, and they fight. `collectTerrainCover` normalizes to a single non-overlapping cover, keeping
the **finest** tile for any ground area; coarser layers still reach it through the ancestor sub-rect
bake.

Dropping a coarse tile outright is wrong — a single fine tile inside it covers 1/4ⁿ of its ground
and the rest would have no surface at all, which reads as a hole. So a coarse tile that *contains* a
finer one is replaced by its four children, recursively, giving a true quadtree partition.

**Split only where a finer collected tile actually sits inside.** Splitting every subtree down to
one global level looks stable on paper, but a layer showing a coarse proxy — one z6 tile standing in
for the whole view while its data loads — is then chopped into hundreds of leaves, most of them
off-screen ground nobody asked for: measured, **16 collected tiles became 127 leaves**. That is
fatal rather than wasteful, because every leaf takes a drape cache entry: two such covers exceed the
cache and the eviction pass drops the *entire* previous generation, which is what both the seed and
the stand-in read from. The symptom is `seeded 0, blank 16` and a screen of flat fills.

**...but the camera seeds the levels the data does not reach.** Built from the collected tiles alone
the cover cannot split past the deepest tile a source gave, so once the camera zooms past a source's
maxzoom the drape's metres-per-texel freeze: the ground goes soft and *stays* soft while the live
geometry beside it keeps its full precision. A drape texture is a fixed size
(`TileRenderer::resolveDrapeResolution`, one per frame), so the only thing that buys sharpness at
depth is a finer leaf.

`collectTerrainCover` therefore also takes the terrain's own camera-driven cover as a seed. The
shared ground takes it whole — it has no texture budget and needs the view covered, which is what
stops it blinking white on a zoom out. The drape takes it with `extendSeedsOnly`: only the seeds
that reach *deeper* than any collected tile, and none at all when the layers have nothing yet. Where
the data already follows the camera the cover is byte-for-byte what it was; the seed pays for the
extra depth and nothing else.

Measured, emulator, 44.0804/3.0037 z15.08 t19 rot130, `mapbox-standard` over mapbox streets:

| source maxzoom | split level | leaves |
|---|---|---|
| 16 (data reaches the camera), before and after | 15 | 11 |
| 14, before | 14 (the `min(maxCollectedZoom, …)` cap) | — |
| 14, after | **15** | 11 |

So the cover no longer depends on where the source stops: 0.86 m/texel at that camera either way,
against 1.72 m/texel when it was pinned to z14.

This is mapbox-gl-js's model with our own cover standing in for their proxy source — theirs is a
`TerrainInternalSource('proxy', 'geojson', 512, 0, ceil(map.transform.maxZoom), reparseOverscaled)`
(`src/terrain/terrain.ts`), a source of its own so that no data source's maxzoom bounds it, drawn
into a `tileSize * 2` = 1024² buffer. Worth knowing before reaching for their model for anything
else: their cover caps at `floor(camera zoom)` exactly as ours does (`shouldSplit` returns false at
`it.zoom === maxZoom`), and 1024² over a 512 px tile is *coarser* on the ground than our
`2 × tileDrawSize × dpiScale` over a 256 px one. Past the source max was the only place they were
ahead.

**...except that the byte budget was silently taking it back.** The ladder above is only the first
half of `resolveDrapeResolution`; the second caps it at the largest power of two a working cover
still fits in the drape cache's 96 MB (`DrapeTuning::resolution`). With `DRAPE_WORKING_SET` at 64
that cap was 512 on **every** device — 64 × 1024² × 4 B is 256 MB — so the screen's 1024 was asked
for and never granted, and the comparison above was wrong in our favour: we were baking at half
mapbox's linear resolution, a quarter of the texels, which is the blurry stretched drape at a
grazing angle. A real cover measures 15–34 leaves, so 64 was headroom nothing used. At **24** the
arithmetic lands on the budget exactly (24 × 4 MB = 96 MB) and 1024 gets through.

**Still open: the oblique near ground.** At a low tilt the ground at the bottom of the screen is
magnified several times past what a cover at `floor(camera zoom)` resolves, and neither model splits
deeper there — `--es drapeResolution 2048` visibly sharpens it, which is what says it is texel-bound
rather than cover-bound. The fix would be a per-tile resolution (near leaves large, far leaves
small, same byte budget) or a split rule with a pitch term; mapbox's `distToSplitScale` is not it,
that one makes grazing tiles *coarser*.

### The drape cache: budget, seeding, and completeness

`TerrainDrapeCache` keeps a generation of tiles alive past the visible cover, because a zoom or a
pan walks the cover back and forth over the same tiles and re-acquiring means re-baking every layer
of every tile. Its cap is a **byte** budget, not a tile count: a drape texture is `resolution² ×
RGBA`, so the same 160 entries are 10 MB at 128 and 640 MB at 1024 — the count alone is how the
cache came to ask for hundreds of megabytes on a high-DPI screen. The tile count is derived from the
budget per resolution, with a floor so a large resolution still caches a usable cover.

Three things then keep the bakes off the critical path:

- **A per-frame bake budget.** A bake re-renders every layer of a tile into a full-resolution
  texture (~16 ms at 1024), so an unbounded loop over a churning cover is a per-frame re-render of
  the whole map. Three classes, not two: a tile with *nothing* is a visible hole and is baked almost
  freely; a tile standing in on an ancestor shows the right ground at half the sharpness; a merely
  out-of-date tile already shows something plausible and can wait. An integer zoom step renames the
  whole cover at once, which is exactly the second class. Raising the budget to bake a renamed cover
  in one frame was measured on device: worst frame **128 ms → 300 ms**, with no visible difference
  in the stand-ins it was meant to remove.
  Two rules on top of the classes (2026-09-03): within a class the tiles bake **nearest the focus
  first**, in tile lengths of their own zoom, so the point the user looks at fills before a corner;
  and the moving budget (16 ms) outlives the last camera move by a **300 ms settle window**
  (`DRAPE_BAKE_SETTLE_MS`) before the at-rest budget (60 ms) opens. A fast zoom is a chain of
  gestures with rests of a few frames between them, and opening the at-rest budget in each rest
  made every one a 60 ms frame while the new zoom's cover baked — watched live at Paris, z15–18
  fast: 100–144 bakes queued, 39 frames over 100 ms, the worst 375 ms, which read as the map
  hanging between the fingers. The deck's own drape (the span bake, per render tile) is under the
  same budget now, nearest first, one always through: unbudgeted, an integer zoom renamed every
  bridge tile in view and baked them all in one frame — 150–210 ms of `drape` in that frame at
  Paris, 43–71 ms after. A deck whose drape is not baked yet draws its plain roof for those frames.
- **Seeding.** A tile entering the cover has no texture and until its bake is budgeted it can only
  be a flat fill — the white sheet over the terrain on every zoom out. But the cache already holds
  this ground: the finer tiles it replaces, or a coarser one covering it. Copying those into the new
  texture is a few textured quads, and the tile shows the map from the frame it appears. Seeds are
  never *sources* (`findBaked` returns baked entries only), so nothing degrades through repeated
  copying.
- **Completeness, not just "has a texture".** A zoom out reaches the new coarse tiles raster-first —
  the hillshade decodes in one step while the vector tiles still have a style pass to run — so the
  first bake holds the hillshade and the background and nothing else. By a "has a texture" rule that
  replaces the previous generation still on screen, and the map turns into bare relief for the half
  second the vector layers need. That is **the flash**. A tile counts as usable only when every
  layer that has something for it is in it.

A bake pins the layer *blend* to 1 on purpose — a cached texture must not have a transient fade
burnt into it — but it also used to pin the style's own layer **opacity** to 1, so a draped layer
with `opacity: 0.5` baked fully opaque. `calculateDrapeOpacity` now supplies it, matching what the
on-screen path passes as element opacity. Comp-op layers keep 1: reproducing them needs the overlay
buffer the bake has no equivalent of.

Stand-ins from the previous generation are pushed **after** the tile's own entry, not before: the
surfaces coincide and the later draw wins, so pushed first they are buried under the fill they were
meant to replace — the whole screen going white for a moment on every zoom out. Several levels deep,
because one gesture crosses several zooms.

A leaf already drawing its **own** bake does not get the finer generation stacked on top of it even
when that bake is incomplete: it covers this ground and is merely missing a layer, with the re-bake
already queued. Stacking a finer tesselation over it was a two-frame mesh pop at every integer zoom
out (`showsOwnBake` in `MapRenderer`, same rule as `showsAncestor` above it).

### The outgoing generation at an integer zoom out

Zooming out z12 → z11, the drape cover moves to z11 while the z12 render tiles are retained and
faded out. `GLTileRenderer::isTileDraped` used to answer "draped" only for a tile that **covers** a
drape tile, so those finer tiles counted as undraped and kept drawing themselves in the 3D pass. Two
separate defects fell out of that, both fixed, both worth knowing about because the path is easy to
reintroduce:

- **The direct raster draw was neither lit nor fogged.** `renderTileBitmap` built its program with
  `PATTERN_FLAG | terrainFlag | fogFlag()` — no `TERRAIN_LIGHT_FLAG` — and it was the one draw path
  in vt that never called `setupFogUniforms`, so the fog code was compiled in with uniforms nobody
  ever wrote. `colormapFsh` had carried the full `TERRAIN_LIGHT` block all along as dead code. The
  visible result was the previous zoom's ground flashing **unshaded** over the lit drape beside it —
  measured as a uniform ×0.62 multiply on all three channels with the sky untouched, which is what
  a missing shading term looks like and what finally identified it. Passing the flag was not enough
  on its own: the block it enabled did not compile until its declarations were shared, see
  [08-lighting-sky-fog.md](08-lighting-sky-fog.md#a-compile-failure-this-uncovered-and-its-fix).
- **The old imagery was drawn at all.** Once shaded identically the flash was gone, but the z12
  raster was still painted over the z11 drape for the length of the fade — visible as the previous
  zoom's satellite imagery. `isTileDraped` now answers "draped" in **both** directions: a drape tile
  that *contains* the render tile covers that ground just as well as one contained by it.

The old comment argued the finer tile had to keep drawing because nothing else covered that ground.
That holds only while the covering drape tile has no content; vt cannot currently tell, so the risk
this trades for is a coarser stand-in (not a hole) for the frames a fresh cover tile needs. If a
blank patch ever appears right after a crossing, that is this trade, and the fix is to plumb
bake-completeness from `MapRenderer` rather than to restore the old imagery.

Adding the lit variant also moved the cost: it is only ever *asked* for at a crossing, so the lazy
build put a full compile and link of the largest colormap variant (DEM taps, PCF, cascades) inside
the gesture — a visible hang. `warmTerrainRasterShader`, called from `startFrame`, builds it on an
ordinary frame and rebuilds only when the flag set changes.

**Dead ends, in order, each killed by a measurement**: the elevation texture cache; the terrain mesh
cache; drape cover composition and its 257 ms lag (real, visually inert); fingerprint churn on finer
tiles; the seed blit; ancestor sub-rect stand-ins; coarse-fed bakes; absent layers; the day cycle;
raster blending speed; `viewZoomCap`; drape stack stability; the background overpainting the raster;
two raster generations baked at full opacity; and `TerrainRenderer`'s own background/surface/depth
passes — which turned out not to run at all once a tile layer owns depth-write
(`background=0 keepDepth=0 prepass=0 depthWriteAssigned=1`), and eliminating them is what pointed at
vt's direct draw.

## Extrusions on a slope

A building is a **prism, not a cloth**. Displacing every extrusion vertex by the terrain under it —
which is what tangram does (`position.z += getElevation()` in `terrain-3d.yaml`, per vertex, for
every style) — shears the roof down the hillside and the building reads as melted.

mapbox splits the two ends, and that is what is implemented here:

| | aligned to | result |
|---|---|---|
| base ring | the terrain under each vertex | the wall meets the slope everywhere, no gap, no float |
| everything above it | ONE elevation, resolved at the footprint's **centroid** on a smoothed field | the roof stays level; walls simply grow taller downhill |

The centroid rides in the **texcoord slot**, which was free because for an extrusion `_texCoords`
was a byte-for-byte duplicate of `_coords`. It is no longer read by the shader — the CPU pass below
reads it back out of the vertex data to know where to ask for the ground, and `polygon3DVsh` keeps
the position for the tile clip. `packGeometry` forces `texCoordScale == coordScale` for `POLYGON3D`,
which is what lets one scale convert either of them.

**The scale is fitted to the anchors as well as the coords** (`_polygon3DAnchorExtent`). Under deep
overzoom the centroid of a palace lies several tiles away from the z20 piece drawing it; at the
coords' own scale it overflowed the int16 and wrapped, so every tile read its base at a garbage
position — the same wing at two heights, a piece under the ground with no walls, only on 3D
terrain and only zoomed in close (the Louvre, 2026-09-04). Host test `ExtrusionAnchorTest`.

### The base is resolved on the CPU, not sampled in the shader

The base is **an `ElevationManager::getDisplayHeight` query at the anchor**, run on the render
thread by `GLTileRenderer::resolveExtrusionBases` and patched into a per-vertex slot
(`TileGeometry::setVertexBase`, `baseOffset` in the vertex layout). The vertex shader only scales
it: `aVertexBase * uBaseScale + uElevationScale.w`.

It has to be that way because **the elevation texture bound is the one the tile being drawn
carries**. Under overzoom one source tile's geometry is drawn once per target tile, so a footprint
spanning two of them was sampled through two different textures, got two different bases, and tore
open along a straight line that crossed every building it met. A CPU query against the global
elevation source is tile-independent by construction. Reproduced in Paris at
`lon 2.34466 lat 48.84847 zoom 19.04 rotation 66 tilt 45`; `--es exaggeration 0` made it vanish,
which is what pinned it to the elevation path rather than the geometry.

`getDisplayHeight` returns **internal z units with the exaggeration and the Mercator stretch already
applied** — not metres. `uBaseScale` is therefore only `1 / frameScaleZ`, the same factor folded
into `uElevationScale.x` for heights that do come from the texture. Feeding it through the metres
conversion instead applies the Mercator stretch twice and drops the exaggeration.

**mapbox-gl-js lands in the same place from the other end.** Their `a_centroid_pos` carries a
CPU-computed elevation (`fill_extrusion.vertex.glsl`), a building on a border is hidden until
resolved (`HIDDEN_CENTROID`), and `updateBorders` reconciles the two halves — matching them by
`building_id`, and **giving up the flat roof entirely when the neighbours are at different zooms**.
We need none of that reconciliation: their lookup is per tile and each half carries its own clipped
footprint, while ours is one global query at a centroid both halves already share.

What we do copy is the timing. Nothing is guessed before elevation exists — an extrusion whose tile
has no elevation texture yet is **not drawn at all**, because a base of 0 is sea level, not
"unknown". `invalidateExtrusionBases` then re-resolves when the elevation version moves, including
an exaggeration ramp, since the exaggeration is inside that height. It is a counter rather than the
labels' per-tile list because `setVertexBase` is a no-op when the height has not moved, so a
needless re-resolve costs the queries and uploads nothing.

**The ground the anchor reads is a SMOOTHED field, not the lidar level the surface is drawn
from.** `ElevationTextureCache::getDisplayHeight(..., smooth = true)` samples the DEM at
`SMOOTH_BASE_ZOOM` (z12, about 20 m posting, mapbox-terrain-dem's own scale), bilinear, from the
grid LRU — an ancestor level answers too, and is asked for if none is there; the arrival bumps
the elevation version and every base moves together. This is the answer to a family of seams that
only exist on lidar: a 0.84 m DEM steps by metres between a courtyard and its street, and every
piece of a building anchored on it — a part beside its pedestal, the halves a source-tile border
cuts, the copies two zoom levels decode for a mixed-zoom cover — stood at its own height. Read
from a field that is smooth at the scale of a building, those pieces agree to centimetres
**without seeing each other**, which is what mapbox gets for free from its DEM. Spans (bridge
chords) keep the drawn surface (`smooth = false`); a deck must meet the road exactly.

**mapbox's floor, per vertex** (`fill_extrusion.vertex.glsl`: `max(c_ele + height, ele + base + 2)`):
a vertex that rises keeps at least 2 m above the drawn ground under it, so a low part whose
smoothed anchor sits under its own lidar street is still a building and not a hole, and nothing on
a hillside is buried uphill. Done on the CPU through the provider, not in the shader: overzoomed
geometry runs past the target tile, where `applyTerrain` clamps to the edge texel, and a shader
floor read there tilted whole roofs into ramps. And per vertex, not per footprint: a floor over the
ring vertices a tile's clip kept was per tile, and one slab came out 31.6 m in one tile and 34.4 m
in the next. Roofs of low parts follow the ground where the floor bites; tall buildings never feel
it. One query per rising vertex position, columns of a wall share theirs.

mapbox's `flatElevation` lift (the rise across the span, sampled at the span's corners) is NOT
ported: its corners lie off the footprint, and beside the Seine one on the Tuileries terrace lifted
a wing 5 m above its neighbour. On a smooth field the floor covers what the lift was for.

This **removes** the previous model's 5 elevation samples per above-ground vertex, measured at
**+0.35 ms** on the `layers3D` pass on an Adreno 610 at the Grenoble city camera (2.30 vs 1.95 ms
median) — see the [performance log](../performance-log.md). Not re-measured since.


### The dead ends

**maplibre's rigid prism buries buildings.** Their `fill_extrusion.vertex.glsl` anchors the base at
the centroid too and sinks it by a flat 10 m ("basement") so it cannot hang over a falling slope.
On the Bastille hillside the ground rises further than a building is tall, so the whole prism
disappears into the hill. 10 m is not a tunable that fixes it — the base has to follow the terrain.

**The anchor must go through the transformer.** `DefaultVertexTransformer::calculatePoint` returns
`(x, 1 - y, 0)`. The coords go through it; a centroid stored raw does not, so `applyTerrain` sampled
a **mirrored y** — a different hill entirely, and every building on a slope sank out of sight. The
tile clip then has to flip back, because `uTileMatrix` works in unflipped tile space.

**Do not clip by the centroid.** Making the overzoom clip test the centroid looks tidy — one tile
owns each building, no cutting at borders — but a building can reach into a tile while its centroid
sits in a neighbour, and then *every* tile holding it discards it. Walls vanish until a zoom out
retiles them. The clip stays per vertex.

**Clamping the finished top to the ground is not the fix.** It was the first answer to a buried
building, and it takes the max *after* adding the height: the wall then has zero height wherever
the hill reaches the roof, so whole faces are simply gone. Taking the ground per vertex instead
keeps the walls but bends the roof down the slope — tangram's melted look, by another route. The
max has to be over the FOOTPRINT and applied to the BASE.

**Sampling the footprint in the shader cannot be made tile-independent.** The previous model took
the max of 5 texture samples — the centroid plus a stored reach in four directions — and it was
wrong twice over. The reach was stored as a fraction of the tile in a signed byte, so its cap of 127
became a zoom threshold: a 30 m footprint hit it at z19 and a 50 m one at z18, and past that every
large building suddenly stood a little shorter. Widening the unit fixed that and left the tile-seam
crack untouched, because the samples still went through whichever elevation texture the drawing tile
had bound. The units were the smaller of the two bugs.

### What is still wrong

Pieces of one building each anchor at their own centroid; they agree only as well as the smoothed
field is flat between those centroids — centimetres across a block in Paris, up to a metre across
a 400 m palace on a real slope. mapbox groups a tile's parts by `building_id` and reconciles
borders (`updateBorders`); tried here in three forms on 2026-09-03 (per-tile groups, source-tile
groups, a global union) and all three made the pieces disagree by more than the field does, because
an anchor shared across a large building is far from most of its parts. On a smooth field the
ungrouped anchor is the smaller error.

## Bridges and tunnels: spans

A road on 3D terrain is **draped** — painted into the terrain texture — so it follows every bump
the DEM has. That is right for a road on the ground and wrong for one on a bridge: the deck sags
into the valley it crosses, and a tunnel climbs over the hill it goes through. A DSM makes it
worse, since it catches the deck itself as terrain and spikes the middle upward.

`line-elevation-mode` / `polygon-elevation-mode` (`drape` | `span` | `underground`) mark the
features that do not lie on the ground. A span leaves the drape bake by construction —
`isDrapeableGeometry` returns false for any geometry carrying span records, whatever the layer
filter says — and takes its height from a **chord** between its two portals instead of from the
DEM under it.

### Portals, and why the tile is the test

A span's portals are the feature's own two ends. The tile grid cuts a long bridge into pieces, and
an end the tile cut is not a portal: a chord between two cut points dives to whatever the ground
does at the cut.

The classification tests the **tile**, not the clip box. The source clips at its own buffer
(mapbox: 1/64 of a tile), so every cut end lands well inside our 1/8 clip box and would read as a
real portal — which is what drew the Millau viaduct as two 30% ramps and a middle. The same point
is inside the *neighbouring* tile's copy, which is where its portal is seen.

### Joining the pieces

The pieces are matched **by geometry, not by feature id**: `LineSymbolizer` passes
`FeatureCollection::getLocalId`, a layer offset plus an index, so one OSM way gets a different id
in every tile it crosses. Two pieces are one structure when the ends the tile *cut* meet — the
buffer makes neighbouring copies overlap rather than touch, so this is proximity (13–18 m between
the Millau pieces at z15) with a direction test to keep a crossing bridge out of the chain.

Joined, the Millau viaduct resolves as one chord — **2472 m at 3.3%** against 2460 m at 3.025% in
reality when it was first measured, **3440 m** on the tile set measured since (see below). The
portals are sampled at the junctions, where the approach road is draped and so sits
on the DEM — anchoring the deck to the same value is what makes the two meet instead of stepping.

A resolved chord is remembered in a small LRU, because a bridge's portals are a property of the
world and not of what is on screen: zooming into one end drops the far piece from the visible set,
and without the cache the chord shortens to whatever is still loaded and the deck changes angle.
A piece with no portal of its own borrows a cached chord by its own **midpoint**, since a piece in
the middle of a long deck is cut at both ends and has none to offer.

**Two portals are not a resolved chord.** Neighbouring tiles each hold a copy of the same abutment,
so a group whose far portal is off screen still collects two portal points — 45 m apart at z15,
across pieces running 1305 m — and that chord passed every other test here. Measured on the viaduct
at z15.16, all eleven of its pieces resolved to that 45 m chord: the deck sagged onto the DEM and
its labels went with it, which is what read as "the join fails when part of the bridge is off
screen". The rule is `SpanGeometry::chordSpansGroup` — a chord must reach across **the group's own
diameter** (95%, for the buffer overlap) — and a group that fails it is unresolved, so the cache
borrow runs and hands it the 3440 m chord it resolved at z14.

### Everything else that stands on the deck

- The **bed polygon** (mapbox's `structure`/`class=land`) takes `polygon-elevation-mode`; its
  portals are the two vertices farthest apart, which for a deck-shaped ring are its ends.
- **Labels, POIs and one-way arrows** ask `spanHeightAt` before the terrain: lifted up to 151 m at
  mid-span on the viaduct, tapering to 0 at the abutments. The allowance scales with the span
  (2%, floor 25 m) because a long deck *curves* in plan while its chord is straight — Millau's
  ~20 km radius puts mid-deck some 36 m off its own chord, and a fixed 25 m missed exactly the
  labels standing on the bridge.
- A span is lit **flat**, not by `terrainNdl`. Borrowing the terrain's normal is right for a road
  lying on the ground and wrong for one flying over it: the deck came out shaded by the valley wall
  beneath it and stepped in tone against its own draped approach.
- A layer whose geometry is *all* spans still occupies its place in the drape unit stack. Dropping
  it shifts every later layer's coverage-mask index, which masked the whole road network away.

### The deck as an extrusion

`building-elevation-mode: span` stands a `BuildingSymbolizer`'s prism on the **chord** instead of on
the ground, so `min-height`/`height` become a thickness measured from the deck rather than a height
above the terrain (negative values hang the structure below the road surface). It is the same
mechanism the span lines already use and not a second one: both write a per-vertex `baseOffset`, so
`resolveExtrusionBases` simply hands a geometry carrying span records to `resolveSpanBases`, which
takes the base from each vertex's own position along the chord. A building keeps the old path — one
elevation query per footprint centroid, giving the flat base a building wants.

`Polygon3DStyle` therefore carries an `elevationMode`, and `TileLayerBuilder` splits the batch on it
(a deck and a building resolve their bases differently, so they cannot share one geometry) and emits
the same `SpanVertexInfo` a filled bed does. Both get their two ends from
`SpanGeometry::farthestPair` — a ring has no ends, so its span is its longest axis, found
centroid → farthest → farthest again. Getting that wrong lays the chord across the deck's *width*,
which resolves as a metre-long span and leaves the deck on the ground; `tests/vt/SpanGeometryTest.cpp`
pins it on a 2460 × 32 m ring.

What this buys beyond looks: labels, POIs and one-way arrows on a deck stop being a special case,
because the deck becomes the same "POI on top of a building" query, and the existing extrusion
shadow pass applies to it unchanged.

Two things had to be got out of the way before any of it drew, and both are worth knowing on their
own:

**A style's building height ramp flattens a deck to nothing.** A converted Mapbox Standard carries
`building-height-scale: linear(zoom, (15, 0), (15.3, 1))`, so every extrusion is multiplied by
**zero** below mb zoom 15 — which is every camera a bridge is looked at. A span deck now takes none
of the three building multipliers (the ramp, the tilt drop, grow-on-appear): they all mean "this
building is not there yet", and a bridge is structure.

**A style parameter can only narrow what draws, never widen it.** A rule whose filter is false for
the parameter's *declared* value is pruned when the style compiles and cannot come back at runtime.
`--es buildings 1` works because Standard declares `buildings: 2` and the runtime value only
removes rules; `--es deck3d 1` could never work while `deck3d` was declared `0`, and every test run
through that knob was silently testing nothing. The demo declares `deck3d: 1` for this reason, and
`--es deck3d 0` turns it off — the direction that works.

**What it looks like.** Verified side-on from west of the viaduct
(`--es lat 44.0790 --es lon 2.9960 --es zoom 15.0 --es tilt 12 --es rotation 90`): the deck reads as
a solid band with thickness. From ABOVE it correctly shows only the road surface, and at a 6.5 m
thickness that is ~2 px at z14 — so "the deck is missing" from a top-down camera is the geometry
being thin, not a rendering failure. What makes a bridge read at those zooms is its shadow and its
piers, neither of which this draws yet.

The pure geometry is in `vt/SpanGeometry.h` and tested on the host (`tests/vt/SpanGeometryTest.cpp`);
none of these rules fails loudly when wrong.

### What is still wrong

**A structure never seen whole has no chord to borrow.** The cache carries a bridge across a zoom
or a pan, but it is only ever filled by a group that resolved on its own. Opening the map already
zoomed into one abutment leaves the deck draped until the far end comes into view once.

**The chord runs long.** The viaduct joins as 3440 m against a 2460 m deck — the chain reaches into
the structure beyond its northern abutment. Not visibly wrong at the cameras tested (the extra
length is bridge too), but it is not the bridge, and 2472 m is what an earlier tile set gave.

**Tunnels are unimplemented.** `underground` parses and is carried through, but nothing draws a
tunnel see-through against the terrain in front of it.

**The converter emits neither property.** `mapbox2css` does not translate `[structure]` into
`line-elevation-mode`, so a regenerated style loses the annotations.

## Bridges and tunnels: spans

A road on 3D terrain is **draped** — painted into the terrain texture — so it follows every bump
the DEM has. That is right for a road on the ground and wrong for one on a bridge: the deck sags
into the valley it crosses, and a tunnel climbs over the hill it goes through. A DSM makes it
worse, since it catches the deck itself as terrain and spikes the middle upward.

`line-elevation-mode` / `polygon-elevation-mode` (`drape` | `span` | `underground`) mark the
features that do not lie on the ground. A span leaves the drape bake by construction —
`isDrapeableGeometry` returns false for any geometry carrying span records, whatever the layer
filter says — and takes its height from a **chord** between its two portals instead of from the
DEM under it.

### Portals, and why the tile is the test

A span's portals are the feature's own two ends. The tile grid cuts a long bridge into pieces, and
an end the tile cut is not a portal: a chord between two cut points dives to whatever the ground
does at the cut.

The classification tests the **tile**, not the clip box. The source clips at its own buffer
(mapbox: 1/64 of a tile), so every cut end lands well inside our 1/8 clip box and would read as a
real portal — which is what drew the Millau viaduct as two 30% ramps and a middle. The same point
is inside the *neighbouring* tile's copy, which is where its portal is seen.

### Joining the pieces

The pieces are matched **by geometry, not by feature id**: `LineSymbolizer` passes
`FeatureCollection::getLocalId`, a layer offset plus an index, so one OSM way gets a different id
in every tile it crosses. Two pieces are one structure when the ends the tile *cut* meet — the
buffer makes neighbouring copies overlap rather than touch, so this is proximity (13–18 m between
the Millau pieces at z15) with a direction test to keep a crossing bridge out of the chain.

Joined, the Millau viaduct resolves as one chord — **2472 m at 3.3%** against 2460 m at 3.025% in
reality when it was first measured, **3440 m** on the tile set measured since (see below). The
portals are sampled at the junctions, where the approach road is draped and so sits
on the DEM — anchoring the deck to the same value is what makes the two meet instead of stepping.

A resolved chord is remembered in a small LRU, because a bridge's portals are a property of the
world and not of what is on screen: zooming into one end drops the far piece from the visible set,
and without the cache the chord shortens to whatever is still loaded and the deck changes angle.
A piece with no portal of its own borrows a cached chord by its own **midpoint**, since a piece in
the middle of a long deck is cut at both ends and has none to offer.

**Two portals are not a resolved chord.** Neighbouring tiles each hold a copy of the same abutment,
so a group whose far portal is off screen still collects two portal points — 45 m apart at z15,
across pieces running 1305 m — and that chord passed every other test here. Measured on the viaduct
at z15.16, all eleven of its pieces resolved to that 45 m chord: the deck sagged onto the DEM and
its labels went with it, which is what read as "the join fails when part of the bridge is off
screen". The rule is `SpanGeometry::chordSpansGroup` — a chord must reach across **the group's own
diameter** (95%, for the buffer overlap) — and a group that fails it is unresolved, so the cache
borrow runs and hands it the 3440 m chord it resolved at z14.

### Everything else that stands on the deck

- The **bed polygon** (mapbox's `structure`/`class=land`) takes `polygon-elevation-mode`; its
  portals are the two vertices farthest apart, which for a deck-shaped ring are its ends.
- **Labels, POIs and one-way arrows** ask `spanHeightAt` before the terrain: lifted up to 151 m at
  mid-span on the viaduct, tapering to 0 at the abutments. The allowance scales with the span
  (2%, floor 25 m) because a long deck *curves* in plan while its chord is straight — Millau's
  ~20 km radius puts mid-deck some 36 m off its own chord, and a fixed 25 m missed exactly the
  labels standing on the bridge.
- A span is lit **flat**, not by `terrainNdl`. Borrowing the terrain's normal is right for a road
  lying on the ground and wrong for one flying over it: the deck came out shaded by the valley wall
  beneath it and stepped in tone against its own draped approach.
- A layer whose geometry is *all* spans still occupies its place in the drape unit stack. Dropping
  it shifts every later layer's coverage-mask index, which masked the whole road network away.

### The deck as an extrusion

`building-elevation-mode: span` stands a `BuildingSymbolizer`'s prism on the **chord** instead of on
the ground, so `min-height`/`height` become a thickness measured from the deck rather than a height
above the terrain (negative values hang the structure below the road surface). It is the same
mechanism the span lines already use and not a second one: both write a per-vertex `baseOffset`, so
`resolveExtrusionBases` simply hands a geometry carrying span records to `resolveSpanBases`, which
takes the base from each vertex's own position along the chord. A building keeps the old path — one
elevation query per footprint centroid, giving the flat base a building wants.

`Polygon3DStyle` therefore carries an `elevationMode`, and `TileLayerBuilder` splits the batch on it
(a deck and a building resolve their bases differently, so they cannot share one geometry) and emits
the same `SpanVertexInfo` a filled bed does. Both get their two ends from
`SpanGeometry::farthestPair` — a ring has no ends, so its span is its longest axis, found
centroid → farthest → farthest again. Getting that wrong lays the chord across the deck's *width*,
which resolves as a metre-long span and leaves the deck on the ground; `tests/vt/SpanGeometryTest.cpp`
pins it on a 2460 × 32 m ring.

What this buys beyond looks: labels, POIs and one-way arrows on a deck stop being a special case,
because the deck becomes the same "POI on top of a building" query, and the existing extrusion
shadow pass applies to it unchanged.

Two things had to be got out of the way before any of it drew, and both are worth knowing on their
own:

**A style's building height ramp flattens a deck to nothing.** A converted Mapbox Standard carries
`building-height-scale: linear(zoom, (15, 0), (15.3, 1))`, so every extrusion is multiplied by
**zero** below mb zoom 15 — which is every camera a bridge is looked at. A span deck now takes none
of the three building multipliers (the ramp, the tilt drop, grow-on-appear): they all mean "this
building is not there yet", and a bridge is structure.

**A style parameter can only narrow what draws, never widen it.** A rule whose filter is false for
the parameter's *declared* value is pruned when the style compiles and cannot come back at runtime.
`--es buildings 1` works because Standard declares `buildings: 2` and the runtime value only
removes rules; `--es deck3d 1` could never work while `deck3d` was declared `0`, and every test run
through that knob was silently testing nothing. The demo declares `deck3d: 1` for this reason, and
`--es deck3d 0` turns it off — the direction that works.

**The deck hangs under its road, in metres.** `min-height: -7; height: -0.3` is a prism from 7 m
under the chord to 0.3 m under it, and for a while it stood 6.7 m *above* the road instead: the
builder converted the offset with `calculateHeight`, which answers in TILE units, and
`resolveSpanBases` added it to a chord held in INTERNAL units — at z15 the -7 m became a rounding
error and the whole thickness went up. `SpanRecord::baseOffset` is now metres, converted where the
chord is, with the same factor the vertex shader applies to a DEM sample (`metersToInternal` from
any elevation texture, times the mercator stretch at the vertex).

**What the deck wears.** The span drape — the tile's span content baked on its own, sampled by the
extrusion — goes on the ROOF only (`vSpanRoof`): a wall's tile position runs along the deck's edge,
and sampling the drape there smeared whatever the road's edge held down the whole face as vertical
ribbing. And it is baked over the deck's own BOUNDS (`_spanDrapeBounds`, the pieces' ends plus a
margin of 4 % of the tile, never under ~25 m), not the whole tile: a deck stands above the ground
and is that much closer to the camera, so its drape is magnified past the ground's, and a
tile-wide bake of a narrow deck spent most of the texture on empty tile. `bakeSpanDrapeTile`
premultiplies `SpanGeometry::clipZoomToBounds`, `resolveSpanDrape` composes
`drapeTransformInBounds` into the sampling transform, both host-tested. In practice the roof drape
is rarely seen at all: a style whose bridge fills and casings are spans draws them LIVE on the
chord, above the roof, and live lines are sharp at any height — which is the real answer to "the
drape on a high bridge loses resolution".

Three rules that took a round each: the bounds hold the DECK's records, not only the draped road's
(a deck wider than the road's margin sampled the clamped edge texel as dark streaks); the composite
is PREMULTIPLIED like every other bake (mixed as straight alpha, a half-covered texel came out half
black — a fringe down every line); and a span extrusion **discards every fragment outside its own
tile** (`polygon3DFsh`, the `SPAN` flag, drape or no drape). That last one is the seam fix: a roof
triangle is emitted whole into every target tile it touches (`TileLayerBuilder::appendPolygon3D`
keeps a triangle whose bbox reaches the polygon clip box), so past a tile edge a neighbour's copy
of the same deck could win the depth test with a drape that stops at ITS tile — a plain strip of
roof along every cut, which read as a wall between pieces at different heights. It was never a
height step: with the drape forced off the whole roof was exactly the strip's colour. And a copy
drawn before its drape is baked is the same deck uncut with walls only near its own tile, so
wherever it won, the deck's side went missing — hence the cut applies to every span extrusion,
not only the draped ones. mapbox cuts its extrusions at the tile for the same reason. The
trade-off is the one mapbox has: a tile whose deck geometry has not resolved (`resolveSpanBases`
is all-or-nothing per geometry) is a gap in the deck now, where an uncut neighbour used to cover
it. A span extrusion also gets no ground-AO skirt (it hangs from a chord, the skirt was a halo
sliding over ground it never touches) and is skipped by the label occlusion depth pass (it hid
its own road's arrows and name).

The union build is timed in the profile build's `RenderStats: tileSetChange … spanUnionsMs` line:
at Grenoble it is 3.5–6.8 ms per second of panning against 55–100 ms of cull work, most of it
label maps.

**A piece never seen whole.** A map opened in the middle of a long bridge holds none of its
portals, so every piece is unresolved and the deck drapes onto the valley. `buildSpanUnions` now
reports every cut end it could not give a chord, stepped a twentieth of a tile past the cut
(`SpanGeometry::beyondCutEnd`, past the source's buffer), and `TileLayer::collectSpanReferenceTiles`
fetches the tile each point lands in as a preloading tile — cached, never drawn, and handed to the
renderer as its own list (`setVisibleTiles(tiles, spanReferenceTiles)`): it joins the span unions
and nothing else, no render tile, no labels, so a reference that overlaps the view does not double
its geometry. Three levels COARSER than the piece (`SPAN_REFERENCE_ZOOM_DROP`), floored at z14
(`SPAN_REFERENCE_MIN_ZOOM`, where OSM-derived tile sets still carry their bridges; a z11 tile has
no span in it at all) and never past the data source's max zoom: a tile beyond it is the same
source data cut again at the finer grid, with the same stranded ends. A piece already at the floor
walks to its neighbour at the same zoom, one hop per cull. The zoom groups in `buildSpanUnions` run
coarsest first and remember their chords as they go (512 of them — a city view holds one per
structure per zoom group, and at 64 the cache evicted chords the pieces on screen still borrowed),
so the fine pieces borrow the reference tile's chord in the same pass. Two plumbing faults kept
this from working at all under overzoom: the fetch-only path (`buildFetchTiles(..., fetchOnly)`,
the OOM fix — no draw data, no substitution search) never handed the tile over, and a preloading
tile's arrival triggers no cull, so with the camera still nothing ever read it. A reference
tile's arrival now refreshes like a visible one's (`FetchTaskBase::run`). And they live in their
own map (`VectorTileLayer::_spanReferenceCache`, pruned to the tiles named), not the preloading
LRU: twenty coarse city tiles at a few MB each evicted one another there, every refetch re-culled,
and the map sat in an endless 80-culls-a-second loop with the deck flipping between two chords.
The set is hard-bounded at 32 (a set full of tiles all named this cull takes no new one) and the
ends are named finest zoom first: named coarsest first, a city at z14 with a thousand stranded
ends grew it to 121 tiles, the horizon took every slot, and the union pass — quadratic then —
ran a second per build at 2 fps. The pass is bucketed by end now (a cell per tolerance, 3x3
lookup), 3.5 ms per second of panning at the same view.
Measured at Grenoble,
Pont de la Porte de France, z19.24 over a z16 source: 218 stranded ends to 8, all eight z14/z15
pieces at the horizon; the deck's 68 z19 and 16 z18 pieces share one chord and hold it through a
pan. The log line `TileLayer: N span reference tiles for M stranded span ends` is the
convergence; a piece that can never resolve (an approach ramp classed as a span) keeps one
reference tile alive and flips the count by a few, which is what the line looks like when it is
done.

**What it looks like.** Verified side-on from west of the viaduct
(`--es lat 44.0790 --es lon 2.9960 --es zoom 15.0 --es tilt 12 --es rotation 90`): the deck reads as
a solid band with thickness. From ABOVE it correctly shows only the road surface, and at a 6.5 m
thickness that is ~2 px at z14 — so "the deck is missing" from a top-down camera is the geometry
being thin, not a rendering failure. What makes a bridge read at those zooms is its shadow and its
piers, neither of which this draws yet.

The pure geometry is in `vt/SpanGeometry.h` and tested on the host (`tests/vt/SpanGeometryTest.cpp`);
none of these rules fails loudly when wrong.

### What is still wrong

**The chord runs long.** The viaduct joins as 3440 m against a 2460 m deck — the chain reaches into
the structure beyond its northern abutment. Not visibly wrong at the cameras tested (the extra
length is bridge too), but it is not the bridge, and 2472 m is what an earlier tile set gave.

**Tunnels are unimplemented.** `underground` parses and is carried through, but nothing draws a
tunnel see-through against the terrain in front of it.

**The converter emits neither property.** `mapbox2css` does not translate `[structure]` into
`line-elevation-mode`, so a regenerated style loses the annotations.

## Near and far planes

Terrain mode floors the near plane at **camera height / 50**, which is tangram's
`core/src/view/view.cpp:452`. The old behaviour — near taken from the nearest visible ground point,
floored at 1/16 of an internal unit — gave centimetre near planes next to a slope, a far/near ratio
of 10⁴–10⁶, and NDC depth so non-linear that a constant-NDC bias was worth hundreds of metres at
range. That is the mechanism behind every see-through this project has had.

"Camera height" is the smaller of the distance to the **focus** and the height above the **terrain
under the camera** (`ViewState::setTerrainCameraReference`, published every frame by the renderer
next to the clearance). Tangram's `m_pos.z` is the distance to what the camera looks at and their
camera is held a distance away from the terrain itself (the depth at the screen centre against
`minCameraDist`); ours is held a *clearance above the ground under it*, so at a low tilt the focus
is kilometres away while the ground is a couple of hundred metres below — and a fiftieth of the
focus distance then parks the near plane in front of the ground at the bottom of the screen and
cuts it away. Over flat ground with the focus close the two distances are the same; the cost of the
smaller one is bounded by 1/sin(tilt) (2× at tilt 30), so the depth budget is only spent in the
close-to-terrain case that needs it.

The floor is a floor and the ground walk is a **ceiling** too, but only when the view is pitched
away from the camera geometry — free roam looking up, or a first person camera
([13-celestial.md](13-celestial.md#seeing-them-free-roam)). The walk takes the near plane from
where the sampled rays MEET THE GROUND; as the view pitches up those hits move off into the
distance, the near plane follows them out, and everything close to the camera is clipped away —
worse the higher the view goes. What is near the camera does not move when the view turns, so in
that case `near` is capped by the same camera-height rule, which does not depend on the view
direction at all.

Their far plane (`2·height/cos(pitch + fovy/2)`) is available as
`TerrainOptions::ViewDistanceFactor` but changes nothing at the cameras tested: the ground-derived far is
already inside the bound it gives.

### An absolute view distance only extends the rule

`TerrainOptions::ViewDistance` pins the distance in **metres** instead, and
`StyleEnvironment::terrainMaxVisibleDistance` (`terrain-max-visible-distance`) does the same from a
style. Both used to take over `ViewState::calculateViewDistance` outright, and that is wrong in one
direction: tangram's rule is **scale-invariant** — `cameraDistance ∝ 2⁻ᶻᵒᵒᵐ`, so the drawn ground
keeps the same size on screen at every zoom — while metres do not. Zoom out and the fixed distance
becomes the binding one, and the ground ends in a disc well inside the screen.

Derived, 1080×2400, fovy 60, tilt 90, z8: camera height ≈ 1270 km, the factor rule ≈ 2500 km,
against a 170 km pin — 15× short. Both are now a `max()`: the absolute distance is a **minimum**,
which is what "keep the panorama as the camera descends into it" actually asks for, and it never
shortens the zoomed-out view ([#156](https://github.com/massif-maps/MassifMaps/issues/156)). The far
plane still follows the absolute distance only when the absolute one won; where the rule is longer
this is the plain factor case and the depth budget is untouched.

## Auto-flattening: when 3D stops earning its cost

Zoomed far out the displacement is sub-pixel, and straight down it shows nothing — but the drape
RTT, the terrain passes and the elevation fetches are all still paid. `TerrainOptions` flattens the
map itself in those two cases, without touching `Enabled`: the rule writes `Flattened`, and what the
switch then does with it is the section below.

The criterion is **parallax in screen pixels**, not a zoom threshold — a fixed zoom is wrong for
flat country or for a high exaggeration:

```
parallax = halfScreenDiagonal · heightRange · exaggeration / cameraDistance
```

Derived over the Alps (4 km range, 1300 px half-diagonal): **z8 → ~4 px, z13 → ~130 px**.
`AutoFlattenParallax` is the threshold, `AutoFlattenTilt` the separate top-down one (at z13 the
parallax is still large and 3D still buys nothing). They default to **2 px and 88°** — device-checked
on the Crosscall, where below 2 px the displacement is under the antialias ramp — and 0 disables
either half. The rule, its
hysteresis and the ramp are `all/native/terrain/AutoFlatten.h`, kept free of the renderer so
`tests/api/AutoFlattenTest.cpp` can check them on the host.

**Hysteresis is not optional**: 3D returns at 1.5× the parallax threshold and 2° below the tilt one.
Without it a camera parked on a threshold flips modes every frame.

## The 2D/3D switch

`Flattened` is the state — writable, so an app can drive its own 2D/3D switch, and the auto rule
above writes the same field. `FlattenMode` is **how far the switch goes**, and it is the whole
design:

| `FlattenMode` | Flat costs | Switching costs |
|---|---|---|
| `RENDER` (default) | the terrain passes, the drape and the elevation fetches are gone, but the tiles keep the terrain subdivision and the terrain tile set | nothing — one ramp, no re-cull, no re-decode |
| `FULL` | nothing: the map decodes, culls and draws as if no `TerrainOptions` were attached | a re-decode of the visible tiles, each way |

Three pieces of state, and which question each answers:

| | Question | Read by |
|---|---|---|
| `isEnabled()` | is terrain configured at all | the app; gates the other two |
| `isActive()` (`FlattenRatio < 1`) | is 3D being **rendered** | every renderer, the touch handler, the sky, the hillshade paint |
| `isDecodeActive()` | are tiles being **prepared** for 3D | `TileLayer::loadData`'s cache compare, `resetTileTransformer`, `calculateVisibleTiles` |

The one fact everything rests on, and it is asymmetric:

- **terrain-decoded tiles render correctly flat** — the displacement is GPU-side and the only
  decode-time difference is subdivision density, so it is extra triangles and nothing else;
- **flat-decoded tiles do not render correctly in 3D** — with no subdivision a road chords straight
  between its endpoints, which over a valley rides well above the ground.

So the decode is only ever moved **while the map is flat**, where the two densities draw the same
picture, and 3D is never entered before the tiles for it exist. `all/native/terrain/FlattenSwitch.h`
is that rule alone — four phases, free of the renderer, so `tests/api/FlattenSwitchTest.cpp` checks
it on the host:

```
FLAT ──ask 3D──▶ WARMING ──tiles ready──▶ RAMPING ──▶ TERRAIN
 ▲                (renders 2D)                          │
 └──────────────── RAMPING ◀────────── ask flat ────────┘
      (drops the decode one frame AFTER it settles)
```

`WARMING` is deliberately **not** `isActive()`: it renders as plain 2D, so the wait costs 2D and
shows no half-built terrain. It ends on `TileLayer::isTerrainDecodeSettled()` for every tile layer,
or on `MapRenderer::TERRAIN_SWITCH_WARM_TIMEOUT` (2.5 s — late 3D beats a map pinned flat by one
tile that never loads). Going the other way there is nothing to wait for, so it ramps at once.

### Who drives the ratio

Three ways, in increasing order of control:

| | What the app writes | The clock |
|---|---|---|
| automatic | `Flattened` | `AutoFlattenDuration`, and `AutoFlattenRiseDuration` for the way up (negative = the same) |
| by gesture | `AutoFlattenTilt` / `AutoFlattenParallax` | the same |
| by hand | `FlattenRatio` | the app's own |

The two durations are split because the directions are not alike: the rise is the one an app matches
to a camera flight, and the one that waited for its tiles first.

`FlattenRatio` is writable, and that is the only way two animations can be made to match **exactly** —
a duration is a second timer, and two timers of the same length still drift when a frame is dropped.
Writing it puts the switch in `MANUAL`, which suspends both its own ramp and auto-flattening, and
**keeps them suspended** until `Flattened` is written. An app driving an animation therefore writes
`Flattened` once when it ends; forget it and a later tilt gesture silently does nothing. `MANUAL` is
deliberately not auto-released at a settled ratio: a rise starts by writing exactly 1.0, so releasing
on the endpoints would hand control back on the animation's first frame. `MANUAL` keeps the tile gate: a ratio below 1 asks for
3D's tiles and the ground is **held** flat until they arrive, with `isSwitching()` as the observable
so an app can start its flight when the hold ends rather than watch its animation jump.

A tilt threshold is asymmetric by construction, and it shows: the rule fires *at* 88°, so a flight
from a landscape view to top-down flattens at the very END of it, while the reverse fires almost
immediately. That is what a threshold means, not a bug — an app that wants the switch to lead the
camera drives `Flattened` or `FlattenRatio` itself and leaves the thresholds at 0.

Two mechanics that make the switch invisible:

- **The tile set follows `isDecodeActive()`, not `isActive()`.** The terrain LOD asks for tiles flat
  rendering never wanted (overzoom targets, the coarsening floor), so culling on the render state
  re-culls at the instant the terrain appears — which is the tile set arriving *after* the map is
  already 3D. That was the flash.
- **The decode change invalidates, it does not clear.** `TileLayer::loadData` calls
  `invalidateTiles(false)` rather than `clearTileCaches(true)`: the old tiles stay on screen and are
  re-fetched one by one. Clearing them blanks the map for a whole decode.
- **Shadows stand down for the ramp.** A cascade is only re-cast when its light box or its caster
  list changes, and neither does while the ratio moves — but the ground *receiving* the shadow is
  displaced every frame, so the map wears the shadow of a terrain it no longer has. `applyTerrainShadows`
  drops the pass while `FlattenRatio > 0` rather than re-casting each frame, which would be a full
  caster pass on the frames least able to afford one. The `!shadowsWanted` path already invalidates
  the atlas, so coming back out of the ramp re-casts every page.

Two consequences worth knowing:

- The ramp bumps the elevation version every frame, so `TileRenderer`'s `scaleOnly` path re-anchors
  labels on each of them, and `VectorLayer`'s terrain projection surface is rebuilt (debounced by
  `ELEVATION_REFRESH_DELAY`). Unmeasured; if a ramp ever stutters, this is where to look.
- `HillshadeRasterTileLayer::isTerrainPaintActive` reads `isActive()`, so flattening drops the layer
  back to its own DEM tile set rather than the terrain's elevation texture. That is a tile-set swap
  on the flip — the alternative is the hillshade vanishing, so it is the right trade, not an
  oversight.

**Not measured:** what `FULL` actually buys in 2D frame time against `RENDER`, and how long the
`WARMING` wait really is on a device. Both need the `-PprofileRender` bench of
[10-performance.md](10-performance.md) at a mountain camera and a city one. See
[#177](https://github.com/massif-maps/MassifMaps/issues/177), which asked the opposite question —
always decode for terrain so `setEnabled` never invalidates — and which this answers the other way,
without its permanent cost.

## The camera against the terrain

**The focus rides on the ground.** The map's projection surface is planar
(`TerrainProjectionSurface` only places vector elements), so every camera event puts the focus at
sea level, and the zoom — the camera's distance to the focus — was a distance to sea level. As
elevation arrives the ground under the focus rises towards a fixed camera: at the Bastille (470 m)
a z16.27 / tilt 20 camera sat at 362 m ASL, *inside* the hill it looked at, which is what drew the
near roads eight times too wide and blurred (the drape is magnified by however much closer the
ground is), and at Aiguille du Midi (3842 m) the clearance below zoomed out until the camera cleared
the peaks: z12.73 for a z16.27 request. mapbox defines zoom as the distance to the terrain at the
centre (`transform._centerAltitude`, `_updateCameraOnTerrain`) and lifts the camera with it.
`MapRenderer` now does the same every frame in terrain mode: when a decoded grid answers under the
focus, `ViewState::liftFocus` moves focus and camera together onto it (zoom, tilt and rotation kept).
Cached-only and only when a grid answers — an evicted grid is not a valley. A pan or zoom event still
places its focus at sea level; the next frame lifts it, and the camera-to-focus vector the event
built is preserved, so the camera follows the ground's height difference as mapbox's does. Both
spots read z16.27 after it.

**The auto-flatten rule hands back what it set.** The SDK's defaults (2 px, 88°) run on the first
frame, at the default tilt of 90, before an app has set its own thresholds — and an app that then
sets them to 0 turned the rule off with its last answer ON, so nothing ever asked for 3D again:
the map came up flat, at random, whenever that first frame beat the app's setters. A rule disabled
while ON now releases the flat state it set (never an app's own `setFlattened(true)`), and logs it.

**The clearance is mapbox's, a fraction of the height, not a fixed 60 m.** The camera is kept a
height above the ground *under it* (`terrain/CameraClearance.h`, the port of
`transform._minimumHeightOverTerrain` / `_constrainCamera`): a sixteenth of its distance to sea
level — `mercatorZ(min(seaLevelZoom, maxZoom) + MAX_DRAPE_OVERZOOM)`, with their constant 4 — so
it shrinks with every zoom in and never blocks one by itself. The fixed 60 m it replaces was the
real cap on the zoom, not the sea-level reference: the camera-to-focus distance at z21 is about
60 m on a phone, so flat ground straight down stopped at ~z21, tilt 45 at ~z20.5, and a slope
under the camera on a 600 m hill ate the rest down to z18. `TerrainOptions::CameraClearance` is
now an optional **floor** in metres under that rule, default 0. What the rule inherits from
mapbox: on very high ground the sea-level share is a cap of its own — 3842 m at Aiguille du Midi
needs a 256 m orbit straight down, about z19. It is a
**bound on the zoom** (`ViewState::getTerrainMaxZoom`, clamped in `CameraZoomEvent::calculate`),
solved on the camera-to-focus vector so it lands exactly on the shell, plus a per-frame
correction in `MapRenderer` for the paths that lower the camera without zooming — panning into
a hillside, a DEM tile arriving. Three rules keep a gesture against that bound from throwing the
map somewhere else:

- **The bound stops a zoom in; it never drives a zoom out.** A zoom event scales the map about its
  pivot, and with the pivot under the fingers, clamping a zoom-*in* request to below the current
  zoom scales the map the other way about that point — the map jumps sideways, once per pinch tick.
  `ViewState::clampZoom` honours the same rule. Getting back onto the shell is the renderer's
  correction, and it is mapbox's: the camera is **lifted at a constant distance to the focus**, so
  the zoom is kept and the tilt gives (`_constrainCamera` keeps `cameraToCenter`'s length). Only
  past the tilt range's top does the rest come from a zoom out about the focus. And it lifts only
  a camera under the ground, or after a pan (their `adaptCameraAltitude` = dragging): after a
  zoom the ground under the moved camera differs by a little, and lifting for that turned every
  pinch tick on a slope into a tilt.
- **A zoom is never cancelled for want of a ground hit.** `TouchHandler::calculatePivotPos` falls
  back to the focus when the ray under the fingers misses the anchor plane or lands past the far
  plane. Close to the terrain the far plane is short and half the screen is sky, so requiring a hit
  (which the pinch, the wheel and the double tap all did) left the map unable to zoom out at all —
  the "I have to pan somewhere else before I can move" symptom.
- **The scale and the angle come from the SCREEN, not from the ground.** A pinch and a two-finger
  turn are what the fingers did (tangram: `InputHandler::handlePinchGesture` /
  `handleRotateGesture`, fed by the platform gesture detector). Taking them from where the two rays
  meet the ground makes a grazing ray — a low camera, a finger near the horizon — into most of the
  answer. The pan is still world-anchored (that is the point of a map pan) and goes through one
  path for both gestures, `TouchHandler::panBetween`, which honours `PanningSpeedMode` and, below
  tilt 15, caps the travel at what the finger's pixels are worth at the map scale — tangram's
  `getTranslation` guard for a near-horizontal view.

`isValidScreenPosition` tests the plane the gesture is actually anchored to (the terrain height
under the touch, `_gestureAnchorHeight`), not sea level: in the mountains the two are hundreds of
metres, and at a low tilt kilometres of ray, apart.

### The zoom pivot sank the focus, and everything was drawn at the wrong scale (fixed 2026-08-13)

**Symptom.** In 3D, zoom very close to the terrain, pan, then pinch back out: the map sticks in a
state where everything is blurry and oversized, and stays that way while zooming out. Enough
movement clears it. In 2D the same state shows labels, shields, peak icons and line widths several
times too large for the zoom on screen, the `VectorLayer` route line with them. Reported as
"blurry", but it is a SCALE fault, not a resolution one. Only ever reproduced with a real style
(the packaged one) — an inline style whose widths and sizes are constants shows almost nothing,
because the fault is in what the zoom-dependent style functions are evaluated at.

**Cause.** `CameraZoomEvent::calculate` shifted the map about the pivot with the **full 3D**
offset `pivot − focus` (`ProjectionSurface::calculateTranslateMatrix`), and the pinch pivot carries
the terrain height under the finger (`TouchHandler::calculatePivotPos` → `_gestureAnchorHeight`).
Every zoom-*out* about a pivot above the focus therefore pushed the focus DOWN by
`(pivotZ − focusZ)·(scale − 1)`. Close to a slope that is a few hundred metres per gesture, and it
accumulates.

The focus height is not cosmetic: `dist(camera, focus)` is the distance the whole zoom scale is
calibrated on (`_zoom0Distance / 2^zoom`, [Near and far planes](#near-and-far-planes) above). With
the focus below the ground, that distance stops describing the distance to what is on screen — so
the tile walk asks for a zoom several levels too coarse (the blur) while every zoom-dependent width
and label size is evaluated for that same far-out zoom (the oversizing), against terrain that is
actually a tenth as far away.

**The fix** is tangram's model verbatim: the pivot moves the map **along the surface only**. Their
pinch correction is a ground translate in x/y (`View::translate`, `core/src/view/view.cpp:258`) and
their view height is derived from the zoom, so a pivot on a mountain cannot move the view point up
or down. `CameraZoomEvent` now forces the pivot to the focus's own height before building the
shift, which means it can no longer change the focus height in any mode — including the lifted
viewpoints of free roam and the peak finder, which set that height deliberately (and which the old
code could silently drag back down to the ground).

The visible trade is theirs too: pinching with a finger on a summit holds the point at the *focus
height* under the finger, so a high point drifts slightly on screen during the pinch.

**How it was found, in numbers.** A probe on `dist(camera, focus)` against `zoom0Distance / 2^zoom`,
printed once a second next to the focus and camera heights, during the gesture on the device:

```
zoom=12.15 dist=589   ratio=1.0000  focusZ=-218  camZ=76.5
zoom=11.06 dist=1259  ratio=1.0000  focusZ=-501  camZ=128.1   <- label depth to the terrain: 115
```

`ratio` staying at 1.0000 is what makes this readable: the invariant the SDK maintains was intact
the whole time — the camera distance did match the zoom. What was broken is the *unwritten* second
invariant, that the focus is on the ground you are looking at. The 1259 against a terrain depth of
115 is the entire bug.

Two things this rules out, both of which cost a round: the camera-clearance clamp (it was active
and correct — `maxTerrainZoom` tracked the zoom throughout), and the sag tesselation above (both
arms measured identical through a scripted zoom sequence — edge energy 17.4/24.7/17.2/25.0 against
18.0/24.8/17.8/24.8 — and the report predates it). A scripted `setZoom` sequence never reproduces
it either: it zooms about the focus, so there is no pivot to sink anything. The demo's
`--es anim approach` (dive, pan, pull out) is that sequence, and its clean run is what pointed at
the pivot.

**Labels partly hid it.** `Label::calculateTerrainScaleFactor` rescales a label by
`depth / focusDistance` to cancel the perspective divide, and that ratio cancels exactly this error
too (it read 0.09 while the fault was worst). Geometry, fills and vector elements have no such
cancel, which is why lines looked worse than text at first and why the 2D screenshot — where the
cancel is near 1 — was the clearer evidence.

**Residual, not fixed here.** Even with the focus where the app put it, on a z=0 plane under a
1000 m ridge the focus still sits below the ground, so `dist` still overstates the distance to what
is on screen — the same error, milder and always on. Tangram's answer is to derive the render zoom
from the terrain depth at the screen centre (`m_zoom` from `m_elevationManager->getDepth(centre)`,
clamped to `[m_baseZoom, m_maxZoom]`, `core/src/view/view.cpp:403-415`). Porting that redefines what
`getZoom()` means for tiles, styles and labels alike, so it is its own change — see
[11-tangram-diff.md](11-tangram-diff.md#the-zoom-is-calibrated-on-the-focus-not-on-the-terrain).

## The surface shader

`TerrainOptions::setSurfaceShaderSource` lets the application paint the terrain surface itself. It
replaces the background bitmap and the background colour as the base fill (precedence: shader >
bitmap > colour) and is drawn by `TerrainRenderer::renderSurface` where those are — globally,
before any tile layer, with the same `keepDepth` semantics. So a map with **no tile layer at all**
still shows shaded relief; that is the relief (peak-finder) case, and it is what
[14-post-process.md](14-post-process.md) draws its lines over.

The shader defines `vec4 surfaceColor()` and gets `v_normal` (world space), `v_worldPos`,
`v_elevation` (metres, before exaggeration), `v_dist` (metres from the camera), the resolved sun
(`u_sunDir`, `u_sunColor`, `u_sunIntensity`, `u_ambientIntensity`), the resolved fog (`u_fogColor`,
`u_fogRange`, plus a `fogAmount(dist)` helper), `u_time`, `u_zoom`, `u_resolution` and every
parameter set with `setSurfaceParameter` / `setSurfaceColorParameter`. Sun and fog come from
`resolveLighting` / `resolveFog` ([08-lighting-sky-fog.md](08-lighting-sky-fog.md)), so a shaded
surface, the tile content and the sky agree on the light. Redeclaring a provided name is a compile
error and the shader is dropped (logged, falls back to the bitmap/colour fill) — the same trap as
`SkyOptions::setShaderSource`.

Two implementation notes:

- **Normals are per-vertex and lazy.** `TerrainRenderer::ensureSurfaceAttribs` fills a
  normal + elevation array from the mesh's own height field the first time a mesh is used by the
  surface pass — central differences in tile-local space, which is a world direction because the
  tile matrix scales x, y and z alike. The depth passes never allocate it (at grid 96 it would be
  150 kB per tile).
- **Nothing else asks for elevation when there is no tile layer.** The tile layers are what
  normally drive DEM loads, so the surface pass prefetches the DEM for its own visible tiles
  (`ElevationManager::prefetchTileGrid`) and keeps requesting frames until they arrive — the same
  argument, and the same code, as the terrain paint cover. Without it the surface shades a flat
  height field and the map goes idle on it.

## Occlusion depth

Billboards and vector elements need to know whether a point is behind a ridge. The terrain is
rendered into an FBO and read back — `glReadPixels` is a full pipeline stall (55–62 ms measured), so
it runs on `TerrainDepthWorker`: its own thread, its own EGL context, deliberately **not shared**
(the pass draws CPU meshes from client memory with its own program and FBO, so a job just holds
`shared_ptr`s and nothing crosses contexts). The render thread only collects meshes (~0.8 ms).

Two GL contexts still share one GPU, so the submit interval matters more than the work:
every frame 13.2 fps, 250 ms 14.3, **500 ms 14.9** (13.7 synchronous). Tangram does the same thing
with a shared context and never waits on it.

### Query with the buffer's camera, not the frame's

Symptom: labels that should be visible fade out and come back while **zooming**, in 3D only, at any
camera (reported at 45.188/5.719 z13.18 t30 r-15). 2D never shows it because
`TileRenderer::updateLabelOcclusionTest` returns early when terrain is off — 2D runs no occlusion
test at all.

The buffer lags the camera by design (`DEPTH_SUBMIT_MOVING_INTERVAL` = 500 ms while moving, plus
worker latency), and the test used to project the label with the **current frame's** MVP and compare
that distance against it. Zooming out moves the camera farther than the 1 % tolerance floor
(`MIN_OCCLUSION_TOLERANCE`) inside those 500 ms, so every anchor reads as behind the terrain,
`updateLabel` fades it to 0, and the next read-back brings it back. Zooming in inverts the same
mismatch through screen drift: the anchor samples a pixel the old camera had something else at —
and `getDepthW` **clamped** out-of-range coordinates to the border pixel, so a label leaving the old
frustum read a border ridge instead of failing open.

`TerrainDepthBuffer` now carries the `mvpMatrix` it was rendered with, and
`TerrainRenderer::isOccludedByTerrain` projects with that matrix, samples in buffer pixels, and
fails open (not occluded) for a position behind that camera or outside its viewport. Staleness then
only makes the answer **late**, never inverted, which is what the throttle assumed all along. The
query also takes the snapshot once instead of per sample, so the five taps cannot straddle a
read-back.

Not affected: billboards and vector elements decide occlusion by ray-marching the elevation grids
from the current camera (`BillboardPlacementWorker`), which is self-consistent already.

## Draped lines sagging into the terrain (historical)

Symptom: lines do not sit on the surface — a route reads as sunk into a ridge or floating over it,
worst at low zoom, straightening as you zoom in, at any tilt.

`TerrainTileTransformer` used to have two line-subdivision paths, and only one of them was exact.
The lattice one — cut each segment exactly where it leaves a surface triangle
(`tesselateSegmentOnLattice`), so every sub-segment lies *in* one triangle — is now the only one.
The other halved segments until shorter than a threshold, and a sub-segment one mesh cell long
still chords across the cell's diagonal fold and sags below it.

The bug there was `lineDivideThreshold = divideThreshold`: lines shared the fill threshold
**including its DEM-texel floor** (`max(tileMeters / meshResolution, demTexelMeters)`). That floor
answers "how much elevation detail exists", which is the right bound for a fill but the wrong one
for the sag — the sag is against the surface **mesh**, not the DEM. Since the threshold is
proportional to the tile, the error scaled with tile size, hence better on every zoom in.

Kept as a record because the same reasoning applies to anything else measured against the surface:
bound it by the mesh cell, not by the data resolution.

Not fixed here: without the regular grid the sag is only *reduced*, never zero. Turning on
regular-grid mode is what removes it, and that is a larger change ([05-depth-model.md](05-depth-model.md)).

### What that subdivision costs over a city

**Line subdivision is the single reason panning over a city is slow.** Crosscall, the app's own
style, a 25 s scripted pan at 45.188/5.724 z15 t45, interleaved:

| | fps | GPU `layers` | geometry indices / frame |
|---|---|---|---|
| shipped | 6.6 | 51.3 ms | 2.90M |
| 3D buildings off | 6.6 | 50.7 ms | — |
| **area** subdivision off entirely | 6.7 | 50.6 ms | 2.83M |
| **lines** at source density | **13.5** | **20.9 ms** | 0.74M |
| terrain off altogether | 21.7 | 11.8 ms | 0.72M |

Fills are innocent: turning area subdivision off changes nothing, because fills are draped and baked
once. Lines are never draped — they are drawn as terrain geometry every frame — and a city is mostly
lines. In regular-grid mode the **lattice split** does the cutting, at every cell edge and diagonal:
about 64 cuts per tile crossing at z15 with `meshResolution` 64, per road.

Two things this reveals:

- **The split runs whatever the relief.** The only flatness gate is `FLAT_HEIGHT_RANGE_EPSILON`
  (0.001 m), so a valley tile is cut exactly like a cliff to protect against a fold it cannot have.
  `debug.massif.latticerelief <metres>` skips the split under a given relief: the city goes 6.61 →
  7.57 fps (`layers` 51.3 → 36.9 ms) at 200 m, and adding `debug.massif.linethreshold 8` on those
  tiles reaches **8.43 fps / 32.5 ms**. The mountain camera does not move (11.4–12.0 fps) — the gate
  never fires there, which is the point.
- **`debug.massif.linethreshold` alone does nothing** in regular-grid mode: the lattice split is tried
  first and returns, so the threshold is only a fallback for segments spanning very many cells. Any
  measurement of line cost has to go through the lattice, not the threshold.

The remaining gap to source density (8.4 against 13.5) is the tiles that legitimately have relief —
the mountains standing in the far half of a tilted city view. They are cut as finely as if they were
under the camera, because subdivision cost is per tile and **independent of the tile's size on
screen**.

### Where this should go: pay in depth, not in vertices

Tangram does not subdivide at all. `res/scenes/terrain-3d.yaml` displaces every vertex in the vertex
shader and pays for the chord with depth instead — `depth_shift = -0.02*u_proj[2][3]`, larger near
the camera where the chord error is. We already ported that shift, and we already have the better
tool for a line: `uDepthClearance`, a clearance worth the same number of METRES at any range, which
is exactly what a chord over relief needs.

What blocks using it is that `setTerrainLineClearance` is **one global value**, so it has to cover
the worst tile on screen — which is why the code notes that un-subdivided lines need a lift so large
it "shines everything through".

### Cutting a line by its sag instead of by the tile's cell count

`tesselateSegmentBySag` splits a segment only where the terrain under it actually leaves the chord,
recursively, until the residual sag is under a tolerance — expressed in METRES so it is the same
currency as the depth clearance that lifts these lines. It replaces both the lattice split and the
fixed threshold, and it is **the shipped path** since 2026-08-13:
`DEFAULT_LINE_SAG_METERS = 2`, with `debug.massif.linesag <metres>` as the override and
`debug.massif.linesag 0` going back to the old lattice split for an A/B.

**The insight is that sag measures curvature, not slope.** A road running along a constant slope
chords perfectly: its sag is zero and it needs no cut at all. Only a break in slope needs one. The
lattice, which cuts at every cell edge and diagonal, was therefore paying about 4x the geometry the
terrain's shape actually asks for.

Crosscall, the app's packaged style, 25 s scripted `--es anim pan`, three interleaved pairs.
Per-frame counts, not per interval — `RenderStats` sums over the log interval, so the faster arm
prints bigger totals ([10-performance.md](10-performance.md#getting-a-trustworthy-number)):

| | fps (3 runs) | geometry indices / frame | draws / frame |
|---|---|---|---|
| city z15 t45, lattice | 7.4 / 7.5 / 7.6 | 2.37M | 210 |
| city z15 t45, **sag 2 m** | **13.4 / 14.1 / 13.8** | **0.70M** | 213 |
| mountain z13.6 t45, lattice | 10.8–13.7 | 1.31M | 140 |
| mountain z13.6 t45, **sag 2 m** | **17.0–21.3** | **0.37M** | 140 |

Same draw count, 3.4x less geometry: the win is in what gets tesselated, not in what gets submitted.
The mountain gains as much as the city, which is the point — relief does not imply curvature.

**The tolerance is not what binds.** 0.5, 1, 2 and 4 m measure the same at both cameras (all within
the run-to-run spread, 0.37–0.70M indices/frame), so the value is chosen for margin: a draped line is
already lifted `DEFAULT_LINE_CLEARANCE_METERS` = 25 m off the surface, and 2 m is an order of
magnitude under that as well as well inside the surface mesh's own chord error. At a far tighter
setting the splitter does keep tracking (0.01 m against 0.5 m differs, 3.43M against 3.39M indices),
so it is live, not saturated.

Checked on screen at 45.244172/5.760595 z13.6 t45, z11 t60, and — the check that was missing before
it became the default — a slow 30 s pan across the ridge at z11.5 t60 with vector elements on: the
two arms are indistinguishable, no line sinking into a crest. The GeoJSON route line is broken at
z11 in BOTH arms — that is the open route-following issue, not this.

### Draping the lines, and keeping contours out of it

Cutting a line better does not change what a line *costs to shade*. With the sag split in place the
city is still fragment-bound, and the whole of it is the lines: draping them
(`TerrainOptions::DrapeLines`, `--es drapeLines true`) bakes them into the per-tile drape texture
once instead of drawing them as terrain geometry every frame, and the frame collapses.

Crosscall, packaged style, 25 s pan at the city camera (5.724/45.188 z15 t45):

| | fps | CPU frame | GPU total | GPU `layers` |
|---|---|---|---|---|
| lines as geometry (default) | 13.4–15.2 | 45 ms | 32.4–34.9 | 20.8–24.1 |
| `drape false` (nothing draped) | 12.0–13.3 | 51–59 ms | 37.3–37.6 | 28.2–28.4 |
| **`drapeLines true`** | **26.8–27.7** | 31 ms | 11.9–12.1 | **0.3** |
| `drapeLines true`, drape resolution 2048 | 24.3–26.4 | — | 13.4–14.3 | 0.3 |
| base map layer off (the floor) | 43 | — | 9.4 | 0.0 |

Draped lines land within 2.5 ms of the no-basemap floor. The cost is resolution: the bake resolves
at the drape texture's size and a slope then magnifies it. Fills and road casings survive that;
**contours do not** — they are hairline, and they smear.

Hence `GLTileRenderer::setNoDrapeLayerFilter`: style layers matching it stay OUT of the bake and are
drawn live in the 3D pass at screen resolution, exactly once (the same predicate gates the bake loop,
`hasDrapeableContent` and the 3D-pass skip). The application sets it through
**`TerrainOptions::NoDrapeLayerFilter`**, a regex over vt layer names, defaulting to `^contour.*`;
an empty string drapes everything the geometry type allows, and `adb shell setprop
debug.massif.nodrapelayers <regex>` (or `none`) overrides it for an A/B without rebuilding.

**Both defaults changed on 2026-08-13**: `DrapeLinesEnabled` is now **true**, with contours exempt.
Verified on device with no props and no intent extras — city pan 26.0–27.2 fps, GPU total 11.8–12.6
ms, `layers` 0.8 ms. An application that wants the old behaviour sets `DrapeLinesEnabled` false;
one that wants everything flattened sets `NoDrapeLayerFilter` to "".

What it costs, same runs: the city does not notice (26.8–27.7 → 22.8–26.8 fps, GPU `layers`
0.3 → 0.7 ms — there are barely any contour lines on a valley floor), the mountain pays for what it
draws (32.9–42.1 → 24.2–31.3 fps at z13.6 t45), and is still far above the 17–21 it had with
nothing draped.

Note the filter matches the **vt layer name**, which comes from the style's own rule names — a style
that calls its contour rules something else needs its own pattern.

### Putting the live layer back in its style position

Keeping a layer out of the bake also takes it out of the **order**. The drape composite for every
visible tile is baked and drawn before any live geometry, and the per-layer pass then skips whatever
went into the bake (`GLTileRenderer.cpp`, the `drapedTile && isDrapeableGeometry && isLayerDraped`
skip), so a no-drape layer can only land on top of *everything* draped. With the defaults — filter
`^contour|maneuver.*`, `DrapeLinesEnabled` true — roads are baked and contours are drawn live over
them, while 2D draws them the other way round. That is [#175](https://github.com/massif-maps/MassifMaps/issues/175).

Three fixes do not work. **Draping the contours too**: the drape is parameterised by the tile's XY,
so its ground resolution degrades as `1/cos(slope)` and contours run *along* the slope, where the
stretch is worst — no `DrapeResolution` fixes a parameterisation. **Cutting the drape** at the
topmost no-drape layer: correct order, but roads stop being draped, and that is the 13.4 → 27 fps
win in the table above. **Splitting the drape into two RGBA textures**: exact, but +4 MB per tile at
1024 against a 96 MB cache.

So: do not reorder the passes, **occlude**. The whole stack flattens into ordered units, one per
style layer of each drape layer, each draped (**D**) or live (**L**). What the frame produces today
is all D then all L, which is wrong at exactly one kind of position — a D after an L. Each L is
therefore drawn through a **coverage mask**: the accumulated alpha of every D after it, sampled in
drape-tile uv, `alpha *= 1.0 - mask`.

| where | what |
|---|---|
| `terrain/DrapeStackCuts.h` | the ordering rule — flatten, walk backwards, one mask per L→D transition. Header-only, covered by `tests/api/DrapeStackCutsTest.cpp` |
| `MapRenderer::onDrawFrame` | asks each drape layer for its D/L runs, stitches them into one sequence, bakes the masks inside `bakeTile` and hands them back before the layers draw |
| `GLTileRenderer::bakeDrapeCoverage` | the bake — the same covering tiles and transforms as `bakeDrapeTile`, restricted to the style layers at or after the cut, with the fragment stage writing alpha (`COVERAGE`) instead of colour |
| `GLTileRenderer::resolveDrapeCoverageMask` | which mask a live layer takes over a given tile, and the target-tile → mask-tile uv sub-rect |
| `TerrainDrapeCache` | stack 0 is the RGBA drape, stacks 1..K the **R8** masks — baked off the same fingerprint, so a mask can never describe a different generation of the map than the drape beside it |

Two properties make it cheap. Coverage accumulation is **order-independent** (`1-(1-a1)(1-a2)`
commutes), so the extra pass need not preserve style order internally; and consecutive L units with
no D between them **share** a mask, so K is the number of L→D transitions — 0 or 1 for every
ordinary style. The rule is derived per frame from `isLayerDraped` plus the existing drape-layer
order, so a style that puts its contours above the roads still renders that way, at zero cost.

| style order | K | result |
|---|---|---|
| landcover, hillshade, **contours**, roads | 1 | contours under roads, over hillshade |
| roads, hillshade, **contours** | 0 | contours on top — already correct, and free |
| landcover, **contours**, roads, **maneuver** | 1 | both correct; the maneuver layer has nothing draped after it |

Costs and known limits:

- **K × 1 MB per drape tile** at resolution 1024 (R8 — [ES3 is a hard requirement](https://github.com/massif-maps/MassifMaps/blob/master/CLAUDE.md#opengl-es-3-is-a-hard-requirement)
  on both platforms), against the 4 MB of the colour drape. `TerrainDrapeCache` budgets in **bytes**
  rather than entries for this reason: a count would let the masks eat a quarter of the cache's
  tiles for nothing.
- **One extra rasterisation** of the above-cut units per mask, inside the existing bake budget.
- **Exact for opaque above-layers, approximate for translucent ones** — a 50 %-alpha road gets the
  contour tinting it rather than the other way round. The exact fix is the 2×RGBA split.
- **A terrain paint contributes no coverage.** It shades the ground the other layers put in the
  drape; treating it as an occluder would hide every live layer under it outright.
- **A drape tile finer than the geometry's render tile** would need several masks in one draw. That
  draw keeps the pre-#175 behaviour and is drawn on top.
- **Capped at 2 masks** (`MAX_DRAPE_COVERAGE_MASKS`); a deeper cut is dropped, logged once, and its
  layer draws on top as before.
- **A style with an opaque layer above the contours hides them completely.** Correct, and what 2D
  already does, but it reads as a bug the first time.

A/B: `adb shell setprop debug.massif.drapemask 0` and relaunch puts the live layers back on top of
the whole drape.
