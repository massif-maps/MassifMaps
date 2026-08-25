---
title: Composite Layer — Style Reference
description: Every CartoCSS property a CompositeVectorTileLayer external source accepts, when it takes effect, and the interpolation syntax.
sidebar_position: 3.1
---

# CompositeVectorTileLayer — style configuration reference

`CompositeVectorTileLayer` is a `VectorTileLayer` that weaves named external data sources
(raster, hillshade, extra vector / contour) into a master CartoCSS style's layer order.
Each external source is placed at the position of a matching layer name in the style, and
configured by a matching `#name { … }` block — including zoom- and style-parameter-dependent
expressions.

## 1. Registering sources (Java API)

```java
MBVectorTileDecoder decoder = new MBVectorTileDecoder(styleSet); // master style
CompositeVectorTileLayer layer = new CompositeVectorTileLayer(baseVectorSource, decoder);

// raster / hillshade: drawn as their own child layer at the '#name' slot
layer.addExternalDataSource("satellite", rasterSource, CompositeSourceType.COMPOSITE_SOURCE_TYPE_RASTER);
layer.addExternalDataSource("hillshade", demSource,   CompositeSourceType.COMPOSITE_SOURCE_TYPE_HILLSHADE);
//   (hillshade decoder arg optional; if omitted each tile resolves it from its "dem_encoding")

// vector (incl. contour): its own child VectorTileLayer over its own source, master-styled,
//   filtered to its layer name, overzooming independently via its MaxOverzoomLevel
layer.addVectorDataSource("contour", new ContourTileDataSource(demSource));

layer.removeExternalDataSource("satellite");     // dynamic remove
layer.getExternalDataSourceNames();              // list

mapView.getLayers().add(layer);
```

## 2. Placement — the layer order

An external source is drawn **only if its name appears in the master style's layer order**,
and it is drawn at that position:

- **Bundle style** (`CompiledStyleSet`, project JSON): the `"layers"` array defines order and
  which layers exist. Add the external name there. **Note:** the array is reversed into draw
  order — the *last* entry is the bottom layer, the *first* entry is on top.
- **Raw CartoCSS string** (`CartoCSSStyleSet`): order = first `#name` reference in the CSS
  (top of file = bottom of map). No `"layers"` array; also **no style parameters** (see §6).

A registered source whose name is not in the layer order is skipped (with a warning).

## 3. Source types & config properties

### Raster (`COMPOSITE_SOURCE_TYPE_RASTER`)
Drawn as a `RasterTileLayer`.

| CartoCSS property | Effect | Timing |
|---|---|---|
| `raster-opacity` | layer opacity 0..1 | per-frame (smooth) |
| `raster-filter-mode` | `nearest` \| `bilinear` \| `bicubic` | per-frame |
| `raster-visible` | `true`/`false` hard toggle | per-frame |

> `raster-comp-op` is parsed but not yet applied (RasterTileLayer has no blend-mode setter);
> use `raster-opacity` for blending.

### Hillshade (`COMPOSITE_SOURCE_TYPE_HILLSHADE`)
Drawn as a `HillshadeRasterTileLayer`. Decoder resolved from the DEM source `encoding`
(`terrarium`/`mapbox`) unless passed explicitly.

| CartoCSS property | Effect | Timing |
|---|---|---|
| `hillshade-opacity` | layer opacity | per-frame (smooth) |
| `hillshade-exaggeration` | relief strength multiplier (shader uniform) | **per-frame (smooth)** |
| `hillshade-illumination-direction` | light azimuth in degrees (0 = N, clockwise; 45° altitude) | per-frame (smooth) |
| `hillshade-shadow-color` / `-highlight-color` / `-accent-color` | shading palette | per-frame |
| `hillshade-method` | `standard`\|`combined`\|`igor`\|`multidirectional`\|`basic` | per-frame |
| `hillshade-contour-color` / `-contour-width` | GPU contour lines (in the hillshade pass) | per-frame |
| `hillshade-height-scale` | raw normal-map height scale (baked at decode) | **per zoom level** |
| `hillshade-contrast` | shading contrast (baked at decode) | **per zoom level** |
| `hillshade-contour-interval` | GPU contour interval, 0 = off (toggles decode) | **per zoom level** |
| `hillshade-visible` | hard toggle | per-frame |

**Exaggeration vs height-scale:** `hillshade-exaggeration` is the one to animate — it is a
per-frame shader uniform (default 1.0 = normal look, higher = stronger), so
`linear([view::zoom], (4, 0.6), (12, 1.4))` glides smoothly. `hillshade-height-scale` is the
raw geometric scale baked into the normal map; changing it re-decodes tiles, so it only
updates when the integer zoom changes (per-zoom-level steps), never continuously.

### Vector / contour (`COMPOSITE_SOURCE_TYPE_VECTOR`, or `addVectorDataSource`)
Rendered as its own child `VectorTileLayer` over its own source, styled by the master CSS,
filtered to its layer name. Style it with the normal vector symbolizers on `#name`, e.g. for a
`ContourTileDataSource` (default layer name `contour`):

```css
#contour['view::zoom'>=12] {
  line-color: #9a5a12; line-width: 0.8; line-opacity: 0.7;
  text-name: [ele]; text-face-name: "Noto Sans Regular"; text-size: 9;  // needs a font asset
}
```

Optional `ContourTileDataSource` generation parameters (applied to the data source, not
per frame — they regenerate tiles):

| CartoCSS property | Data source setter |
|---|---|
| `contour-base-interval` | `setBaseInterval` |
| `contour-resolution` | `setResolution` |
| `contour-min-visible-zoom` | `setMinVisibleZoom` |
| `contour-simplify-tolerance` | `setSimplifyTolerance` |

All of them are optional: a `#contour` block that only styles the lines (no `contour-*`
property) leaves the source on its own defaults. Ordinary styling rules in the block are
ignored when the config is read, filters included — a converted MapBox style writes
`[$type] = 'line'` on every line rule, and that filter reads a feature the config pass does
not have.

Contours render past the DEM's max zoom via the child layer's `MaxOverzoomLevel` (from the
source) — no need to download the DEM at the target zoom. Set
`contourSource.setMaxOverzoomLevel(n)`.

## 4. Property evaluation — zoom & interpolation

- **`[view::zoom]`** is the fractional map zoom → evaluated **per frame** (smooth). Use it for
  animated config.
- **`zoom`** is the integer tile zoom → steps per zoom level.
- **Interpolation** keyframes use **parentheses**, not brackets (brackets are field access):

  ```css
  hillshade-opacity: linear([view::zoom], (11, 0.2), (12, 1.0));   // correct
  hillshade-opacity: linear([view::zoom], [11, 0.2], [12, 1.0]);   // WRONG - silently ignored
  ```

  Methods: `linear`, `step`, `cubic`.

- **Illumination direction** is degrees and wraps correctly (any range mods via sin/cos, so
  `(11, 300), (12, 420)` sweeps 300°→60°). A wide sweep visibly swings the shadows as the light
  crosses due-south (180°) and due-north (360°) — that is physical, not a glitch. Pick a smaller
  arc for a gentle change.

- **Smooth vs stepped:** per-frame properties animate smoothly; decode-bound ones
  (`hillshade-height-scale`, `hillshade-contrast`, `hillshade-contour-interval`) update only at
  integer-zoom crossings. Prefer `hillshade-exaggeration` (per-frame) over `hillshade-height-scale`
  for animation.

## 5. Visibility (zoom & param::)

Gate a source's visibility with rule predicates on its `#name` block:

```css
#satellite['view::zoom'>=13] { raster-opacity: 0.4; }          // only z>=13 (also limits fetching)
#hillshade['view::zoom'>5]['view::zoom'<=15] { hillshade-opacity: 0.6; }
#hillshade['param::show_relief'=true] { hillshade-opacity: 0.6; } // toggled at runtime (see §6)
```

Raster/hillshade children have their fetch/draw zoom range constrained to the config rules'
zoom range, so they are not fetched outside the enabled zooms.

## 6. Style parameters — runtime visibility toggles

`param::` parameters let user settings drive layer visibility/appearance at runtime via
`decoder.setStyleParameter(name, value)`. **They must be declared in a bundle style's project
JSON** (`"styleparameters"`); a raw CartoCSS string cannot declare them (`loadMap` passes none).

Build a self-contained bundle in memory (no external file) with an in-memory zip:

```java
// project.json declares the style parameter, the layer order, and the mss file(s)
String projectJson =
    "{ \"styles\": [\"style.mss\"]," +
    "  \"layers\": [\"place\",\"contour\",\"building\",\"transportation\",\"hillshade\",\"landcover\",\"water\"]," + // top -> bottom
    "  \"styleparameters\": { \"show_relief\": { \"default\": true } } }";

String mss =
    "Map { background-color: #eef2f0; }\n" +
    "#water { polygon-fill: #9cc3e0; }\n" +
    "#landcover { polygon-fill: #dbe8cc; }\n" +
    "#hillshade['param::show_relief'=true]['view::zoom'>=4] {\n" +
    "  hillshade-opacity: linear([view::zoom], (4, 0.4), (12, 0.7));\n" +
    "  hillshade-exaggeration: linear([view::zoom], (4, 0.6), (12, 1.4));\n" +
    "}\n" +
    "#transportation { line-color: #ffffff; line-width: 1.2; }\n" +
    "#building['view::zoom'>=14] { polygon-fill: #d9cfc4; }\n" +
    "#contour['view::zoom'>=12] { line-color: #9a5a12; line-width: 0.8; }\n";

// Zip the two assets in memory -> ZippedAssetPackage -> CompiledStyleSet (loadMapProject)
java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream();
java.util.zip.ZipOutputStream zos = new java.util.zip.ZipOutputStream(bos);
for (String[] entry : new String[][] { {"project.json", projectJson}, {"style.mss", mss} }) {
    zos.putNextEntry(new java.util.zip.ZipEntry(entry[0]));
    zos.write(entry[1].getBytes("UTF-8"));
    zos.closeEntry();
}
zos.close();
CompiledStyleSet styleSet = new CompiledStyleSet(
    new ZippedAssetPackage(new com.massifmaps.core.BinaryData(bos.toByteArray())));
MBVectorTileDecoder decoder = new MBVectorTileDecoder(styleSet);

CompositeVectorTileLayer layer = new CompositeVectorTileLayer(baseVectorSource, decoder);
layer.addExternalDataSource("hillshade", demSource, CompositeSourceType.COMPOSITE_SOURCE_TYPE_HILLSHADE);
// ... contour, satellite ...
mapView.getLayers().add(layer);

// Toggle the relief on/off at runtime from a user setting:
decoder.setStyleParameter("show_relief", "false");  // hides #hillshade live, no re-add
```

Style parameters can be booleans (as above), enums, or numbers; declare enums with a `values`
map in the project JSON. `setStyleParameter` re-symbolizes and the composite re-evaluates the
config on the next frame.

## 7. Compiled Mapnik XML styles

A style shipped as compiled XML (`css2xml project.json style.xml`) keeps its slots: each config
block is written out as a symbolizer element and read back by the XML parser.

| CartoCSS block | XML element |
|---|---|
| `#name { raster-* }` | `<RasterConfigSymbolizer …/>` |
| `#name { hillshade-* }` | `<HillshadeConfigSymbolizer …/>` |
| `#name { contour-* }` | `<ContourConfigSymbolizer …/>` |

Attributes are the property names without the block prefix — `hillshade-shadow-color` becomes
`shadow-color`, `raster-opacity` becomes `opacity`. A block that sets nothing still compiles to
`visible="true"`, so the slot survives.

## 8. Gotchas & limitations

- **`layers` array is reversed** into draw order (first entry = top). Raw-CSS placement is
  first-reference = bottom.
- **Background:** the style `Map { background-color }` is drawn by the bottom group only.
- **Decode-bound props** (`height-scale`, `contrast`, `contour-interval`) step per zoom level;
  parameter-driven changes to them take effect on the next zoom change.
- **Fonts:** text symbolizers (labels) need a font asset in the style bundle; the raw-string
  demo omits text.
- **Performance:** the master style is decoded once per style-layer group (groups = external
  slots + 1). Fine for a few slots; wrap shared sources in a cache so network is fetched once.
  A single-pass renderer (one decode) is a documented future optimization.
- **`comp-op` for raster** is not yet applied.
