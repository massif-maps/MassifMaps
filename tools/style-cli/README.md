# @massif-maps/style-tools

Style conversion for [Massif Maps](https://github.com/massif-maps/MassifMaps). One command,
`massif-style`, with no toolchain to install.

```sh
npx @massif-maps/style-tools css2xml style/project.json style.xml
```

## Subcommands

| | |
|---|---|
| `css2xml <project.json> <out.xml>` | compile a CartoCSS style project to the mapnik XML the decoder reads |
| `mapbox2css <style.json> <outdir>` | translate a MapBox/MapLibre style to a CartoCSS project |

`css2xml` runs the SDK's own C++ compiled to WebAssembly — the same compiler the map uses at
runtime, so what compiles here renders there. `mapbox2css` is TypeScript.

```sh
massif-style css2xml --roundtrip project.json out.xml   # also parse the XML back and diff
massif-style mapbox2css style.json out/ --validate      # compile the result before writing it
massif-style mapbox2css topo.json out/ --contour-schema div   # nth_line -> div contour attributes
massif-style mapbox2css standard.json out/ --schema openmaptiles   # read OpenMapTiles tiles instead
```

`--schema openmaptiles` retargets the style at an OpenMapTiles tileset. Both **MapBox Streets v8**
and **MapTiler `planet_v4`** are understood as the source, detected from the style's own source-layer
names (`--source-schema mapbox|maptiler` says it outright). Layer names, field names and field values
are all rewritten, one source layer can feed several target ones, and anything without an equivalent
is dropped and named in the coverage report rather than guessed at. See
[the docs](https://massif-maps.github.io/MassifMaps/docs/contributing/style-tools#retargeting-at-another-tile-schema---schema).

`--contour-schema div` rewrites contour-layer `nth_line` tests onto a `div` (interval in metres)
attribute, for tiles built with the gdal ladder rather than MapTiler's schema. Only the major/minor
split survives — the base interval is not in the style — and `--contour-major-div` (default 100)
sets the threshold. Colours and widths are untouched.

`mapbox2css` skips MapBox properties CartoCSS has no equivalent for and prints a coverage report
naming each one and how often it appeared. `--strict` turns any skipped property into a non-zero
exit.

## Requirements

Node 20 or newer. The wasm build uses `NODERAWFS`, so it runs under Node only — not in a browser.

## Development

The wasm is built from the `libs-massif` submodule and is **not** in the repository. Either build it
(see [the docs](https://massif-maps.github.io/MassifMaps/docs/contributing/style-tools)) or download
`massif-style.wasm` and `massif-style.mjs` from a `style-tools-v*`
[release](https://github.com/massif-maps/MassifMaps/releases) into `wasm/`.

```sh
npm ci && npm run build && npm test
```
