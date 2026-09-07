---
title: The web (wasm) build
description: How the SDK builds under emscripten, the platform layer it needs, the threading contract the browser imposes, and what the build does not carry yet.
sidebar_position: 7
---

# The web (wasm) build

The same `all/native` core, compiled by emscripten and rendering into a `<canvas>` through WebGL 2.
It exists for the style preview on the website — and as a development loop that does not need an
Android emulator.

```sh
source ~/emsdk/emsdk_env.sh
python3 scripts/build-web.py --profile lite --configuration RelWithDebInfo --build-demo
python3 web/demo/serve.py            # http://localhost:8088
```

Size, measured with emscripten 6.0.9 on the `lite` profile at `--configuration Release`:

| Artefact | Raster only | + vector tiles, CartoCSS and labels |
|---|---|---|
| `.wasm` | 2.5 MB | **3.8 MB** |
| `.wasm` gzipped | 910 KB | **1.5 MB** |
| `.mjs` loader | 297 KB | 151 KB |

The vector column is what the style preview needs; the difference is mapnikvt, cartocss, freetype
and harfbuzz, which the raster-only demo never referenced. A preloaded font adds its own
`.data` file on top (306 KB for Roboto).

Use `--configuration RelWithDebInfo` while developing, but do not quote its size: it carries DWARF
and the `.wasm` comes out around 340 MB.

## The bench

`web/demo` is the browser's answer to the Android demo's intent extras: the map is configured from
the query string, so a camera and a style are a link.

| Parameter | Meaning |
|---|---|
| `lon`, `lat`, `zoom` | The camera. Defaults to Paris at z12 |
| `source` | Tile URL template. A `.png`/`.jpg`/`.jpeg`/`.webp` in it means raster, anything else vector |
| `css` | URL-encoded CartoCSS, used when the source is vector |
| `project` | A CartoCSS project directory under `web/demo/styles`, preloaded like the fonts |
| `style` | Which entry file of that project to use, without its extension. Default `project` |
| `terrain` | A DEM tile URL or `.pmtiles` archive; turns on 3D terrain |
| `terrainMaxZoom` | Deepest level of that DEM. Default 12, which is Mapterhorn's |

`project` is how a converted MapBox style is previewed:

```sh
curl "https://api.mapbox.com/styles/v1/mapbox/standard?access_token=$TOKEN" -o standard.json
node tools/style-cli/dist/cli.js mapbox2css standard.json web/demo/styles/mapbox-standard \
  --source-schema mapbox
```

Pass **`--sprite-key`** or there are no POI icons at all: Mapbox serves the sprite sheet from the
style's own URL, which needs the token, and without it the converter says
`Sprite not loaded ... icons will be dropped` and carries on.

Mapbox Standard's 150 layers convert and render. `CompiledStyleSet` wants the entry file at the
**root** of its package, so the package is the project directory and the style name is the file
without its extension - `project`, or `night` for one of the themes the converter also writes.

Its labels name fonts nobody has (`DIN Pro Medium`), and `MBVectorTileDecoder` asks for a font by
name STRICTLY so that a font list can fall through to its next entry. The bench therefore registers
every TTF in `/fonts` with `addFallbackFont`, which is what makes the labels appear at all.

```
?zoom=14&source=<url-encoded MVT template>&css=<url-encoded CartoCSS>
```

The style is passed in rather than fetched because `main()` runs on the browser's main thread,
where a synchronous fetch is illegal. The JavaScript binding is what will replace it.

Copy `web/demo/config.example.json` to `web/demo/config.json` to set what `/demo/` shows with no
query string at all. It is gitignored because it holds a tile-provider token; the query string still
wins over it. The buttons in the corner fly the camera through the facade, which is the shortest
example of driving the map from JavaScript.

### Gestures

`WebMapView` handles the mouse itself and leaves touch to the SDK's `TouchHandler`, which already
knows pinch and two-finger rotate. The speeds are **ported from maplibre-gl-js**
(`src/ui/handler/mouse.ts`, `scroll_zoom.ts`) so the map feels like any other web map:

| Input | What it does | Constant |
|---|---|---|
| Drag | Pan | the SDK's own |
| Wheel | Zoom toward the pointer | `1/450`, sigmoid `2/(1+exp(-abs(Δ·rate)))` |
| Trackpad pinch (wheel + ctrl) | Zoom toward the pointer | `1/100` |
| Right-drag, or ctrl-drag | Rotate and tilt | `0.8` and `0.5` degrees per CSS pixel |
| Double-click | Zoom in 1, shift for out, toward the pointer | 0.3 s |

Two sign traps, both found by feel and then explained:

- **Both rotate and pitch are negated** against maplibre's constants. maplibre turns the camera's
  *bearing* where `rotate()` turns the map under it, and this SDK's **tilt is 90 looking straight
  down** where mapbox's pitch is 0 there. Taken as written the map moved the wrong way on both axes.
- The speeds are per **CSS** pixel, but pointer coordinates are scaled to device pixels for
  `onInputEvent`, so the delta has to be divided back out or a drag turns the map twice as far on a
  2x display.

A mouse also needs a much smaller click tolerance than a finger: `ClickHandlerWorker` would not
start panning until the pointer had travelled 0.2 inch, which is about 32 CSS pixels and made every
drag feel stuck. `Options.clickMovingTolerance` names that threshold in dp - still 32 by default, so
Android and iOS are unchanged - and the web host sets 3, which is maplibre's `clickTolerance`.

### Zoom is one level off maplibre's, and TileDrawSize cannot fix it alone

Measured at zoom 13 on a 2x display: massif draws **12.5726 m per CSS pixel** where maplibre's zoom
13 is 6.2864. Massif's zoom 13 is maplibre's zoom **12**, exactly (ratio 2.0000) - massif's world at
zoom 0 is `tileDrawSize` = 256 CSS pixels where maplibre's is 512.

`setTileDrawSize(512)` does buy exact parity - re-measured at **0.99998** - but it is the same knob
that decides how big a tile is DRAWN, so every label, line width and halo doubles with it. Tried
and reverted; the screenshots are unambiguous. `MBVectorTileDecoder`'s pixel scale is not a way out
either: it sets the resolution a glyph is rasterised at, not its size on screen, so lowering it just
makes the same oversized text blurry.

Aligning the two properly means decoupling the zoom-to-distance mapping in `ViewState` from
`TileDrawSize` - a zoom offset - which is an SDK change, not a web default.

Method: pan a known number of CSS pixels and read `focusPos` before the release. **Before** - kinetic
pan keeps gliding after mouseup and inflated the first measurement by 1.26x.

### Range requests, and why PMTiles failed on the web

A PMTiles archive is read by HTTP range, and `HTTPClient` checked the `Content-Range` of every 206
against the offset it asked for. **A browser hides `Content-Range` from the page** unless the server
sends `Access-Control-Expose-Headers`, and tile hosts generally do not - so the header read as
absent, the offset compared as 0, and every range request was rejected with
`Content range mismatch: 0/127`. The check now applies only when the header is actually readable:
the range was honoured either way, and refusing it makes every PMTiles archive unreadable in a
browser.

Mapterhorn's planet archive (`https://download.mapterhorn.com/planet.pmtiles`) is 705 GB of
Terrarium-coded WebP, z0-12, and serves `access-control-allow-origin: *` - only the ranges actually
read are fetched.

### 3D terrain, and what a desktop should set differently

`?terrain=<url>` attaches a DEM. Verified against Mapterhorn's planet archive and against AWS's
terrarium tiles; both read the same elevation, so **Terrarium is the right decoder for Mapterhorn**
(`?demEncoding=mapbox` switches to Terrain-RGB and is visibly wrong - the clearance shoves the
camera out to zoom 6).

Elevation comes back in PROJECTED units, not metres: Mont Blanc reads 6849 where the mountain is
4808, because the Mercator scale at 45.8 degrees is 1/cos = 1.435. 4808 x 1.435 = 6900. Nothing is
wrong with the DEM when that number looks too big.

The bench sets two things a phone would not:

| Setting | Phone default | Web | Why |
|---|---|---|---|
| `autoFlattenTilt` / `autoFlattenParallax` | 88 / 2 | **0 / 0** (off) | Auto-flatten drops the height field when the map looks straight down, to save a phone the cost. A desktop can hold it up, and dropping it every time the map returns to 88 degrees is a visible sink-and-rise |
| `meshResolution` | 64 | **128** | Cells per tile edge, clamped to 2..256. 64 is a phone budget |

### Draw distance

`Options.drawDistance` defaults to 16, which is a phone's battery talking: tilt the map and
buildings and terrain stop at a near band. The web host sets **96**, because a desktop GPU can
afford what mapbox and maplibre draw - from Chamonix at tilt 35 that reaches Lausanne.

### DPI

`syncCanvasSize` sets `Options.DPI` to `Const::UNSCALED_DPI * devicePixelRatio`, the same thing the
iOS host does with `UIScreen`'s scale. Miss it and the SDK takes a 2x canvas for a 1x screen: every
tile is drawn at half the size it should be, so roughly four times as many are on screen at once and
the map looks tiny and loads slowly. It is set on every resize rather than once, so it follows a
window dragged to another display.

### Fonts

A style that names a font it does not ship falls through to `SystemFontUtils`, which on the web
means `/fonts/<name>.ttf` in the virtual filesystem. Drop TTFs into **`web/demo/fonts/`** and the
link preloads the directory as `/fonts` — `Roboto.ttf` there is what
`text-face-name: 'Roboto'` resolves to. The directory is gitignored: fonts are a licensing
question, so the bench asks for one rather than shipping one. Without it the build carries no font
and a style with a text rule draws no labels.

## The platform layer

`web/native/` is the platform directory, the same shape as `android/native` and `ios/native`, and
CMake globs it under `elseif(EMSCRIPTEN)`. There is no wrapper directory: the web build has no
SWIG bindings and is meant to be driven through the facade's C ABI.

| File | What it does |
|---|---|
| `network/HTTPClientEmscriptenImpl` | The Fetch API, called synchronously |
| `ui/WebMapView` | The canvas host: WebGL 2 context, `requestAnimationFrame`, pointer/touch/wheel events |
| `utils/AssetUtils` | Assets from the emscripten virtual filesystem, under `/assets` by default |
| `utils/SystemFontUtils` | Fonts from `/fonts` — a browser has no font API to read outlines out of |
| `utils/PlatformUtils` | `PLATFORM_TYPE_WEB`; the app identifier is `location.origin`, which is what a tile server checks |
| `utils/ThreadUtils`, `components/Task` | Trivial; a web worker has no priority to set |
| `graphics/BitmapCanvasWebImpl` | **Not implemented** — see below |

`vt::parseFontNames` gained a `web` platform tag, so a style can write `web:Inter` next to
`android:Roboto`.

## The threading contract

The SDK's tile pools and its cull, label and billboard workers are real threads, so the build is
`-pthread` and the page **must be cross-origin isolated** — `Cross-Origin-Opener-Policy:
same-origin` and `Cross-Origin-Embedder-Policy: require-corp`, which is all `web/demo/serve.py`
adds over `http.server`. Without them the browser refuses `SharedArrayBuffer` and the module never
starts.

Two rules follow from that split, and both cost a debugging session to find:

- **GL lives on the main thread only.** The context is created there and every frame is drawn from
  `requestAnimationFrame`. A GL call from a worker fails with `GLctx is undefined`.
- **`requestAnimationFrame` is main-thread only too.** The redraw request comes from the tile and
  label workers, so `WebMapView::requestRedraw` marshals it with
  `emscripten_async_run_in_main_runtime_thread`. Calling it directly from a worker does nothing at
  all: the first frame draws, the flag stays set, and the map is frozen with tiles arriving behind
  it.

### Isolation on GitHub Pages

The docs site is on GitHub Pages, which serves no custom headers at all — so the threaded module
cannot start there. `emscripten` fails with
`SharedArrayBuffer transfer requires self.crossOriginIsolated` before a single tile is fetched.

`web/demo/coi-serviceworker.js` is the way around it: registered from the page, it re-serves every
response with the two headers and reloads once, which is what makes a static host isolated. COEP is
**`credentialless`**, not `require-corp` — a tile server sends no
`Cross-Origin-Resource-Policy`, and `require-corp` would block every tile.

**Unverified.** The mechanism is the standard one, but the embedded browser used to build this
refuses to register any service worker, so the shim has never actually run. Check it in a real
browser before relying on it:

```sh
python3 web/demo/serve.py --no-headers     # serve the way GitHub Pages does
```

The page should reload itself once and then render; `crossOriginIsolated` in the console tells you
which side of the fence you are on.

`PTHREAD_POOL_SIZE=8` covers the pools plus the three workers. The pool is pre-warmed because
`pthread_create` on the main browser thread cannot block waiting for a worker to spawn.

A synchronous `emscripten_fetch` is illegal on the main thread and legal on a worker, which is
exactly where the SDK does its blocking tile loads — so the arrangement above is what makes the
HTTP client work at all.

### emscripten's response headers need trimming

`emscripten_fetch_unpack_response_headers` keeps the space after the colon and copies one character
too many, so every value arrives as `" 32818\n"`. `Content-Length` then fails
`boost::lexical_cast<uint64_t>` and every tile load dies as `bad lexical cast`. The impl trims both
key and value.

## The JavaScript binding

`web/js/massif.mjs` wraps the facade's C ABI. It is deliberately thin - the facade is a table, so a
new SDK feature reaches JavaScript without touching the binding:

```js
const massif = new Massif(module);          // module is the emscripten Module
const camera = await MassifCamera.attach(massif);
camera.zoom;                                 // 13.29
camera.flyTo({ position: [2.35, 48.86], zoom: 15, duration: 1.5 });
massif.call(camera.handle, 'screenToMap', { x: 100, y: 200 });
```

Three things it has to get right, and each was a bug first:

- **`-sEXPORTED_FUNCTIONS` REPLACES the default list**, so `_main` has to be in it or the program is
  stripped and nothing runs at all. The list is generated from `MassifApiC.h` by CMake, so a new
  `mm_` function reaches JavaScript by existing.
- **The host has to adopt its map** - `MassifInterop::adopt("map", "map", view)` - or the camera has
  nothing to point at.
- **`await Module(...)` resolves before `main()` has run** when pthreads are on, so reading the map
  straight away is a race. `MassifCamera.attach` polls for it.
- **`mm_call` answers with a result HANDLE, not a string buffer.** Read it like any other object and
  release it with `mm_destroy_handle`, or the context holds it forever. Reading it as a string
  buffer silently returned garbage for every method that produces a value.
- **Out-parameters must be zeroed first.** `_malloc` does not, so a void method's result length came
  back as whatever was in that memory - 4, which then parsed as a control character.

`bindings/typescript/massif.d.ts` describes what can be passed; it is generated by
`scripts/gen-api-bindings.sh`.

## What the build does not carry

- **`BitmapCanvas`**, and with it the `Text` and `BalloonPopup` vector elements. The other
  platforms draw those through a system 2D text API; the browser's is asynchronous and
  main-thread-only, which this synchronous interface cannot reach from a worker. Every call warns
  once and produces an empty bitmap.
- **The `lite` profile only** so far: no sqlite, so no persistent tile cache, no offline packages,
  no routing or geocoding.
- **The layer is still built in C++.** `web/demo/main.cpp` reads the source and style from the
  query string; creating a layer from JavaScript works through `massif.create(...)` but the demo
  does not do it yet.
- **3D terrain, shadows and the sky** are untested here. They compile, but the MRT and
  depth-texture paths have never been run against a WebGL 2 driver.
Raster tiles, MVT decoded through mapnikvt and styled by CartoCSS, and labels with halos and
accented glyphs all render — that part is observed, not inferred.

## What emcc found that no other build did

Nothing new this time — the two latent portability bugs (`stdext`'s `unistring`, harfbuzz's
`-Wunused`) were already fixed for
[the style tools' wasm build](../contributing/style-tools.md#the-wasm-build), which is what proved
`vt`, `mapnikvt`, `cartocss`, freetype and harfbuzz compile under emcc in the first place. What was
new here is `vt`'s GL renderer: the style tools build it with `EXCLUDE_GL=ON`, so `GLTileRenderer`
had never seen emscripten until this build. It compiled unchanged.

Two dependencies needed wiring rather than fixing: `zlib` comes from `libs-external` (there is no
system one, same as UWP), and `pion` is dropped (it is boost::asio sockets, which a browser has
none of).
