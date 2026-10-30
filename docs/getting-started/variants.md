---
title: Variants
description: "What each Massif Maps variant contains on Android, iOS, the web and NativeScript, how to pick one, and how to select it."
sidebar_position: 1.6
---

# Variants

The SDK is built in several variants so an app ships only what it uses. The variants differ in which
optional modules are compiled in; the renderer, styles, sources, layers, terrain and the
[surface API](/docs/api) are the same in all of them. Every variant of a release has the same version.

## What each one contains

| Module | `full` | `core` | `lite` |
|---|:-:|:-:|:-:|
| Rendering: vector, raster, hillshade, terrain, elements, styles, online sources | ✓ | ✓ | ✓ |
| SQLite and the offline tile sources (MBTiles, PMTiles, persistent cache, multi source) | ✓ | ✓ | – |
| Search (vector tile, vector element, feature collection) | ✓ | ✓ | – |
| Editable vector layer and its edit events | ✓ | ✓ | – |
| Offline packages (`PackageManager`) | ✓ | – | – |
| Geocoding (online services, offline OSM) | ✓ | – | – |
| Routing requests and results, and the online routing services | ✓ | – | – |
| Offline routing (Valhalla) | ✓ | – | – |
| WKT and WKB readers and writers | ✓ | – | – |

`core` is the `standard` build profile. `lite` has no SQLite at all, which is most of the difference in
size: measured on the arm64 `libmassif.so`, `core` was about 30% smaller than `full` and `lite` about 38%
([build and size](/docs/internals/build-and-size)). Those numbers predate the object API split below, which
takes about 15% more off the Android `libmassif.so`.

Routing can also be shipped on its own, with no map: [Packages](packages.md#routing-without-the-map).

## The object API

The SDK has two public surfaces: the **surface API** (specs, handles and property paths: `MassifMap`,
`Massif`, `MassifApi`) and the older **object API** (`VectorTileLayer`, `HTTPTileDataSource`,
`MapPos`, ... one proxy class per C++ class).

| Platform | Default variants | Object API |
|---|---|---|
| Android | `full`, `core`, `lite` | **not included**: the JNI wrappers for it are 1.3 MB of the library |
| Android | `objectapi` | included, on top of `core` |
| iOS | `full`, `core`, `lite` | included in every build |
| Web | `standard`, `full` | not applicable: the web SDK is the surface API |

An Android app that has not moved to the surface API yet depends on `objectapi`; one that has uses `full`,
`core` or `lite`. What moved where is in the [migration guide](/docs/migration#the-android-sdk-ships-without-the-object-api-objectapi-is-a-variant).
`objectapi` is a `core` build: it has no routing, geocoding or offline packages.

## Android

The artifact is `com.github.massif-maps:MassifMaps-android-aar`. `full` is the one with no classifier;
the others are classifiers of the same artifact:

```groovy
dependencies {
    // full: the default
    implementation 'com.github.massif-maps:MassifMaps-android-aar:6.0.2'

    // one of the others
    implementation 'com.github.massif-maps:MassifMaps-android-aar:6.0.2:core@aar'
    implementation 'com.github.massif-maps:MassifMaps-android-aar:6.0.2:lite@aar'
    implementation 'com.github.massif-maps:MassifMaps-android-aar:6.0.2:objectapi@aar'
}
```

Take exactly one of them. The same files are on the GitHub release as
`massif-android-<version>-<variant>.aar`, with `<variant>` one of `full`, `core`, `lite`, `objectapi`.
The routing library is one more classifier, `routing`, and goes beside any of them.

## iOS

The Swift package has one product per variant:

| Product | Variant |
|---|---|
| `MassifMaps` | `full` |
| `MassifMapsCore` | `core` |
| `MassifMapsLite` | `lite` |

Add one product to a target, never two: they define the same classes. There is no `objectapi` product, since
every iOS build includes the object API. The xcframeworks are on the release as
`massif-ios-<version>-<variant>.zip`.

## Web

`@massif-maps/web` ships two modules and the page picks one:

| Module | Variant | `.wasm` (brotli) |
|---|---|---|
| `massif-web` | `standard` (the default) | 1.85 MB |
| `massif-web-full` | `full` | 2.34 MB |

```ts
const map = createMap(canvas, { variant: 'full' });
```

A page downloads one module. The details, and how to serve them, are in [the web guide](web.md).

## NativeScript

[`@nativescript-community/ui-massifmaps`](https://www.npmjs.com/package/@nativescript-community/ui-massifmaps)
wraps the published native builds. On Android it takes the variant from two Gradle properties, set in
`App_Resources/Android/before-plugins.gradle`:

```groovy
ext {
    massifSDKVersion = "6.0.2"
    massifSDKVariant = "core"   // full (the default), core, lite or objectapi
}
```

The plugin's `/api` module and the map view run on the surface API, so they work with every variant. The
plugin's older wrappers (`layers`, `datasources`, `vectorelements`, ...) need the object API: use
`objectapi` on Android for an app that still imports them. With the other variants they fail with a message
that says so.

A local build of the SDK replaces the download: `massifAar` (the file name without `.aar`) and `massifAarDir`
(its folder) in the same `ext` block.

## Choosing one

- **Starting a new app:** `full` while you find out what you need, then `core`, or `lite` when you use no
  offline sources, search or editing. What the variants share behaves the same in all of them.
- **An Android app that uses the object API:** `objectapi`, and plan the move to the surface API.
- **Offline routing, geocoding or offline packages:** `full`. They exist nowhere else.
- **Size matters and the map is online:** `lite`.
- **A web page:** `standard`, unless you need what only `full` adds there: routing, geocoding, offline packages.

A module a variant lacks is not in it at all: its classes do not exist on Android and iOS, and a spec that names
one is refused.
