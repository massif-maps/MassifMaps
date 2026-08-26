---
title: MapLibre Tiles (MLT)
sidebar_position: 14
---

# MapLibre Tiles (MLT)

`MBVectorTileDecoder` reads **MapLibre Tiles** as well as MapBox Vector Tiles. Everything past the
decode — CartoCSS, symbolizers, `param::` parameters, labels, terrain draping — is shared, so an MLT
tileset styles exactly like an MVT one.

:::info Fork feature
Added in PR [#90](https://github.com/massif-maps/MassifMaps/pull/90), on the
[maplibre-tile-spec](https://github.com/maplibre/maplibre-tile-spec) decoder vendored under
`libs-external/mlt`. Technical notes:
[`docs/internals/rendering/02-tiles.md`](/docs/internals/rendering/tiles#two-binary-formats-mvt-and-mlt).
:::

## Usage

Nothing to do in the common case — point a `VectorTileLayer` at an MLT source and it works:

```kotlin
val source = HTTPTileDataSource(0, 14, "https://your.tiles/{z}/{x}/{y}.mlt")
mapView.layers.add(VectorTileLayer(source, MBVectorTileDecoder(styleSet)))
```

Pin the format when the source is known — it skips the check and cannot be fooled:

```kotlin
decoder.tileFormat = TileFormat.TILE_FORMAT_MLT   // or TILE_FORMAT_MVT / TILE_FORMAT_AUTO
```

| Value | Meaning |
|---|---|
| `TILE_FORMAT_AUTO` | default: the source's declared format, else per-tile detection |
| `TILE_FORMAT_MVT` | MapBox Vector Tile (protobuf) |
| `TILE_FORMAT_MLT` | MapLibre Tile (columnar) |

### With the surface API

`tileFormat` is a property of the `mbvt` style, so pinning it is one key in the spec:

```java
map.addLayer("basemap", Spec.of("vector")
    .set("source", Spec.of("http")
        .set("url", "https://your.tiles/{z}/{x}/{y}.mlt")
        .set("maxZoom", 14))
    .set("style", Spec.of("mbvt")
        .set("project", "outdoor-project")
        .set("tileFormat", "TILE_FORMAT_MLT")));
```

:::caution One format per decoder
`setTileFormat` pins the decoder, not the layer. A decoder shared between layers whose sources
differ in format must stay on `TILE_FORMAT_AUTO` — the check costs 1–19 ns per tile.
:::

## How the format is chosen

1. **A source that declares its format wins.** The `VectorTileLayer` constructor reads the
   container's metadata — `encoding` first, then `format` (the MBTiles/PMTiles table, forwarded by
   the wrapper sources) — and pins the decoder when either is conclusive. A format the app set
   explicitly is left alone.
   Matching is case-insensitive and by substring; **`pbf` is deliberately inconclusive**, because
   MapLibre's own demotiles declare `"format": "pbf"` with `"encoding": "mlt"`.
2. **Otherwise the tile itself is checked.** There is no magic number, but the framing separates
   them: an MLT tile is a sequence of `(varint layer length, varint layer tag, body)` that tiles the
   buffer exactly, which a protobuf MVT does not fit. Measured against the maplibre-tile-spec
   fixture corpus (663 `.mlt` + 134 `.mvt`): **663/663 detected, 0/134 false positives**.

   That corpus's MVT tiles all come from one encoder family, so an exotic MVT producer has not been
   tested — set the format explicitly when you know it.

## Behaviour differences to expect

- **MLT decodes the whole tile.** The format has no lazy path: every layer and every property
  column comes back. MVT decodes per layer, on demand, and only the attributes the style asked for
  — on an OpenMapTiles schema with a `name:*` column per language that is a real difference, and it
  is not benchmarked yet.
- **No attribute-set dedup on the MLT side** — MVT dedups identical attribute sets by their tag
  indices; MLT properties are columnar and carry no equivalent key. The geometry cache is kept for
  both.
- **Pre-tessellated triangles are ignored** for now — the renderer still tessellates itself.

## Build

MLT requires **C++20** (the decoder's public headers use `std::span` and `std::ranges`); Android,
Apple and UWP builds moved to `-std=c++20` for it. The decoder is vendored decoder-only and adds
nothing to a binary that never references it (`--gc-sections`); measured on its own it is 254 KB of
arm64 text+rodata+data at `-Oz`.

Only `cpp/` of `libs-external/mlt/mlt` is needed and its test fixtures are 142 MB, so restrict the
checkout once:

```bash
git -C libs-external/mlt/mlt sparse-checkout set cpp
```

## See also

- [PMTiles](/docs/features/pmtiles) — a container that can serve either format.
- [Composite Vector Tile Layer](/docs/features/composite-vector-tile-layer) — mixing sources into one style.
