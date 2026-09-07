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

```
?zoom=14&source=<url-encoded MVT template>&css=<url-encoded CartoCSS>
```

The style is passed in rather than fetched because `main()` runs on the browser's main thread,
where a synchronous fetch is illegal. The JavaScript binding is what will replace it.

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

## What the build does not carry

- **`BitmapCanvas`**, and with it the `Text` and `BalloonPopup` vector elements. The other
  platforms draw those through a system 2D text API; the browser's is asynchronous and
  main-thread-only, which this synchronous interface cannot reach from a worker. Every call warns
  once and produces an empty bitmap.
- **The `lite` profile only** so far: no sqlite, so no persistent tile cache, no offline packages,
  no routing or geocoding.
- **No JavaScript binding yet.** `web/demo/main.cpp` hard-codes its layer; the binding over
  [`MassifApiC.h`](../internals/api-facade.md) is the next step, and
  `bindings/typescript/massif.d.ts` already describes that surface.
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
