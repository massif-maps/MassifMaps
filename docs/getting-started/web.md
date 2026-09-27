---
title: The Web
sidebar_position: 3
description: "Run Massif Maps in a browser: the package, the headers a host must send, and what differs from Android and iOS."
---

# Massif Maps on the web

The web build is the same C++ SDK compiled to WebAssembly and drawn with WebGL 2, not a second
renderer. It is driven through the **same typed API as the NativeScript plugin** (`MassifMap`,
`addLayer`, `camera()`, the option groups and the events), so the JavaScript tab of every
[example](/examples) is the code of the map running live beside it.

:::info Preview
The web platform is new. It renders raster and vector tiles, CartoCSS and MapBox-converted styles,
3D terrain, sky, labels and the [celestial layers](/docs/features/celestial-objects). What differs
from the mobile platforms is listed [below](#what-differs-from-android-and-ios).
:::

## Get it

Every release ships the same files two ways:

| Channel | What |
|---|---|
| npm | `npm install @massif-maps/web` - depends on [`@massif-maps/api`](#the-api), the typed API |
| GitHub | `MassifMaps-web-<version>.zip` on the [Releases page](https://github.com/massif-maps/MassifMaps/releases) |

| File | Size (gzip) | What it is |
|---|---|---|
| `massif-web.wasm` | 2.2 MB | the SDK (the `standard` profile) |
| `massif-web.mjs` | 46 KB | its emscripten loader, which also starts its worker threads |
| `massif-web.data` | 255 KB | the fonts the build carries (Roboto, regular and bold) |
| `massif-maps.bundle.mjs` | 29 KB | `createMap` and the typed API in one file, for a page with no bundler |
| `index.mjs`, `bridge.mjs`, `massif.mjs` | | the same, unbundled, for a bundler; `massif.mjs` is the low-level binding |
| `coi-serviceworker.js` | | cross-origin isolation for a host that cannot send headers ([below](#hosting)) |

Serve them **from your own origin**, all from one directory: the loader starts the SDK's worker
threads from its own URL, and a browser only starts a worker from the page's origin.

## Your first map

```html
<canvas id="map" style="width: 100%; height: 100vh; display: block"></canvas>
<script type="importmap">{ "imports": { "@massif-maps/web": "/massif/massif-maps.bundle.mjs" } }</script>
<script type="module">
  import { createMap } from '@massif-maps/web';

  const map = await createMap(document.getElementById('map'));
  map.addLayer('basemap', {
    type: 'raster',
    source: { type: 'http', url: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png', maxZoom: 19 },
  });
  map.camera().moveTo([6.8652, 45.8326], { zoom: 11 });
</script>
```

`createMap(canvas, options)` loads the module, starts the map on the canvas and returns the
`MassifMap`. Positions are `[lon, lat]` unless `projection` says otherwise. `moduleUrl` points at
`massif-web.mjs` when it is not beside the script that imports it - which is the case under a
bundler (copy the three `massif-web.*` files to your static assets and pass their URL). The
emscripten module itself is `map.module`, for its virtual filesystem.

## Hosting

The SDK's workers share memory, and a browser only allows that on a **cross-origin isolated**
page. The page (and any page framing it) must be served with:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: credentialless
```

`credentialless` rather than `require-corp`: tile servers do not send
`Cross-Origin-Resource-Policy`, and `require-corp` would block every tile. Tile and style URLs
still need CORS, as they do for any web map.

A host that cannot set headers - GitHub Pages - can load `coi-serviceworker.js` as the page's first
script. It installs a service worker that adds the two headers, and reloads the page once:

```html
<script src="/massif/coi-serviceworker.js"></script>
```

This site runs on exactly that. Check with `crossOriginIsolated` in the console: `createMap` needs
it `true`.

## Keeping tiles

A source with `type: 'persistent-cache'` keeps tiles in a database file. On the web the file lives
in the module's virtual filesystem, so mount IndexedDB there first and write it back now and then:

```js
const { FS } = map.module;
FS.mkdir('/cache');
FS.mount(FS.filesystems.IDBFS, {}, '/cache');
await new Promise((resolve) => FS.syncfs(true, resolve));
setInterval(() => FS.syncfs(false, () => {}), 10000);

map.addLayer('basemap', {
  type: 'raster',
  source: { type: 'persistent-cache', databasePath: '/cache/osm.db', source: { type: 'http', url: '…' } },
});
```

The [peak finder example](/examples#peak-finder) keeps its elevation and summit tiles this way.

## What differs from Android and iOS

- **Zoom numbers are MapLibre's.** A zoom on the web shows the area MapLibre GL shows at that
  number, which is one level wider than the same number on Android and iOS
  (`Options.zoomOffset` is 1 here). A style's zoom filters follow the same numbering.
- **One map per page.** The typed API talks to one module, and the module draws one map. Two maps
  are two pages - an `iframe` each, as the examples on this site are.
- **A map lives as long as its page.** There is no way to destroy one and start another in its
  place yet.
- **Events arrive on the page's thread, after the fact.** A handler cannot claim an event
  (`consumed` has no effect), so a click also reaches the handlers after it.
- **What the `standard` profile leaves out:** routing, geocoding and offline packages. Tiles,
  terrain, styles, labels, the persistent cache and search over loaded features are in.
- **Fonts.** The build carries Roboto; a style that names `sans-serif`, Arial or Helvetica gets it.
  Text elements and celestial labels are drawn by the browser, so any web font the page has loaded
  works there.

## The API

`@massif-maps/api` is the typed surface API on its own, platform-neutral - the same package the
NativeScript plugin is built on. Every path, spec key, method and event completes and type-checks,
because it is generated from the SDK's property table ([the surface API](/docs/api/)).
`@massif-maps/web` re-exports all of it, so an app imports from one place.

## Building it yourself

[`docs/maintenance/web-build.md`](/docs/maintenance/web-build) covers the emscripten build, the
module's exports and the bench pages.
