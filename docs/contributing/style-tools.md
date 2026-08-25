---
title: Style tools CLI
description: "How massif-style is built to WebAssembly, wrapped as an npm package and released"
sidebar_position: 4
---

# The style tools CLI

`@massif-maps/style-tools` is one command, `massif-style`, carrying every style conversion the
project ships. It is an npm package so a style author needs no toolchain, and the conversions that
must agree with the SDK run the SDK's own C++ compiled to WebAssembly, so there is no second
implementation to keep in step.

| Subcommand | Runs | What it does |
|---|---|---|
| `css2xml` | wasm | compiles a CartoCSS style project to the mapnik XML the decoder reads |
| `mapbox2css` | TypeScript | translates a MapBox/MapLibre style JSON to a CartoCSS project |
| `carto2css` | — | not written yet |

`mvt2xml` stays a standalone host tool and is **not** in `massif-style`: it needs compiled
Boost.Serialization, and its `std::wstring(path.c_str())` only compiles where
`std::filesystem::path::value_type` is `wchar_t`, so it does not build on POSIX today.

## Why the split

A conversion is either **compilation** or **translation**, and they want opposite homes.

`css2xml` *is* the CartoCSS compiler — it calls `CartoCSSMapLoader` and `MapGenerator` directly. A
reimplementation in another language would drift from the decoder the moment a symbolizer property
is added, and the drift would only surface as a style that renders differently on device. It stays
C++.

`mapbox2css` is a mapping table: a few hundred rules from MapBox property names and expressions onto
CartoCSS ones. It needs no SDK code, it changes constantly, and a contributor fixing one rule should
not need a C++ toolchain. It is TypeScript.

WebAssembly is what lets both live behind one command. The alternative — prebuilt native binaries
per platform, fetched at install time — needs a build matrix (macOS, Linux, Windows, x64, arm64), a
hosting story and a fallback when a platform is missing. One `.wasm` in the tarball has none of
that.

`mapbox2css --validate` closes the loop: it runs its own output back through `css2xml` in the same
process, so a translation that produces CartoCSS the compiler rejects fails at conversion time
rather than on device.

## Layout

```
tools/style-cli/
  package.json           @massif-maps/style-tools, bin: massif-style
  src/
    cli.ts               subcommand dispatch
    wasm.ts              loads massif-style.mjs, calls main() with argv
    mapbox2css/          the translator
    generated/
      properties.json    the CartoCSS allowlist, from scripts/gen-cartocss-properties.py
  wasm/                  build output, gitignored — CI fills it
    massif-style.mjs
    massif-style.wasm
```

The C++ lives in the **`libs-massif` submodule**, under `cartocss/util/`, where `massif-style.cpp`
dispatches to `css2xml.cpp`.

## The property allowlist

`mapbox2css` translates *onto* a fixed set of CartoCSS properties, and that set is not written down
anywhere in the sources — it lives in the `bindProperty()` calls across
`libs-massif/mapnikvt/src/mapnikvt/*Symbolizer.h` and in `CartoCSSMapnikTranslator`'s name map.

`scripts/gen-cartocss-properties.py` reads both and emits
[the reference page](../features/cartocss-properties.md) and `src/generated/properties.json`. It
carries each property's **live** and **baked** flags, so the converter can tell at translation time
whether a `param::` it emits will change the map with a redraw or force a re-decode.

`--check` fails when either output is stale; the release workflow runs it before publishing.

## The wasm build

```sh
emcmake cmake -B build-wasm -DCMAKE_BUILD_TYPE=MinSizeRel
emmake make -C build-wasm massif-style
```

Link options:

```
-sNODERAWFS=1 -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1
-sMODULARIZE=1 -sEXPORT_ES6=1 -sINVOKE_RUN=0
-sEXPORTED_RUNTIME_METHODS=callMain
```

`NODERAWFS=1` is the load-bearing one. `css2xml`'s `AssetLoader` opens style assets with `fopen`
against a folder path, and it writes its result with `pugi::xml_document::save_file` — with the raw
Node filesystem both work unchanged, so the C++ needs no virtual-filesystem preloading and no I/O
rewrite.

Its cost is that the build is **Node-only**: `NODERAWFS` replaces the emscripten filesystem with
Node's `fs`, so this artifact cannot run in a browser. A browser playground would need a second
build with `MEMFS` and the assets pushed in from JavaScript.

Size, measured on the first CI build: **1.68 MB** of `.wasm` plus **73 KB** of `.mjs` loader — a
little under the 2.2 MB native binary at `MinSizeRel`, and a long way under the ~86 MB unoptimised.
The bulk is `boost.spirit` and `vt` template code that `mapnikvt` and `vt` hand over as object
libraries.

The link pulls in `cartocss mapnikvt vt pugixml tess2 brotli miniz mlt zlib freetype harfbuzz bidi`
plus Boost headers. All of them build under emcc — but two needed fixing first, and both were
latent portability bugs rather than anything to do with the tools:

- `stdext`'s `unistring` was `basic_string<uint32_t>`, and `std::char_traits` has no primary
  template on a libc++ new enough to have dropped that extension. Now `char32_t`.
- harfbuzz's `hb.hh` promotes `-Wunused` to an error for its own CI, and a clang that counts
  `-Wunused-template` in that group fails harfbuzz on harfbuzz's own templates.
  `HB_NO_PRAGMA_GCC_DIAGNOSTIC_ERROR` is upstream's opt-out for packagers.

Apple's libc++ and clang have neither behaviour, which is why every native build passed and the
first emcc build did not.

## Releasing

[`.github/workflows/release-style-tools.yml`](https://github.com/massif-maps/MassifMaps/blob/master/.github/workflows/release-style-tools.yml),
`workflow_dispatch` with `version` and `publish` inputs — the same shape as
[`build.yml`](https://github.com/massif-maps/MassifMaps/blob/master/.github/workflows/build.yml).

1. **build-wasm** — checkout with submodules, install emsdk, fetch Boost headers, build, upload the
   two artifacts.
2. **publish** — build the TypeScript, drop the wasm into the package, `npm publish --provenance`,
   and attach the same wasm to the GitHub Release for anyone not using npm.

Two things this repository does not have yet and this workflow is the first to need:

- an **`NPM_TOKEN`** secret. `@massif-maps/types` under `bindings/typescript/` is `private: true`
  and has never been published, so nothing has authenticated to npm from here before.
- `permissions: id-token: write` on the publishing job, which is what `--provenance` signs with.

Boost is fetched from `archives.boost.io`. The older
[`build_css2xml.yml`](https://github.com/massif-maps/MassifMaps/blob/master/.github/workflows/build_css2xml.yml)
still points at `boostorg.jfrog.io`; check it still resolves before relying on that workflow.
Only headers are needed (`./b2 headers`), never a compiled Boost.

`build_css2xml.yml` stays as it is — it builds native binaries for local debugging, which is a
different job from shipping the tool.

## What mapbox2css does not carry

Every skipped property is counted and named — `mapbox2css` prints a coverage report, and `--strict`
turns any drop into a non-zero exit. What it refuses, and why:

- **Layer types** with no symbolizer: `heatmap`, and `symbol` layers that have only an icon.
- **The gap list** — `line-blur`, `line-gap-width`, `line-gradient`, `fill-extrusion-pattern`,
  every `*-translate`, most `raster-*` adjustments. These are the CartoCSS gaps, not converter bugs.
- **Expressions with no CartoCSS form**: `feature-state`, `within`, `let`/`var`, `number-format`,
  `image`, `%` (absent from the grammar), `abs`/`floor`/`ceil` (absent from `_basicFuncMap`), and
  any `interpolate` over something other than zoom or with an exponential base other than 1.
- **Draw order across source-layers.** One entry in the project's `layers` array pulls *every*
  attachment of that name, so two MapBox layers on different source-layers cannot be interleaved.
  The converter reports how many layers end up out of order rather than reordering silently.

Three traps the CartoCSS grammar sets, all of which the tests pin:

- expression-level booleans are `&&` and `||`; `and`/`or` are predicate syntax only;
- `^` is **XOR**, so MapBox's exponent has to become `pow()`;
- a namespaced field is quoted in a *predicate* (`['mapnik::geometry_type' = 2]`) and bare in an
  *expression* (`[view::zoom]`).

## What could be better

- **No browser build.** `NODERAWFS` rules it out. A `MEMFS` variant would give a web playground that
  compiles a style in the page, which is the natural home for a "does my style still work" check.
- **`carto2css` does not exist.** The reverse direction (mapnik XML back to CartoCSS) has no
  generator; `MapGenerator` only goes one way.
- **Only the size is measured.** Startup time and peak memory for a large style project are still
  unknown; a slow load would push the design towards keeping the module warm across subcommands.
- **The sprite is not converted.** `icon-image` is dropped, so a symbol layer with an icon and no
  text produces nothing. Converting the sprite sheet into individual bitmaps plus `marker-file`
  paths is the next real chunk of `mapbox2css`.
- **No real-style coverage numbers yet.** The report exists; running it over a set of public styles
  is what should decide which SDK gap to close first.
