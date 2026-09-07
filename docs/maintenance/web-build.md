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

Add `--website` to put the same module under `website/static/preview`, which is what the
documentation site's [style preview](../tools/style-preview.md) page runs.

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

### Zoom: `Options.ZoomOffset`, and the three places a convention hides

The SDK calibrates zoom the way a 256-pixel slippy map does; maplibre and mapbox-gl calibrate on a
512-pixel tile, so the same NUMBER is one level closer there. Measured at zoom 13 in Paris on a 2x
display, before: massif **12.5729 m per CSS pixel** against maplibre's 6.2864 - a ratio of exactly
2.0000. The web host now sets `Options.ZoomOffset = 1` and the same measurement gives **6.2864,
ratio 1.00000**. Every other platform keeps the SDK's own convention; the option defaults to 0.

**It is a renumbering and nothing else.** Offset 1 at zoom 13 is offset 0 at zoom 14 - the same
camera, the same tiles, the same picture, a different number. That was checked by screenshotting
both and comparing: identical down to the street labels, with only the readout differing.

Getting there needed three things to move together, and each of the first two was tried alone and
was wrong in a way that looks like the answer:

| | |
|---|---|
| **The camera** | `ViewState::calculateZoom0Distance`. Alone, it renumbers the camera and leaves the tiles behind, so the same tile is drawn twice as large - every label and line with it. |
| **The tile level** | `TileLayer`'s `targetTileZoom` cap. It is this cap, not the screen-area rule, that ties a tile level to the zoom number. **Scaling the area rule instead is a trap**: it fetches a level COARSER and draws it bigger, which doubles labels just the same. |
| **The renderer** | `ViewState::getRenderZoom`, handed to `vt::ViewState`. vt sizes everything by `2^(zoom - tileZoom)`, so it has to be told the zoom the tiles were picked for. Miss this and labels are still double, with everything else already right. |

The arithmetic is `all/native/graphics/ZoomConvention.h`, header-only and covered by
`tests/api/ZoomConventionTest.cpp` - which asserts the traps above rather than only the happy path.

Two things the offset deliberately does not do. A **raster** source's 256-pixel tiles are drawn at
512 points and blur, exactly as they would in maplibre without `tileSize: 256`; set `TileDrawSize`
to compensate. And a **style's zoom stops** do not move: they are evaluated at the TILE zoom
(`mapnikvt/TileReader.cpp`), which the offset leaves alone, so a converted MapBox style is no more
and no less aligned than before.

Method: `screenToMap` at two points a known number of DEVICE pixels apart (the centre of the
drawing buffer maps exactly to `focusPos`, which is how the convention was confirmed), divided by
`devicePixelRatio`. Do not pan and read `focusPos` - kinetic pan keeps gliding after mouseup and
inflated the first measurement by 1.26x.

### A drag that ends off the canvas

Mouse `down` is bound to the canvas, but `move` and `up` go to the **document**, and `blur` to the
window - maplibre's own arrangement, for the reason its `handler_manager.ts` gives: there is no
pointer capture to lean on, so a release outside the canvas only ever reaches a document-level
listener. Bound to the canvas, a drag that ended anywhere else never saw its mouseup, the map
stayed in the drag, and every later hover panned it.

Two things follow. The move handler must ignore everything unless a drag is actually running,
because it now sees the pointer crossing the whole page. And coordinates have to come from
`clientX/clientY` minus the canvas rect, not from `targetX/targetY` - those are relative to
whatever the listener was bound to, which is no longer the map.

### Labels shredded by buildings from zoom 12

Not a glyph bug, though it looks like one: the text is drawn and then PAINTED OVER, so the letters
come out with pieces missing. `VectorTileLayer` defaults labels to `RENDER_ORDER_LAYER` and
buildings to `RENDER_ORDER_LAST`, and those two together put every extrusion on top of the text.
It starts at zoom 12 because that is where OpenMapTiles begins serving `building`.

Setting the labels to `LAST` is **not** enough on its own - `TileRenderer::onDrawFrame3D` runs the
2D label pass BEFORE the building pass, so the only labels that land above an extrusion are the
billboards. The pair that works is buildings on `LAYER` (back inline with their own layer) and
labels on `LAST`, which is what the preview sets on the layer it builds.

Worth knowing for any style with both, on any platform - the defaults are the SDK's, not the web's.

### Two double-click zooms

The host listened for the browser's `dblclick` and zoomed. So did the SDK, from the pointer stream
it is already given: `TouchHandler::doubleClick` puts the map into `SINGLE_POINTER_ZOOM`, which is
BOTH the double-tap zoom and the double-tap-and-drag zoom, and on release it only adds the tap zoom
when the gesture did not actually drag (`singlePointerZoomStop`).

A double-tap-and-drag therefore zoomed continuously, and then zoomed once more when the button came
up - the browser's `dblclick` arriving on top of a gesture the SDK had already handled. There is no
`dblclick` listener now; the SDK owns the gesture.

### A flight and a gesture fighting over the camera

`TouchHandler` cancelled the pan, rotation, tilt and zoom animations whenever the user touched the
map, but **not a flight** - `stopFlight` was called from nowhere except the facade method. So a
`flyTo` still in the air kept interpolating along its own path while the drag moved the camera too,
and the map flickered between the two: the flight's position, which near the start is still the
place you left, and the place you had just dragged to. Every platform, not only the web.

The fix is one more line at each of the eleven places the other four are stopped.

While there: use `flyTo`, not `fitBounds`, to travel. `fitBounds` moves through the pan and zoom
animations, whose duration is taken literally, so a fixed one drifts across a country at the same
rate it crosses a suburb. `flyTo` follows van Wijk's arc and, given a duration of **0**, picks
`S / 1.4` seconds itself - maplibre's rule, already implemented in `AnimationHandler` and easy to
override by accident. Paris to Grenoble comes out at 4.7 s.

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
- **3D terrain, shadows and the sky** are barely tested here. They compile, but the MRT and
  depth-texture paths have never been run against a WebGL 2 driver.
Raster tiles, MVT decoded through mapnikvt and styled by CartoCSS, and labels with halos and
accented glyphs all render — that part is observed, not inferred.

## The style preview on the documentation site

`/preview` on the site is this module with a React page around it. Two things are worth knowing.

**locateFile has to be explicit.** Emscripten's fallback resolves the `.wasm` and the preloaded
`.data` against the DOCUMENT, and the page sits at a different depth in the two places it runs:
`/preview/` on the dev server, `/preview.html` once built. The built one asked for
`/massif-demo.data`, one directory too high - a 404 that appeared only in production, which is
exactly the kind that reaches a user first.

**A rebuilt wasm needs a no-store dev server.** Its URL never changes, so a browser will happily
run yesterday's renderer against today's page - which reads exactly like "my change did nothing".
`website/plugins/style-preview` sends `Cache-Control: no-store` for that reason. Cost an afternoon
once: an A/B that appeared to disprove a fix was measuring the cached build.

**Where the wasm comes from.** It is a build artefact and is not tracked. `.github/workflows/web-preview.yml`
builds it whenever `all/native`, `libs-massif`, `web/` or the build scripts change on `master`, and
uploads it as the `web-preview` artefact; `docs.yml` downloads the newest successful one into
`website/static/preview` before building the site. That download is best-effort — without it the
page says the module is missing and the rest of the site deploys unaffected. It is a separate
workflow because docs.yml also runs on every documentation push and nightly for the roadmap page,
and an hour of emscripten per typo is not a trade worth making.

**The page adds no C++.** It runs the same `web/demo` module and drives it entirely through the
facade's C ABI: clear the layer list, build a `layer` from a spec, add it. The one thing that had
to reach the facade for it is `addFallbackFont` on the `style` kind — a decoder built from a spec
had no way to be given font bytes, so every converted MapBox style, which all name DIN Pro, lost
its labels. `web/demo/main.cpp` also adopts its `Layers` now, which is what makes the map
replaceable at all.

MapBox style JSON is translated in the page, by `mapbox2css` — the CLI's own TypeScript, which runs
in a browser unchanged apart from sprites. `sprite.ts` used to reach straight for `node:fs` and
pngjs; it goes through a `SpriteHost` now, and the browser one fetches the sheets and uses the
exact PNG codec in `png.ts`. That codec exists because the obvious route — `createImageBitmap` into
a 2D canvas — stores premultiplied colour, so reading it back changes every partly transparent
texel. On an SDF sheet, whose alpha channel is a distance field and not opacity, that is not a
rounding error but a mangled icon.

## What emcc found that no other build did

`stdext`'s `utf8_filesystem::fseek64`, `ftell64` and `ftruncate64` had a branch for Windows,
Android and Apple and **no `#else`** — so on emscripten the functions had no body at all. That is
undefined behaviour, and at `-O2` LLVM folded it backwards through the caller: `URLFileLoader`
reported that it could not open the file without ever calling `fopen`, so every `file://` read on
the web failed identically whether the file existed or not. The fix is a generic POSIX branch
(`fseeko`/`ftello`/`ftruncate`), which is what Apple was already using.

Beyond that, the two latent portability bugs (`stdext`'s `unistring`, harfbuzz's
`-Wunused`) were already fixed for
[the style tools' wasm build](../contributing/style-tools.md#the-wasm-build), which is what proved
`vt`, `mapnikvt`, `cartocss`, freetype and harfbuzz compile under emcc in the first place. What was
new here is `vt`'s GL renderer: the style tools build it with `EXCLUDE_GL=ON`, so `GLTileRenderer`
had never seen emscripten until this build. It compiled unchanged.

Two dependencies needed wiring rather than fixing: `zlib` comes from `libs-external` (there is no
system one, same as UWP), and `pion` is dropped (it is boost::asio sockets, which a browser has
none of).
