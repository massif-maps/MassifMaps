---
title: Packages
sidebar_position: 1.5
---

# Packages

Everything Massif Maps publishes, and where to get it. The SDK packages share one version, the
`v<version>` of the [GitHub release](https://github.com/massif-maps/MassifMaps/releases); the styles
and the NativeScript plugin have their own.

## Map SDK

| Platform | Package | Get it |
|---|---|---|
| Android | `com.github.massif-maps:MassifMaps-android-aar` — variants `full` (default), `core`, `lite` | JitPack, or the `.aar` on the release — [install](installation.md#android) |
| iOS | `massif-maps/MassifMaps-ios-swift` — products `MassifMaps` (full), `MassifMapsCore`, `MassifMapsLite` | Swift Package Manager, or the xcframework zip on the release — [install](installation.md#ios) |
| Web | `@massif-maps/web` | npm, or `MassifMaps-web-<version>.zip` on the release — [web guide](web.md) |
| NativeScript | `@nativescript-community/ui-massifmaps` | npm — [install](installation.md#javascript-through-an-integration) |
| Xamarin, UWP | — | build from source ([BUILDING.md](https://github.com/massif-maps/MassifMaps/blob/master/BUILDING.md)) |

`core` is the `standard` build profile; `lite` drops geocoding, routing and offline packages and is
about 40% smaller.

## Routing without the map

| Platform | Package | Get it |
|---|---|---|
| Android | `com.github.massif-maps:MassifMaps-android-aar:<version>:routing@aar` | JitPack, or the `.aar` on the release |
| iOS | `ValhallaRouting`, a product of the `MassifMaps-ios-swift` package | Swift Package Manager |

Its own native library (`libvalhalla_routing.so`), so it installs beside any map variant. See the
[routing guide](/docs/guides/routing).

## JavaScript and TypeScript

| Package | What it is |
|---|---|
| [`@massif-maps/web`](https://www.npmjs.com/package/@massif-maps/web) | the SDK compiled to WebAssembly, with the typed API |
| [`@massif-maps/api`](https://www.npmjs.com/package/@massif-maps/api) | the typed surface API on its own, shared by the web and NativeScript bindings |
| [`@massif-maps/style-tools`](https://www.npmjs.com/package/@massif-maps/style-tools) | `massif-style`: CartoCSS compilation, MapBox style conversion, legends — [style CLI](/docs/tools/style-cli) |
| [`@massif-maps/styles`](https://www.npmjs.com/package/@massif-maps/styles) | the Massif styles for MapLibre and the SDK, own version — [Massif styles](/docs/styles/massif) |

## Release assets

Also attached to each `v<version>` GitHub release:

- `css2xml_<os>.zip` — the CartoCSS compiler as a native binary, Linux and macOS;
- `massif-api-bindings.zip`, `massif-api.json`, `massif.d.ts` — the facade API schema and its
  generated bindings, for an integration that generates its own;
- `massif-style.wasm`, `massif-style.mjs` — the style compiler that `@massif-maps/style-tools` loads.

## Pre-releases

A release candidate (`6.1.0-rc.1`) is a GitHub *prerelease*, and its npm packages carry the `next`
dist-tag: `npm install @massif-maps/web@next`. JitPack and Swift Package Manager take the version
explicitly. A plain install never picks a pre-release.
