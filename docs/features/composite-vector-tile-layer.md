---
title: Composite Vector Tile Layer
sidebar_position: 3
---

# Composite Vector Tile Layer

`CompositeVectorTileLayer` is a `VectorTileLayer` that **weaves external data sources**
(raster, hillshade, extra vector / contour) into a master CartoCSS style's layer order. Each
external source is placed at the slot of a matching **layer name** and configured from a
`#name { … }` block in the style — including zoom- and `param::`-dependent expressions.

:::info Fork feature
Added in PR [#19](https://github.com/massif-maps/MassifMaps/pull/19). Pairs with the libs-massif
config symbolizers (submodule PR `massif-maps/massif-maps-libs#5`). Full property reference:
[`docs/features/composite-layer-reference.md`](/docs/features/composite-layer-reference).
:::

## Why

Normally, mixing a hillshade or raster overlay into a vector basemap means juggling several
independent `Layer`s and keeping their z-order and opacity in sync by hand. `CompositeVectorTileLayer`
lets a **single CartoCSS style** own the whole stack: you name a layer in the style, attach a source
to that name, and the SDK renders it in the right slot with style-driven configuration.

## Usage

```kotlin
import com.massifmaps.layers.CompositeVectorTileLayer
import com.massifmaps.layers.CompositeSourceType
import com.massifmaps.datasources.HTTPTileDataSource

// Master style + its base vector source, as for a normal VectorTileLayer.
val composite = CompositeVectorTileLayer(baseVectorSource, mbVectorTileDecoder)

// Raster overlay placed at the style layer named "satellite".
composite.addExternalDataSource(
    "satellite",
    HTTPTileDataSource(0, 19, "https://your.tiles/sat/{z}/{x}/{y}.jpg"),
    CompositeSourceType.COMPOSITE_SOURCE_TYPE_RASTER
)

// Hillshade placed at the style layer named "hillshade". The elevation decoder is
// resolved per tile from the DEM's "dem_encoding" meta data (pass one to override
// the source-level default).
composite.addExternalDataSource(
    "hillshade",
    demSource,
    CompositeSourceType.COMPOSITE_SOURCE_TYPE_HILLSHADE
)

// An extra vector/contour source, master-styled and filtered to its layer name.
composite.addExternalDataSource(
    "contour",
    contourSource,
    CompositeSourceType.COMPOSITE_SOURCE_TYPE_VECTOR
)

mapView.layers.add(composite)
```

Sources are **dynamic** — add or remove them at runtime:

```kotlin
composite.removeExternalDataSource("hillshade")
composite.addVectorDataSource("labels", extraLabelsSource) // shortcut for VECTOR type
```

### With the surface API

The layer itself is a spec:

```java
map.addLayer("base", Spec.of("composite-vector")
    .set("source", "osm")
    .set("style", "outdoor"));
```

:::note External sources are still object-API
`addExternalDataSource` takes a `TileDataSource` and a `CompositeSourceType`, and carries no
generated method entry — the [layer reference](/docs/api/reference/layer) lists what is
reachable. Use `rawLayer` / the object API for the external sources themselves, and the spec for
everything else.
:::

### Source types

| Type | Renders as | Notes |
|---|---|---|
| `COMPOSITE_SOURCE_TYPE_RASTER` | `RasterTileLayer` | Styled by `raster-*` properties. |
| `COMPOSITE_SOURCE_TYPE_HILLSHADE` | `HillshadeRasterTileLayer` | Decoder from DEM `encoding`; `hillshade-*` properties. |
| `COMPOSITE_SOURCE_TYPE_VECTOR` | own `VectorTileLayer` | Master-styled, filtered to its layer name, overzooms independently via its `MaxOverzoomLevel`. |

## Styling from CartoCSS

Each external source is configured by a block whose selector matches its name. Properties are
evaluated **per frame**, so `[view::zoom]` and `param::` parameters animate smoothly:

```css
#satellite {
  raster-opacity: 0.6;
}

#hillshade {
  hillshade-exaggeration: 1.4;
  hillshade-illumination-direction: 315;
}

#contour {
  contour-base-interval: 100;
}
```

Supported property families include `raster-opacity`, `hillshade-exaggeration` /
`-illumination-direction` / …, and `contour-base-interval` / …. `[view::zoom]` expressions and
`param::` bundle parameters are supported. See the
[full config reference](/docs/features/composite-layer-reference)
for every property, smooth-vs-per-zoom-level timing, and interpolation syntax.

### Smooth hillshade exaggeration

The fork also adds `HillshadeRasterTileLayer.getExaggeration()` / `setExaggeration()` — a
per-frame relief factor applied as a shader uniform (no re-decode, default `1.0`), which is what
lets `hillshade-exaggeration` animate without a tile rebuild.

## How it renders

The tile-build-time `rendererLayerFilter` means one renderer can't be re-filtered per frame, so
each style-layer *group* renders on its own stable-filtered layer: group 0 on the composite layer
itself, later groups on internal `VectorTileLayer`s over the base source, with the external children
drawn between them in painter order. The style background is drawn once by the bottom group. This
works in **2D and over [3D terrain](/docs/features/3d-terrain)**.

## Known limitations & follow-ups

- The master style is decoded once **per style-layer group** (groups = external slots + 1). A
  single-pass renderer (one decode) is a scoped follow-up.
- `raster-comp-op` is parsed but not yet applied.
- New API surfaces via SWIG — regenerate proxies (`swigpp-*.py`) when building from source:
  `CompositeVectorTileLayer` and `HillshadeRasterTileLayer.Exaggeration`.
