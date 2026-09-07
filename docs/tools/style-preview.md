---
title: Style preview
description: "Edit a CartoCSS or MapBox style in the browser and see it drawn by the SDK itself, compiled to WebAssembly."
sidebar_position: 2
---

# The style preview

**[Open the preview →](pathname:///MassifMaps/preview)**

The SDK compiled to WebAssembly, rendering a style you can edit. It is the same renderer Android
and iOS run and the same converter [`massif-style`](style-cli.md) runs, so what appears there is
what an app would draw — which is the point. A style that looks right in another tool and wrong in
an app is exactly what this catches.

Everything runs in your browser. No style, tile key or sprite sheet is uploaded anywhere.

## What it does

| | |
|---|---|
| **CartoCSS** | Edit it and press Apply. Four starter styles, from "the smallest thing that shows a map" to extruded buildings. |
| **MapBox style JSON** | Paste one and press Apply. It is translated by `mapbox2css` in the page — sprite sheet, icons, light presets and all — then rendered. |
| **Tiles** | Any TileJSON URL or `{z}/{x}/{y}` template. The default is [OpenFreeMap](https://openfreemap.org)'s planet, which needs no key. |
| **Camera** | Drag to pan, wheel to zoom, right-drag to rotate and tilt. **Copy link** puts the style and the camera in the URL. |

## Two things that make a style draw nothing

**The schema has to match the source.** A style names source layers, and the vocabularies differ:
MapBox Streets calls a road `#road`, OpenMapTiles calls it `#transportation`. The starters are
written for OpenMapTiles because that is what the default source serves; a MapBox Streets style
against it draws an empty map, and neither the style nor the source is wrong.

**A font the build has not got.** The web build carries one face. A style naming another — every
converted MapBox style names DIN Pro — keeps its labels only because the converted project is given
a fallback font; a hand-written CartoCSS naming a face that is not there loses those labels
silently. Name `Roboto` and they come back.

## What it is not

- **Not the whole SDK.** The web build is the `lite` profile: no routing, no geocoding, no offline
  packages, no persistent tile cache.
- **Not a device.** It is the same C++ and the same GL, but a desktop GPU is not a phone, and the
  defaults it runs with are a desktop's. Judge performance on a device.
- **Not an editor.** There is no autocomplete and no error underlining. A CartoCSS mistake shows up
  as a message from the compiler, or as nothing being drawn.

## Running it locally

The page loads a wasm build that is not tracked, so a checkout needs it built once:

```bash
source ~/emsdk/emsdk_env.sh
python3 scripts/build-web.py --profile lite --configuration RelWithDebInfo --build-demo --website
npm --prefix website start
```

Then <http://localhost:3000/MassifMaps/preview>. Without the build the page says what to run rather
than hanging. The whole toolchain, and what the browser needs from the host, is in
[the web build page](../maintenance/web-build.md).
