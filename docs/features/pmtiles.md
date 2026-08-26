---
title: PMTiles
sidebar_position: 6
---

# PMTiles

`PMTilesTileDataSource` reads **[PMTiles](https://github.com/protomaps/PMTiles) v3** archives — a
single-file, cloud-optimized tile pyramid format. Drop it in anywhere a `TileDataSource` is
expected (vector or raster).

:::info Fork feature
Added by the fork. Full technical notes:
[`docs/internals/pmtiles-datasource.md`](/docs/internals/pmtiles-datasource).
:::

## Why PMTiles

- **One file** for a whole tile pyramid — easy to ship, host or embed.
- **Random access** via a Hilbert-curve directory — no server needed, works off local storage.
- **Any tile type** — MVT, PNG, JPEG, WebP, AVIF.
- **Compression**: none / gzip / brotli / zstd, for both directories and tile data.

## Usage

```kotlin
import com.massifmaps.datasources.PMTilesTileDataSource
import com.massifmaps.layers.VectorTileLayer

// Auto-detect zoom range from the archive…
val source = PMTilesTileDataSource("/sdcard/maps/basemap.pmtiles")

// …or specify it explicitly.
// val source = PMTilesTileDataSource(0, 14, "/sdcard/maps/basemap.pmtiles")

val layer = VectorTileLayer(source, decoder)
mapView.layers.add(layer)
```

```swift
let source = MSFPMTilesTileDataSource(path: "\(NSHomeDirectory())/maps/basemap.pmtiles")
let layer = MSFVectorTileLayer(dataSource: source, decoder: decoder)
mapView.getLayers()?.add(layer)
```

## From JSON

`pmtiles` is a `source` spec type of the [facade API](/docs/internals/api-facade), so a
NativeScript / React Native / C ABI caller builds one from JSON, with no native construction and
no `adopt` step:

```json
{"type":"vector",
 "source":{"type":"pmtiles","path":"/sdcard/maps/basemap.pmtiles"},
 "style":{"type":"mbvt","project":{"type":"project",
          "assets":{"type":"dir","path":"/sdcard/massif_style"},"name":"osm"}}}
```

`minZoom` / `maxZoom` are accepted like every other source, but `getMinZoom()`/`getMaxZoom()` read
the archive header either way, so leaving them out is the normal case.

## Metadata & extent

```kotlin
val name = source.getContainerMetaData("name")
val attribution = source.getContainerMetaData("attribution")
val minZoom = source.minZoom
val maxZoom = source.maxZoom
val bounds = source.dataExtent
```

## Creating PMTiles files

Use the [go-pmtiles CLI](https://github.com/protomaps/go-pmtiles) to convert an existing MBTiles:

```bash
# Convert MBTiles → PMTiles
pmtiles convert input.mbtiles output.pmtiles --compression=gzip

# Higher ratio (brotli) or fast (zstd)
pmtiles convert input.mbtiles output.pmtiles --compression=brotli
pmtiles convert input.mbtiles output.pmtiles --compression=zstd
```

All four compression modes (none / gzip / brotli / zstd) are supported for both the internal
directories and the tile data.

## See also

- [Offline Maps](/docs/guides/offline-maps) — MBTiles and offline packages.
- [Layers & Data Sources](/docs/guides/layers-and-data-sources) — how sources feed layers.
