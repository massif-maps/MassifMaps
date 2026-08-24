---
title: Migrating from the CARTO Mobile SDK
description: Every namespace, package, class prefix and style token renamed when the CARTO Mobile SDK became Massif Maps — and the ones deliberately left alone.
sidebar_position: 8
slug: /migration
---

# Migrating from the CARTO Mobile SDK

Massif Maps **is** the CARTO Mobile SDK, forked and kept alive after CARTO stopped maintaining it.
The 4.x concepts, class shapes and CartoCSS styles all still apply — what changed in 6.0 is the
name, in every namespace, package and debug token.

Every rename below is a **hard break** except the CartoCSS ones, which keep parsing the old spelling
and log a deprecation warning once per stylesheet.

:::tip Most apps are a find-and-replace
An Android app that only used `com.carto.*` classes migrates with one search-and-replace and a
dependency line. The part with no automatic path is the CARTO **online services**, which the fork
removed entirely — see [What was removed](#what-was-removed).
:::

## Do this first

**1 · Change the dependency.**

```gradle
// before
implementation 'com.carto:carto-mobile-sdk:4.4.+'
// after — JitPack overrides the declared groupId, so the coordinate is the GitHub path
implementation 'com.github.massif-maps:MassifMaps-android-aar:v6.0.0'
```

On iOS the CocoaPod is replaced by a Swift package:
`https://github.com/massif-maps/MassifMaps-ios-swift`.

**2 · Rename the imports.**

```bash
# Android / Java / Kotlin
grep -rl 'com\.carto\.' src/ | xargs sed -i '' 's/com\.carto\./com.massifmaps./g'

# iOS / Obj-C / Swift — class prefix NT -> MSF
grep -rl 'NT[A-Z]' Sources/ | xargs sed -i '' -E 's/\bNT([A-Z][A-Za-z0-9]*)/MSF\1/g'
```

**3 · Replace anything that talked to CARTO's servers** — see
[What was removed](#what-was-removed).

**4 · Styles need no change.** `nuti::` still parses. Rename it when convenient.

## Bindings

| Platform | Before | After |
|---|---|---|
| Java / Kotlin | `com.carto.*` | `com.massifmaps.*` |
| Java (routing-lib) | `com.akylas.routing.*` | `com.massifmaps.routing.*` |
| Objective-C / Swift | `NTMapView`, `NTVectorTileLayer`, … | `MSFMapView`, `MSFVectorTileLayer`, … |
| .NET | `Carto.Ui`, `Carto.Layers`, … | `Massif.Ui`, `Massif.Layers`, … |
| Native library | `carto_mobile_sdk` (`libcarto_mobile_sdk.so`) | `massif` (`libmassif.so`) |
| Gradle artifact | `com.carto:carto-mobile-sdk` | `com.massifmaps:massif` |

On JitPack the resolvable coordinate is `com.github.massif-maps:MassifMaps-android-aar:<tag>` — JitPack
overrides the declared `groupId`, so the package name and the coordinate differ by design.

## C++ (only if you build from source or embed the core)

| Before | After |
|---|---|
| `carto::` | `massif::` |
| `_CARTO_*_SUPPORT` build defines | `_MASSIF_*_SUPPORT` |
| `CARTO_VT_RENDER_STATS`, `CARTO_FRAME_PROFILER` | `MASSIF_VT_RENDER_STATS`, `MASSIF_FRAME_PROFILER` |
| `mvt::NutiParameter*` | `mvt::StyleParameter*` |
| `CartoGeocodingProxy` | `MassifGeocodingProxy` |

## CartoCSS — the old spelling still works

Both spellings parse. `CartoCSSMapLoader` logs one warning per deprecated token per stylesheet; the
old spelling will be removed in a later release.

| Before | After | What it means |
|---|---|---|
| `[nuti::x]` | `[param::x]` | app-supplied runtime parameter, joining `mapnik::` and `view::` |
| `"nutiparameters"` (project.json) | `"styleparameters"` | the block declaring those parameters |
| `text-placement: nutibillboard` | `billboard` | fully screen-aligned |
| `text-placement: nutibillboardline` | `billboard-line-repeat` | screen-aligned, repeated along a line |
| `text-placement: nutipoint` | `flat` | flat in the placement plane, no camera facing |
| `text-placement: nuticallout` | `callout` | screen-aligned with a leader line |

`point` (upright on the ground normal, swivelling to face the camera) is unchanged.
See [Live style parameters](/docs/features/style-parameters) for what `param::` can do now.

### `text-placement: line` lies flat again (behaviour change in 6.0.1)

6.0.0 laid every `line` label out on the camera axes, so the text stayed upright at any tilt instead
of lying on the map — the 5.x behaviour, and what the same text drawn with `text-clip` still did.
`line` is flat again, and the upright run moved to `billboard-line`, which now follows the line
instead of placing one upright label per `text-spacing` step.

| You want | Use |
|---|---|
| the 5.x look, text in the ground plane (route distances, contour heights) | `text-placement: line` — no change needed |
| the 6.0.0 look, text upright and readable at any tilt | `text-placement: billboard-line` |
| one upright label per `text-spacing` step, not turning with the line (road shields) | `text-placement: billboard-line-repeat` |

See [Labels](internals/rendering/06-labels.mdx) for the three layouts.

### `shield-placement` defaults to `billboard-line-repeat` (breaking change after 6.0.1)

`billboard-line-repeat` is new: it repeats along the line at `shield-spacing` like `line` does, but
lays no glyph run on it, so each repeat stays an upright camera-facing box. That is what a road
shield is, and until now no placement gave both — `billboard-line` turns the shield with the road,
`billboard` draws one per line. It is now the **default** for shields, so a style that never set
`shield-placement` changes twice:

| Feature | Before (`point`) | Now |
|---|---|---|
| line | one shield, flat on the ground at the line's midpoint | one per `shield-spacing` step, facing the camera |
| point / polygon | flat on the ground, foreshortening with the tilt | facing the camera |

`shield-placement: point` restores the old default exactly. `text-placement` is unchanged — text
still defaults to `point`.

### `text-clip` / `shield-clip` no longer follow `allow-overlap` (behaviour change in 6.0.1)

`clip` used to default to whatever `allow-overlap` was, so `text-allow-overlap: true` alone moved the
text off the label pipeline onto the tile geometry: no culler, no `text-min-distance`, no run
following the line, one copy at the midpoint of **every** segment, and glyphs cut at the tile border.
Both now default to `false`, which is their own declared default.

- A style that wanted overlapping **labels** needs no change and gets correctly placed ones.
- A style that relied on the clipped geometry path (usually for its cost, on very dense text) must
  now say `text-clip: true` explicitly.
- `marker-clip` is unchanged — `marker-allow-overlap: true` still takes the geometry path.

## Debug knobs

| Before | After |
|---|---|
| logcat tag `carto-mobile-sdk` | `massif` |
| `adb shell setprop debug.carto.*` | `debug.massif.*` |
| demo app `com.akylas.cartotest` | `com.massifmaps.MassifDemo` |
| demo `--es style nuti` | `--es style project` (`--es demo nuti` still accepted) |
| demo `--es nutiInterval` | `--es paramInterval` |

## What was removed {#what-was-removed}

The fork dropped everything that depended on CARTO's hosted services — they were switched off with
the product. There is **no renamed equivalent**; bring your own source.

| Gone | Replace with |
|---|---|
| `CartoOnlineVectorTileLayer` and the hosted basemap | any `TileDataSource` + a CartoCSS style — see [Layers & data sources](/docs/guides/layers-and-data-sources) |
| CARTO offline map packages and their `PackageManager` endpoints | your own package server, [MBTiles](/docs/guides/offline-maps) or [PMTiles](/docs/features/pmtiles) |
| CARTO hosted routing and geocoding endpoints | the embedded Valhalla / SGRE engines, or any HTTP service |
| CARTO API keys and the mobile app registration flow | nothing — there is no key to set |

Guides carried over from the CARTO documentation that still use these classes carry a warning
banner at the top.

## Breaking changes after 6.0.0

### Map camera events carry a reason, and onMapStable fires once per movement

`MapEventListener.onMapMoved` and `onMapStable` take a `MapMoveReason` argument, so every app that
overrides them must change the signature. The C++ compiles either way — a missed override just
stops being called — so this is a **silent** break unless you build with an override check.

```java
// before
public void onMapMoved() { ... }
public void onMapStable() { ... }

// after
public void onMapMoved(int reason) { ... }
public void onMapStable(int reason) {
    if (reason == MapMoveReason.MAP_MOVE_REASON_GESTURE) { ... }
}
```

```objc
// before
- (void)onMapMoved;
// after
- (void)onMapMoved:(enum MSFMapMoveReason)reason;
```

The reason is `MAP_MOVE_REASON_GESTURE`, `_ANIMATION` or `_API`. On the facade, `map.moved` and
`map.stable` now carry a `MapMoveInfo` payload with a `reason` property, where they carried none.

**`onMapStable` also changes behaviour**: it is edge-triggered. It fires once, at the end of a
movement, and a touch that did not move the camera does not raise it at all. It used to be a poll —
it fired for a tap on a motionless map, and could fire repeatedly while at rest. An app that kept
its own "did the map actually move" flag can drop it, and one that relied on a tap raising stable
must listen for the click instead.

See [Map events](internals/map-events.md) for the full model.

### 3D buildings cast a contact shadow by default

`building-ao-ground-radius` used to default to `0`, i.e. off, so nothing changed until a style
asked. It now defaults to **4 metres** with `building-ao-intensity` **0.2**, and an app that
upgrades gets the shadow without changing anything.

An extrusion with no contact shadow reads as pasted onto the map rather than standing on it, which
is why it is on. It is not free — around 3 ms of GPU frame at a city camera where there is no
drape to bake it into, and nothing where there is (see
[the performance log](internals/performance-log.md)).

`building-ao-ground-attenuation` also **changes meaning**: it is now the exponent `k` of
`occlusion = (1 - d)^k`, not mapbox's `1 - pow(1 - d, k)`. Higher keeps the shadow tighter to the
wall; the old formula read far too strong across the whole band.

| | Before | After |
|---|---|---|
| `building-ao-ground-radius` | `0` (off) | `4` |
| `building-ao-intensity` | `0.5` | `0.2` |
| `building-ao-ground-attenuation` | `0.69`, mapbox's curve | `1.75`, exponent of `(1 - d)` |

To keep the old look, set `building-ao-ground-radius: 0;` in the style's `Map` block.


### OpenGL ES 3.0 is required

The SDK no longer creates or accepts an OpenGL ES 2.0 context. No API changed — this is a **device**
break, not a code one.

| | Before | After |
|---|---|---|
| Android manifest | `uses-feature glEsVersion 0x00020000` | **`0x00030000`** |
| Android context | ES 3.0 if the device reported it, else ES 2.0 | ES 3.0, no fallback |
| iOS context | ES 3.0, falling back to ES 2.0 | ES 3.0, no fallback |
| UWP | `EGL_CONTEXT_CLIENT_VERSION 2` | `3` |

**If your app ships its own `AndroidManifest.xml` with a `glEsVersion` line, raise it to
`0x00030000`** — the merged manifest takes the highest value, so a stale `0x00020000` in your app is
harmless, but leaving it there advertises support you no longer have.

Devices lost are pre-2013 GPUs: Mali-400, Adreno 200/305, Tegra 3, PowerVR SGX. At `minSdk 21` and
an iOS 13 floor — where every device is A7 or newer — that is a rounding error. On desktop the
equivalent floor is D3D feature level 10_1 (Sandy Bridge, 2011), which Windows 11 already exceeds.

### Shaders moved to GLSL ES 3.00 — and your shaders still work

The SDK's shaders now compile as `#version 300 es`. **This is not a breaking change for
application GLSL.** If you pass a shader to `SkyOptions.shaderSource`, `FogOptions.shaderSource`,
`TerrainOptions.surfaceShaderSource`, `CustomRasterTileLayer.shaderSource` or `PostProcessEffect`,
keep writing it exactly as before — `attribute`, `varying`, `texture2D` and `gl_FragColor` all still
work.

They keep working because the SDK prepends tangram's compatibility preamble, which maps the ESSL
1.00 spellings onto their 3.00 equivalents, including `#define gl_FragColor`. That is legal: ESSL
reserves the `GL_` prefix for *macro* names, and `gl_FragColor` is a built-in *variable* that ESSL
3.00 does not declare.

You may now also use ESSL 3.00 features directly (`in`/`out`, `texture()`, integer operations) — but
do not declare your own `#version` line, and do not declare a fragment output named
`TANGRAM_FragColor`, which the preamble already provides at location 0.

### `LightOptions.shadowDistance` is a factor, not metres

| Before | After |
|---|---|
| `lightOptions.shadowDistance = 20000` (metres) | `lightOptions.shadowDistance = 4.5` (**multiples of the camera-to-focus distance**) |
| style `shadow-distance` in metres | style `shadow-distance` in the same multiples |

Same unit as `fogOptions.rangeStart` / `rangeEnd`, and the same reason: the camera-to-focus distance
is a function of the zoom alone, so one value holds from a city to a massif where a metric radius has
to be retuned per zoom. The name did not change, so **an app that set metres will not fail to
compile** — it will ask for thousands of times the view. Drop the setting and let the default (`0`,
i.e. `4.5`) apply, or scale from there; do not convert.

### Fog moved to its own `FogOptions`

Fog was three fields on `TerrainOptions` and two on `SkyOptions`. It is not a terrain feature — it
fogs a plain 2D map too — so it is now one object on `Options`, modelled on the Mapbox `fog` style
property. See [Sky, Sun & Shadows](/docs/features/sky-sun-shadows).

| Before | After |
|---|---|
| `terrainOptions.fogColor` | `fogOptions.color` |
| `terrainOptions.fogStartDistance` (metres) | `fogOptions.rangeStart` (**multiples of the camera-to-focus distance**) |
| `terrainOptions.fogDistance` (metres) | `fogOptions.rangeEnd` (same unit) |
| `skyOptions.fogBlend` (degrees) | `fogOptions.horizonBlend` (fraction of a quarter turn: `degrees / 90`) |
| `skyOptions.fogHorizon` | `fogOptions.horizonAngle` (unchanged, degrees) |
| style `fog-start-distance` / `fog-distance` | style `fog-range-start` / `fog-range-end` |

**The range unit changed, so the numbers do not carry over.** Metres had to be retuned for every
zoom; the camera-to-focus distance is a function of the zoom alone, so one setting now holds
everywhere. Start from the defaults (`0.8` to `8`) rather than converting.

New with it: `enabled` (a real switch — no more toggling fog by driving a distance to `0`),
`highColor`, `spaceColor`, `starIntensity` (Mapbox `high-color` / `space-color` / `star-intensity`),
and `shaderSource` for a custom fog blend across map and sky.

### 3D buildings are lit by the sun, whatever the terrain is doing

Tile extrusions had two lighting models, picked by whether terrain lighting was on. With it off,
walls used `Options.MainLightDirection` / `MainLightColor` / `AmbientLightColor` and **roofs were
shaded by the view direction** — so buildings changed shape when terrain lighting was toggled, and
never answered to the hour of day. There is now one model, the same normalised Lambert the terrain
surface uses, and it is always on.

| Before | After |
|---|---|
| `options.mainLightDirection` (3D buildings) | `lightOptions.sunAzimuth` / `sunAltitude` |
| `options.mainLightColor` (3D buildings) | `lightOptions.sunColor` |
| `options.ambientLightColor` (3D buildings) | style `building-ambient` (see below) |

**The look changes on upgrade even if you set nothing.** The old default light pointed down and
slightly north-east (`0.35, 0.35, -0.87`); the sun defaults to azimuth 315 / altitude 45. Buildings
are also tinted by `sunColor` now, which is what makes a facade go warm at dusk with the slope
behind it.

The three `Options` properties still exist and still drive the normal-map illumination default and
the spherical-mode 2D lighting; they simply no longer reach tile extrusions. `Polygon3DLayer`
elements are unaffected — they have their own renderer and their own lighting.

Styles keep both overrides: `building-light-intensity` and `building-ambient` in the `Map` block
still win over the sun, so a style can tune walls without moving the terrain sun.

Note the asymmetry: buildings take the sun's **direction, colour and intensity**, but keep their
**own ambient**. `lightOptions.ambientIntensity` moves the ground, not the facades — at ambient 1 a
shared value would flatten every building, and an extrusion with no side shading does not read as 3D.

### 3D buildings follow mapbox's `fill-extrusion` model

The normalised Lambert above was replaced by mapbox's model, so a facade shades the way the same
building does in mapbox at the same hour. Details in
[`internals/rendering/08-lighting-sky-fog.md`](internals/rendering/08-lighting-sky-fog.md#buildings).

**The look changes on upgrade even if you set nothing**, in three ways:

| | Before | After |
|---|---|---|
| light sum | `ambient + sun * (1 - ambient)`, in sRGB | `ambient + sun`, summed **linear**, returned to sRGB |
| `building-ambient` default | 0.35 | **0.5** |
| `building-light-intensity` default | 1.0 | **0.5** |
| `building-vertical-gradient` default | 0.65 | **0** — mapbox has no facade gradient |

The two 0.5s sum to exactly 1 in direct sun, so a lit facade keeps its own colour at any hour. To
keep something close to the old look, set `building-vertical-gradient: 0.65` and
`building-light-intensity: 1` in the style's `Map` block; the linear sum has no opt-out.

Roofs now read **lighter than walls in daylight and darker at dawn**, as mapbox does — it falls out
of `N.L` and is not tunable. A 60° floor on the building sun's altitude, present only in unreleased
6.1 builds, was removed: it was compensating for a shadow bug and it suppressed that inversion.

Buildings also gain a rounded roof edge (`building-edge-radius`, metres, 0 = off as before), a
`building-roof-shade` knob, and pitched roofs from OSM `roof:shape` / `roof:height` where the tiles
carry them. All default to the previous flat-capped, sharp-edged geometry.

### Java enums are int constants (Android only)

The 31 generated Java `enum` classes are now **classes of `int` constants**, annotated with
`@IntDef` — the shape Android's own APIs use. iOS and .NET are unchanged: Objective-C already
emitted `typedef NS_ENUM`, and C# enums cost nothing to wrap.

```java
// before                                    // after
public enum PanningMode {                    public final class PanningMode {
  PANNING_MODE_FREE,                           public final static int PANNING_MODE_FREE = 0;
  PANNING_MODE_STICKY,                         public final static int PANNING_MODE_STICKY = 1;
  ...                                          ...
}                                              @IntDef({ ... }) public @interface Value {}
                                             }
```

Signatures carry the annotation, so Android Studio still autocompletes only the valid constants and
lint flags a wrong `int`:

```java
public void setPanningMode(@PanningMode.Value int panningMode);
```

**Most call sites do not change.** The class name and the constant names are identical, so
`options.setPanningMode(PanningMode.PANNING_MODE_STICKY)` compiles as before. What breaks:

| | Before | After |
|---|---|---|
| holding one | `PanningMode m = options.getPanningMode();` | `int m = options.getPanningMode();` |
| parsing a name | `PanningMode.valueOf(name)` | your own `switch` on the string |
| numeric value | `m.swigValue()` | `m` **is** the value |
| from a number | `PanningMode.swigToEnum(i)` | `i` **is** the constant |
| iterating | `PanningMode.values()` | no equivalent — list the constants you need |
| `switch` | `case PANNING_MODE_FREE:` | unchanged |

`switch` keeps working because the constants are now compile-time literals rather than a JNI call
per constant at class load — which is also what lets them be `@IntDef` members.

The Android artifact gains one dependency, `androidx.annotation`, for the annotation itself.

### The facade sugar has its own `Position`, `Bounds`, `ScreenPoint`, `ScreenRect`

Facade sugar only — `MapPos`, `MapBounds`, `ScreenPos` and `ScreenBounds` are untouched and stay
with the object API.

| Was | Now |
|---|---|
| `com.massifmaps.core.MapPos` | `com.massifmaps.api.Position` (`lng`, `lat`, `alt`) |
| `com.massifmaps.core.MapBounds` | `com.massifmaps.api.Bounds` (`min`, `max`) |
| `com.massifmaps.core.ScreenPos` | `com.massifmaps.api.ScreenPoint` (`x`, `y`) |
| `com.massifmaps.core.ScreenBounds` | `com.massifmaps.api.ScreenRect` (`min`, `max`) |
| `MSFMapPos` / `MSFMapBounds` / `MSFScreenPos` / `MSFScreenBounds` | `MSFPosition` / `MSFBounds` / `MSFScreenPoint` / `MSFScreenRect` |

```java
map.camera().moveTo(new MapPos(5.7245, 45.1885), 13.5f);        // was
map.camera().moveTo(new Position(5.7245, 45.1885), 13.5f);      // now
```

Fields, not getters: `pos.lng` rather than `pos.getX()`. `MSFPosition` uses properties
(`pos.lng`). Longitude is still FIRST, as `MapPos.getX()` was.

Also changed, on the camera:

- `MapCamera.fitBounds(bounds)` is now `fitBounds(bounds, width, height)`; the ObjC
  `fitBounds:screenBounds:integerZoom:` is `fitBounds:screenRect:integerZoom:`. The camera no
  longer holds the view, so it cannot measure it for you. An overload taking `resetRotation` and
  `resetTilt` is new, as is `camera().climb(height)` for an arched flight.
- `MassifMap.screenToMap` returns a `Position` and `mapToScreen` a `ScreenPoint`.
- New: `MapCamera.progress()` / `MSFMapCamera.progress`, 0 to 1 through the current flight.

Why: a `MapPos` is a SWIG proxy over a C++ object, so every position a click handler reads costs
a JNI allocation and a finalizer to carry two doubles. These are plain objects, and they are what
lets the sugar name no SWIG type at all.

### A facade position is WGS84 by default

Facade API only — the object API is untouched, and `MapPos` still means whatever its owner says.

A position read through the facade with **no projection named** used to come back in the object's
own projection (EPSG:3857 in practice). It now comes back in **EPSG:4326** — degrees:

```java
MassifApi.getPos(payload, "featurePos");                // was [641267, 5660048], now [5.7606, 45.2442]
MassifApi.getPos(payload, "featurePos", "EPSG:3857");   // the old behaviour, asked for by name
```

This is the change most likely to pass a compiler and fail at runtime, so check every facade
`getPos` / `mm_get_string` / `mm_get_position` call that does **not** name a projection. A
per-read name still wins, and a per-subscription default still applies to the reads inside its
handler; only "nobody said" changed.

Writes moved with it — `setString` on a position property (`Options.panBounds`,
`GeocodingRequest.location`) is now taken as WGS84 too, so a value read and written back is
unchanged. That is the whole reason both sides moved at once.

Unchanged: a Mercator conversion that produces a non-finite number (the poles) still fails with
`RESULT_UNSUPPORTED_TYPE` rather than emitting JSON that will not parse, and an object with no
known projection is still left alone rather than guessed at.

### `adopt` and the event bridges moved to `MassifInterop`

Facade API only, and a straight rename — same arguments, same behaviour:

| Was | Now |
|---|---|
| `MassifApi.adopt(kind, id, …)` | `MassifInterop.adopt(kind, id, …)` |
| `MassifApi.getSource` / `getLayer` | `MassifInterop.getSource` / `getLayer` |
| `MassifApi.createEventBridge` | `MassifInterop.createEventBridge` |
| `MassifApi.createVectorTileEventBridge` | `MassifInterop.createVectorTileEventBridge` |
| `MassifApi.createVectorElementEventBridge` | `MassifInterop.createVectorElementEventBridge` |

On iOS the class is `MSFMassifInterop`; `MassifMaps.h` already imports it.

Those are the only facade methods that name an SDK class in their signature, so they are the only
ones a hand-written binding cannot carry. Splitting them out is what makes "`MassifApi` is
SWIG-free" a checkable property rather than an intention — see
[api-facade.md](internals/api-facade.md). The typed sugar (`Massif`, `MassifMap`, `MassifLayer`,
`MSFMassif…`) is unaffected; it already calls this for you.

### `TileDataSource.encoding` became a general meta data map

`setEncoding` / `getEncoding` were a DEM-only setting on the base class of **every** data source,
and the name already collided: `getMetaData("encoding")` meant the *tile format* (`mvt` / `mlt`) on
the same object. They are replaced by the meta data bag `VectorElement` and `Layer` already use, and
the DEM key is renamed:

| Was | Now |
|---|---|
| `source.setEncoding("terrarium")` | `source.setMetaDataElement("dem_encoding", Variant("terrarium"))` |
| `source.getEncoding()` | `source.getMetaDataElement("dem_encoding")` (a `Variant`) |
| `source.getMetaData(key)` → `String` | `source.getContainerMetaData(key)` → `String` |
| — | `source.getMetaData()` / `setMetaData(map)` — the whole map, `String` → `Variant` |
| `MBTilesTileDataSource.getMetaData()` | `getContainerMetaData()` |

`getMetaDataElement` falls back to `getContainerMetaData`, so a tileset that declares `dem_encoding`
in its own MBTiles/PMTiles metadata needs no application code at all.

**The decoder is now resolved per TILE**, from the map that tile carries. Two DEM sources of
different encodings can therefore sit behind one `OrderedTileDataSource` — see
[3D terrain](/docs/features/3d-terrain). Two consequences worth knowing:

- `ContourTileDataSource`'s default changed from Terrarium to **MapBox**, which is what the terrain,
  the hillshade and the composite layer already defaulted to. A Terrarium source that relied on the
  contour default must now declare `dem_encoding`.
- The `PersistentCacheTileDataSource` schema gained a `metaData` column. An existing cache database
  is dropped and rebuilt on first open, as with any past schema change.

`TileData::getMetadata` / `setMetadata` were renamed to `getMetaDataElement` / `setMetaDataElement`
too; they were never exposed to bindings, so only C++ embedders see it.

## Deliberately NOT renamed

These name data or upstream work, not this SDK:

- **On-disk formats.** `NUTi` is the 4-byte magic of the compressed bitmap format; `nutikeysha1` is
  a row in the `metadata` table of downloaded offline packages; `.nutigraph` / `.nutigeodb` are
  package file extensions; `__Nuti_pkgmgr_` is a local filename prefix. Renaming any of them would
  orphan data users already have on disk.
- **`cartodb_id`**, an MVT feature field.
- **CartoCSS** — the style language, MapBox's and CARTO's, which this SDK implements rather than
  owns.
- **The CartoDB copyright headers and LICENSE attribution.**

## Default option values (2026-08-23)

The SDK now ships the values every bench and every example screenshot in this repo was actually made
with, instead of values nothing was tuned at. They live in
`scripts/android-dev/.../demo/DemoConfig.java`, which is where the tuning was done.

An app that already sets one of these explicitly is unaffected. **A map that took the defaults will
look and perform differently** — mostly better, but the terrain ones change framing enough to
invalidate a stored camera.

| Option | Was | Now | Why |
|---|---|---|---|
| `Options.zoomGestures` | off | **on** | double-tap, two-finger tap and double-tap-drag zoom. Every other map SDK does these; an app that had to ask just looked broken |
| `Options.tileThreadPoolSize` | 1 | **2** | tangram's `numTileWorkers`. One made tiles arrive late enough to be seen arriving |
| `Options.tileLODFactor` | 1.0 | **0.5** | half a nominal tile of screen area per level |
| `TerrainOptions.meshResolution` | 32 | **64** | tangram's value. 32 leaves draped content visibly floating; 128 cost 8.5 fps against 15.2 |
| `TerrainOptions.cameraClearance` | 200 m | **60 m** | 200 stops the camera short of the surface, so a close approach swings into the nearest hillside |
| `TerrainOptions.billboardOcclusionTolerance` | 0.02 | **0** | a label goes out when its anchor goes behind the relief |
| `LightOptions.ambientIntensity` | 0.35 | **1.0** | both of these only apply once terrain lighting is on |
| `LightOptions.shadowStrength` | 0 | **0.3** | ↑ |
| `LightOptions.shadowBias` | 0.25 | **1.0** | 0.25 leaves acne on a lit slope at 3 cascades |
| `HillshadeRasterTileLayer.heightScale` | 1.0 | **0.05** | at 1.0 real DEM relief saturates and the shading reads as a stencil |
| `HillshadeRasterTileLayer.hillshadeMethod` | `STANDARD` | **`IGOR`** | keeps slopes readable under imagery |
| `HillshadeRasterTileLayer.illuminationMapRotationEnabled` | true | **false** | turning the map should not relight the terrain |
| `ContourTileDataSource.seamlessEdges` | off | **on** | without it a traced line stops dead at every tile border |
| `ContourTileDataSource.minVisibleZoom` | 12 | **5** | the interval ladder already coarsens a regional view |
| `ContourTileDataSource.simplifyTolerance` | 1.0 | **1.5** | |

**Not moved, and why.** The demo's sun position (azimuth 355°, altitude 9°) and hillshade
illumination azimuth are a raking *test* light for judging relief, not a default any app wants; the
same goes for its camera, its tile URLs and its per-source cache sizes.

`TerrainOptions.maxTileZoomCoarsening` was raised to the demo's 8 and **put back to 3**. It only
pays for itself next to the demo's fixed 170 km `viewDistance`, where what it coarsens is the far
horizon. On the default view distance it coarsens tiles that are still large on screen: measured on
the iOS terrain example, a blurred band with a hard tile edge down the middle of the view. The two
are a pair — raise both or neither.
