---
title: 3D Terrain
sidebar_position: 1
---

# 3D Terrain

Render the map draped over **real elevation**, with correct depth occlusion (near ridges hide
far slopes), fill draping and fast zooming.

<figure class="docs-figure">

![3D terrain over the Chartreuse massif, Grenoble](/img/features/terrain-hero.jpg)

<figcaption>3D terrain over the Chartreuse massif above Grenoble — hillshade relief, contour lines and a route line draped onto the surface. Captured from the <code>scripts/android-dev</code> demo.</figcaption>

</figure>

<figure class="docs-figure">

<video controls muted loop playsinline width="360" poster="/MassifMaps/img/features/terrain-3d.jpg">
  <source src="/MassifMaps/img/features/terrain-demo.mp4" type="video/mp4" />
</video>

<figcaption>Panning across the tilted 3D terrain (demo capture).</figcaption>

</figure>

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
    meshResolution = 64               // grid cells per tile edge (2..256)
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
| `MeshResolution` | `32` | Grid cells per tile edge, clamped `2..256`. Limited by DEM resolution. |
| `DrapeFillsEnabled` | `true` | Render-to-texture fill/background draping. |
| `DrapeLinesEnabled` | `true` | Drape tile lines too (softer, zero-cost hug). |
| `DrapeResolution` | `0` | Drape texture size; `0` = derived from the tile size. |
| `NoDrapeLayerFilter` | — | Layer-name pattern kept out of the drape (sharp geometry). |
| `SeamlessTileEdgesEnabled` | `true` | Backfill the 1-texel DEM border from the neighbour level — removes the ridge at tile borders. |
| `ElevationPrefetchEnabled` | `true` | Also request the neighbours of every visible terrain tile. |
| `TileEdgeStitchingEnabled` | `true` | Stitch the mesh across tiles of different levels. |
| `BackgroundColor` | transparent | Fill color drawn before tiles (works even with zero tile layers). |
| `BackgroundBitmapEnabled` | `false` | Drape `Options.getBackgroundBitmap()` over the terrain (world-anchored, repeats). |
| `ViewDistanceFactor` / `ViewDistance` | `1.0` / `0` | Where the ground ends; `ViewDistance` overrides in metres. Pair it with fog ([FogOptions](/docs/features/sky-sun-shadows)). |
| `MaxTileZoomCoarsening` | `3` | How much coarser far tiles may get. |
| `BillboardOcclusionEnabled` / `…Tolerance` | `true` / `0.02` | Hide markers and popups behind a ridge. |
| `SurfaceShaderSource` | — | Replace the terrain surface shader ([post-processing](/docs/features/post-processing)). |
| `MinZoom` / `DepthBias` | `5` / `0.0002` | LOD floor and depth slack for draped geometry. |

Recommended configuration:

```java
terrainOptions.setMeshResolution(64);   // the defaults already drape fills and lines
```

## Querying elevation

`TerrainOptions` (and the shared `ElevationManager`) can return heights for map positions:

```kotlin
val metres: Double = terrain.getElevation(mapPos)          // single
val many: DoubleVector = terrain.getElevations(mapPosVector) // batched
```

## Performance notes

- **Draped content skips terrain subdivision** (it is baked flat), so vertex buffers upload at
  source density instead of ~`meshResolution²` per tile. This, plus an LRU elevation-texture cache
  (no full flush), removes most of the fast-zoom render-thread stall.
- Prefer `meshResolution = 64` as a good quality/cost balance; go higher only if you see terraced
  slopes at close range.
- The shared regular grid and the painter-order depth model are always on where the GPU supports
  vertex texture fetch; there is no per-tile CPU tesselation to pay for there.

## Known limitation

At low zoom, `VectorLayer` **element** lines (e.g. long routes) can still leak through a ridge:
they are CPU-baked to a fine bilinear surface while the low-zoom occluder is coarse, and no depth
bias wins that case. The fix (render-to-texture element draping across layers) is on the roadmap.

## Styling differently in 3D

A style reads `[render::3d]` to branch on whether it is drawn on terrain — flat labels in 2D,
billboard ones in 3D, for instance. See
[Live Style Parameters](/docs/features/style-parameters#the-render-mode-variable-render3d).

## See also

- [Hillshade](/docs/features/hillshade) — shaded relief from the same DEM.
- [On-the-fly Contours](/docs/features/contours) — contour lines that drape over terrain.
- [Composite Vector Tile Layer](/docs/features/composite-vector-tile-layer) — mix terrain-aware sources into one style.
