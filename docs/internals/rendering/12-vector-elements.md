---
title: Vector elements
description: Markers, popups, app-drawn lines and polygons, billboards and picking.
sidebar_position: 12
---

# Vector elements, billboards, and picking

Scope: everything drawn from app-supplied objects rather than from tiles — markers, popups, labels
placed by the app, lines, polygons, 3D polygons, NML models — plus how a tap is turned into a hit.

## The renderers

`VectorLayer` holds a set of `VectorElement`s and hands each to a renderer by kind
(`all/native/renderers/`):

| renderer | draws |
|---|---|
| `PointRenderer` | points |
| `LineRenderer` | lines |
| `PolygonRenderer` | filled polygons |
| `Polygon3DRenderer` | extruded polygons |
| `GeometryCollectionRenderer` | mixed collections, delegating per geometry |
| `BillboardRenderer` | markers, popups, labels, balloons, NML models — anything screen-facing (`NMLModel` is a `Billboard`) |

Elements are converted to *draw data* (`renderers/drawdatas/`) when they change, and the renderer
consumes draw data, not the elements themselves — so an element edited from the app thread never
races the render thread.

## Billboards

Billboards are screen-facing, so they need placement (where on screen, does it collide) rather than
just a transform. `BillboardPlacementWorker` recomputes that off the render thread; the frame kicks
it when `MapRenderer::billboardsChanged` has been set.

`BillboardSorter` orders them back-to-front for correct alpha blending — billboards are drawn after
the layer's geometry, in a pass of their own (`billboards` in `PROF`).

## Text on a billboard

`BalloonPopup`, `Text` and the popup buttons rasterize their text into a bitmap with `BitmapCanvas`,
which is the **platform** text API (Android `Typeface`, iOS CoreText, UWP DirectWrite) — not the
FreeType/HarfBuzz path the tile labels use. Their font names go through the same list syntax and the
same system-font matching as a style's `text-face-name`; see
[06-labels.mdx](06-labels.mdx#font-names-fork-specific).

## Terrain interaction

Two mechanisms, and they are different things:

**1. Occlusion (billboards).** A marker behind a ridge must fade out. The terrain depth is rendered
into an FBO and read back (`TerrainRenderer`, `sampleDepthW`), and each billboard's
draw data carries a `terrainOcclusionOpacity` that is *animated* toward the target
(`updateTerrainOcclusionOpacity`) rather than switched, so a marker crossing a crest fades instead of
blinking. The read-back is a pipeline stall, so it runs on `TerrainDepthWorker` at an interval and
the occlusion is allowed to lag a gesture — the depth texture is a half-resolution approximation
sampled with a mesh capped at `DEPTH_TEXTURE_MESH_RESOLUTION = 32`. See
[04-terrain.md](04-terrain.md#occlusion-depth).

**2. Depth (geometry elements).** Lines and polygons drawn on the terrain are displaced like tile
content and depth-tested against the ground. They get their own small forward bias, deliberately
**much smaller** than tile content's: the eye tolerance of a constant-NDC bias grows as
distance²/near, and a large one is exactly how a vector line shows through a ridge from the far side.
Their clip-space slack is **not** scaled with `MeshResolution` the way tile content's is, because an
element's chord error follows its own tesselation, not the terrain mesh's — scaling it over-clipped
lines on shoulders when it was tried.

If a vector line disappears into the terrain or shows through it, read
[05-depth-model.md](05-depth-model.md) before touching any constant here.

**3. The elevation warm-up.** `VectorLayer::FetchTask::loadElements` loads the elevation under the
elements before their draw data is built, on the fetch thread, so a line is not drawn half draped
(vertices with and without heights give near-vertical segments). It samples the bounds of each
loaded element. It used to sample a 4×4 grid over the **cull envelope** instead, and in terrain mode
that envelope reaches the view distance: its corners land hundreds of km away, off the DEM coverage,
so every fetch task blocked on elevation tiles that could only 404 — measured on a Crosscall, 15
failing HTTP round trips per startup, re-issued each time the failure markers expired
(`FAILED_TILE_TTL_MILLISECONDS`, 30 s) while panning.

## Picking

`MapRenderer::calculateRayIntersectedElements` turns a screen position into a world ray and asks
every layer:

- **Vector layers** intersect the ray against their elements' geometry.
- **Tile layers** (`TileLayer::calculateRayIntersectedElements`) intersect against tile content;
  for raster/bitmap hits `GLTileRenderer::findTileBitmapIntersections` walks the tile's **CPU surface
  meshes** to find where the ray meets the displaced ground.
- `TileLayer::processClick` then decides what to report to the app.

**Known gap:** in regular-grid mode (which is every terrain configuration we ship) the per-tile CPU
surfaces are never built, so `_tileSurfaceMap` is empty and the bitmap intersection path finds
nothing. The fix is to build a tile's surface lazily on the pick — a pick is a user gesture, not a
frame, so it can afford it — or to serve the query from the terrain depth read-back the way tangram
does (`ElevationManager::elevationLerp` over the texture's own CPU buffer, with a one-entry memo).

## UTF grids

`TileLayer::setUTFGridDataSource` attaches a UTF-grid source for feature interactivity; hits are
resolved from the grid rather than from geometry, and reported through `UTFGridEventListener`.

## App content that is NOT a vector element

Content the app owns can also be served as vector tiles from an in-memory source and styled by
CartoCSS — which keeps it on the tile path (terrain tesselation, shared drape, style-driven z-order)
instead of this one, and lets style properties do work an element would need new C++ for. Navigation
maneuver arrows are built that way: see [15-maneuver-arrows.md](15-maneuver-arrows.md).

