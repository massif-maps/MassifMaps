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
`gdal` profile against a GDAL you supply — see [Building with GDAL](#building-with-gdal-yourself). A stock
release does not contain them, and asking for one of the specs below returns an unknown-type error.
:::

## The published `full+gdal` build

Releases publish a `full_gdal` variant for **Android only**, built by CI against a GDAL it
cross-builds per ABI with `scripts/build-gdal-android.py`:

```gradle
implementation 'com.github.massif-maps:MassifMaps-android-aar:v<version>:full_gdal'
```

That variant also carries the **NML model LOD tree** (`nml-lodtree` and its two sources). Not a
dependency — the LOD tree needs only `nml` and `sqlite3pp` — but `full+gdal` is the one kitchen-sink
build that is published, and the LOD tree has to be in a shipped profile to be reachable at all.
Compose `nmlmodellodtree` on its own to get it without GDAL.

There is no iOS `full+gdal`. The xcframework is five slices — device, two simulator architectures
and two Catalyst ones — and each needs its own cross-built GDAL. `build-gdal-android.py` has no
iOS counterpart yet, so the iOS matrix entry is excluded rather than left to fail.

## Building with GDAL yourself

`libs-external` carries no GDAL, and never has. On Android one script cross-builds GDAL and PROJ
for every ABI from release tarballs - no submodule, nothing added to `libs-external`:

```sh
cd scripts
python3 build-gdal-android.py --ndk "$ANDROID_NDK_HOME"
python3 build-android.py --profile "standard+gdal" \
  --cmake-options "CMAKE_PREFIX_PATH=$PWD/../build/gdal/{abi}"
```

`{abi}` expands per ABI, which is what lets one option point at four different prefixes. GDAL is
built with its internal libtiff/libgeotiff/libjson and a driver set of GTiff, VRT, MEM, RAW,
Shapefile, GeoJSON, GPKG and SQLite; everything optional is off.

On any other platform, build GDAL for the target yourself and point CMake at it:

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

## `proj.db` — required, and not yet delivered to the device

PROJ 6+ keeps its CRS definitions in a **`proj.db`** SQLite database, and
`GDALRasterTileDataSource` calls `importFromEPSG(3857)` on every construction. Without the database
that lookup fails, the source logs one error and reprojects nothing. It is not optional.

`build-gdal-android.py` writes it to `build/gdal/proj.db` (~9 MB, architecture-independent). **What
is missing is the last step**: the app has to ship it and PROJ has to be told where it is, and the
SDK does neither today — nothing calls `proj_context_set_search_paths` and nothing sets `PROJ_LIB`.
Until that exists, a `full_gdal` build reprojects nothing.

Trimming the database to a handful of CRSs is possible and would cut most of the 9 MB, but it has
not been attempted here.

## Known gaps

- **`proj.db` is not wired to the device** — see above. This is the one that blocks the feature
  actually working.
- **No vendored GDAL.** `scripts/build-gdal-android.py` cross-builds it per ABI from release
  tarballs — no submodule, nothing in `libs-external`. Vendoring a minimal GDAL and PROJ as
  subprojects is the follow-up, and would give iOS its five slices for free.
- **iOS is not wired** — see above.
- **Untested on device by the fork.** These classes were dead code for the life of the fork —
  `_CARTO_GDAL_SUPPORT` was referenced only inside `#ifdef`s and defined by no build. They compile
  and the facade tables generate, but nothing here has opened a real GeoTIFF.
- The style-selector expression language is the SDK's own, not OGR SQL, and is documented only by
  `StyleSelectorContext`.
