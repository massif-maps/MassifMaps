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
| **Terrain** | 3D terrain over [Mapterhorn](https://mapterhorn.com)'s global DEM, on by default and streamed straight from their tiles — no key. |
| **Fog** | The SDK's MapBox-modelled atmosphere: the haze the far ground fades into, which is what hides the horizon a tilted view ends at. Two sliders set where it starts and where it saturates, in multiples of the camera-to-focus distance. |
| **Shadows** | Sun shadows cast by the ground and by the buildings, at the hour the slider is on. |
| **Search** | Any place name, through [Photon](https://photon.komoot.io) — OSM data, no key. A result with an extent is fitted to it, a point is flown to. Only the query you type is sent. |
| **Light** | An hour slider drives the SDK's own solar model at the map centre, so shadows fall where they would there on 21 June. A converted MapBox style also carries dawn/day/dusk/night presets, and the page can follow them from the hour. |

## Two things that make a style draw nothing

**The schema has to match the source.** A style names source layers, and the vocabularies differ:
MapBox Streets calls a road `#road`, OpenMapTiles calls it `#transportation`. The starters are
written for OpenMapTiles because that is what the default source serves; a MapBox Streets style
against it draws an empty map, and neither the style nor the source is wrong.

**A font the build has not got.** The web build carries one face. A style naming another — every
converted MapBox style names DIN Pro — keeps its labels only because the converted project is given
a fallback font; a hand-written CartoCSS naming a face that is not there loses those labels
silently. Name `Roboto` and they come back.

## Judging shadows

The hour is UTC and the date is fixed at 21 June, so the sun runs its longest arc. It is placed for
the **map centre** — the sun over Reykjavik in June barely sets, and the slider shows that — which
also means the sun is re-placed when you search somewhere new, not when you pan by hand. Nudge the
slider after a long pan.

Shadows are cast over the **terrain** cover, so the **Terrain** button is what turns the shadow
pass on at all: with it off the hour still moves the shading, and nothing casts. With it on, the
casters are the ground itself and any extrusion a style declares — `building-height` in the
starters. Judge them at a low sun; at noon over a city there is barely anything to see.

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
