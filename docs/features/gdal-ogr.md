---
title: GDAL & OGR sources
description: "Reading GeoTIFF rasters and OGR vector files (shapefile, GeoPackage, GeoJSON) straight from disk, behind an opt-in build profile."
sidebar_position: 15
---

# GDAL & OGR sources

Two data sources that read GIS files directly, without tiling them first:

| Class | Reads | Feeds |
|---|---|---|
| `GDALRasterTileDataSource` | anything GDAL opens as a raster — GeoTIFF, and so on | `RasterTileLayer` |
| `OGRVectorDataSource` | anything OGR opens as vector — shapefile, GeoPackage, GeoJSON, KML | `VectorLayer` |
| `OGRVectorDataBase` | one file with several layers, opened once and shared | `OGRVectorDataSource` |

:::warning Not in a default build
GDAL is **not** vendored with the SDK. These classes only exist when the SDK is compiled with the
`gdal` profile against a GDAL you supply — see [Building with GDAL](#building-with-gdal). A stock
release does not contain them, and asking for one of the specs below returns an unknown-type error.
:::

## The published `full+gdal` build

Releases publish a `full_gdal` variant for **Android only**, built by CI against a GDAL it
cross-builds per ABI with vcpkg:

```gradle
implementation 'com.github.massif-maps:MassifMaps-android-aar:v<version>:full_gdal'
```

There is no iOS `full+gdal`. The xcframework is five slices — device, two simulator architectures
and two Catalyst ones — and each needs its own cross-built GDAL, which vcpkg does not cover; the
iOS matrix entry is excluded rather than left to fail.

## Building with GDAL yourself

`libs-external` carries no GDAL, and never has. You build GDAL for each target yourself and point
CMake at it:

```sh
cd scripts
python3 build-android.py --profile "standard+gdal" \
  --cmake-options "CMAKE_PREFIX_PATH=/path/to/your/gdal/install"
```

`--cmake-options` takes `NAME=value` pairs separated by `;`; the script prefixes each with `-D`.

The profile adds `-D_MASSIF_GDAL_SUPPORT` and turns on `INCLUDE_GDAL`, which does
`find_package(GDAL REQUIRED)` and links `GDAL::GDAL`. Without the profile the seven translation
units are removed from the source glob entirely, so a build that does not want GDAL pays nothing
for it.

The SWIG wrappers follow the same define, so regenerate them with the matching profile or the
bindings will not contain these classes:

```sh
cd scripts && python3 swigpp-java.py --profile "standard+gdal" --swig /path/to/mobile-swig/swig
```

## A GeoTIFF as a raster layer

```java
GDALRasterTileDataSource source = new GDALRasterTileDataSource(0, 18, "/sdcard/dem.tif");
mapView.getLayers().add(new RasterTileLayer(source));
```

The zoom range is yours to pick; the source reprojects into the map's tile grid on demand. The
four-argument constructor takes an explicit SRS for a file whose own projection is missing or
wrong.

## A shapefile as vector elements

`OGRVectorDataSource` turns each feature into a `VectorElement`, and a **style selector** decides
which style each one gets — that is what `StyleSelectorBuilder` is for:

```java
MarkerStyleBuilder markerStyleBuilder = new MarkerStyleBuilder();
markerStyleBuilder.setSize(20);

StyleSelectorBuilder selectorBuilder = new StyleSelectorBuilder();
selectorBuilder.addRule("population > 100000", markerStyleBuilder.buildStyle());
selectorBuilder.addRule(smallMarkerStyle);   // the fallback, matched last

OGRVectorDataSource source = new OGRVectorDataSource(
        projection, selectorBuilder.buildSelector(), "/sdcard/places.shp");
mapView.getLayers().add(new VectorLayer(source));
```

A rule's expression is evaluated against the feature's own fields, so `population > 100000` reads
the `population` column of the shapefile. Rules are tried in order and the first match wins; a rule
added with no expression matches everything and belongs last.

Open a multi-layer file once and share it:

```java
OGRVectorDataBase database = new OGRVectorDataBase("/sdcard/data.gpkg", false);
for (int i = 0; i < database.getLayerCount(); i++) {
    mapView.getLayers().add(new VectorLayer(
            new OGRVectorDataSource(projection, selector, database, i)));
}
```

## On the facade API

All four constructible classes carry specs, so a binding builds them from JSON like any other
source:

```ts
map.buildLayer('layer.dem', {
    type: 'raster',
    source: { type: 'gdal', path: '/sdcard/dem.tif', minZoom: 0, maxZoom: 18 }
});

// A projection has no spec type of its own, so it comes from the map as a handle.
const projection = map.child('options.baseProjection');

map.buildLayer('layer.places', {
    type: 'elements',
    source: {
        type: 'ogr',
        path: '/sdcard/places.shp',
        projection: projection.handle,
        style: { type: 'style-selector' }
    }
});
```

| Spec `type` | Kind | Builds |
|---|---|---|
| `gdal` | `source` | `GDALRasterTileDataSource` |
| `ogr` | `source` | `OGRVectorDataSource` |
| `ogr-database` | `data` | `OGRVectorDataBase` |
| `style-selector` | `elementstyle` | `StyleSelectorBuilder` |

`StyleSelector` itself has no spec `type` of its own (`-`), exactly like `MarkerStyle` and
`BalloonPopupStyle`: its constructor is `%ignore`d and the builder is the only way in.

## Known gaps

- **No vendored GDAL.** CI cross-builds it with vcpkg per ABI, and anyone building locally supplies
  their own. Vendoring a minimal GDAL and PROJ into `libs-external` is the obvious follow-up and is
  a build-system project of its own; the binary-size cost is why it is not done here.
- **iOS is not wired** — see above.
- **No `child()` in the Java facade.** `OGRVectorDataSource` needs a `Projection`, and Java has no
  way to read an object property *as an object* the way TypeScript's `child()` does. So the Android
  example demonstrates the GDAL raster only; the OGR vector case is reachable from the object API
  and from TypeScript, but not from a Java spec.
- **Untested on device by the fork.** These classes were dead code for the life of the fork —
  `_CARTO_GDAL_SUPPORT` was referenced only inside `#ifdef`s and defined by no build. They compile
  and the facade tables generate, but nothing here has opened a real GeoTIFF.
- **`all/native/assets/gdal/`** holds ~7 MB of PROJ CSV tables as headers, included by nothing. They
  belong to a GDAL build that supplies its own `proj.db`, and should probably go.
- The style-selector expression language is the SDK's own, not OGR SQL, and is documented only by
  `StyleSelectorContext`.
