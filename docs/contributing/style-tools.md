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

- **Layer types** with no symbolizer: `heatmap`.
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

## Labels: three MapBox properties, one CartoCSS placement

MapBox splits the label's orientation over `symbol-placement` and the two `*-rotation-alignment` /
`*-pitch-alignment` pairs, both defaulting to `auto`. The spec resolves `auto` rotation as `map` for
a line placement and `viewport` otherwise, and `auto` pitch as whatever the rotation is — so a plain
point label is **screen-facing**, which CartoCSS spells `billboard`, not `point` (flat in the ground
plane, only swivelling). [`placement.ts`](https://github.com/massif-maps/MassifMaps/blob/master/tools/style-cli/src/mapbox2css/placement.ts)
resolves the triple once, for the text and the icon separately:

| `symbol-placement` | pitch | rotation | CartoCSS |
|---|---|---|---|
| point (or unset) | viewport | — | `billboard` |
| point (or unset) | map | — | `point` |
| line / line-center | map | map | `line` |
| line / line-center | viewport | map | `billboard-line` |
| line / line-center | — | viewport | `billboard-line-repeat` |

MapTiler's topo-v4 sets `text-pitch-alignment: viewport` on all 17 of its line-placed layers, so
dropping the alignments left every road name lying flat on the terrain.

**Everything MapBox measures in ems** — `text-max-width` (10 by default), `text-offset`,
`text-letter-spacing`, `text-line-height` — is pixels in CartoCSS, so each is multiplied by
`text-size` (16 when the layer states none). Taken literally, that 10-em default wrapped every name
at 10 **pixels**, one word per line, and the lines then overlapped. A zoom-driven `text-size` stays
an expression (`(10 * linear([view::zoom], …))`) rather than being pinned to one zoom. A line-placed
label is laid out along its line and never wrapped, matching MapLibre.

Two more that only look like gaps: `text-overlap` is the modern spelling of `text-allow-overlap`
(`cooperative` has no equivalent and is taken as `never`), and `symbol-sort-key` is
`text-placement-priority` **negated** — MapBox places the lowest key first, the culler takes the
highest priority.

## A field belongs in a predicate, never in a value

`Symbolizer::createFeatureProcessor` runs **once per rule, with no feature bound**, and
`ColorFunctionProperty::buildFunction` only defers to a per-frame function for view-state and
live-parameter expressions. A `[class]` in a property *value* is therefore evaluated there and then
against nothing: the colour collapses to null, `parseColor("")` throws, and **the whole rule is
lost for that tile** — which is what "some tiles have no roads, others do" looks like. A float
quietly becomes 0 instead of throwing.

Measured on MapTiler topo-v4, cleared cache, z14: two `line-color` declarations reading `[class]`
and `[paved]` produced 3 `Color parsing failed`; replacing just the field with a constant — keeping
the nested ternary — brought it to 0. The ternary is fine, the field is not.

So [`split.ts`](https://github.com/massif-maps/MassifMaps/blob/master/tools/style-cli/src/mapbox2css/split.ts)
turns a `case`/`match` over a field into **one attachment per branch**, each with a constant value
and the branch's condition added to the filter. Later branches exclude the earlier ones, because
MapBox takes the first match. On topo-v4 that is 17 layers and **+24 attachments**.

- A `match` over a plain `["get", f]` uses the **legacy** filter spelling, which lands in brackets
  (`[class = 'motorway']`) instead of a `when()`.
- Past 8 variants a layer is left whole and its field-driven values keep only their **fallback** —
  the same thing the decoder would have evaluated, minus the broken rule. It is counted as an
  approximation.
- A value with no fallback (`["get", "width"]`) is **dropped** with that reason, not emitted.
- `text-field` is exempt: the text is evaluated per feature inside the processor rather than through
  a `Property`, so it reads fields correctly.

## How far apart labels stay

MapBox pads a label's collision box by `text-padding` on **every side** — 2 px on any layer that
states nothing — so two labels end up at least twice that apart. The culler's `minimum-distance` is
the single buffer between a pair, hence `2 ×` the padding. It used to be dropped as "no CartoCSS
equivalent", which it is not.

`--label-spacing N` scales that gap for a map that wants thinning beyond what the style asks;
1 is the style's own value.

A **line-placed** label is left out of that default. When no minimum is stated the decoder floors it
at the label's own size, which is what stops a repeat of the same name being drawn twice where two
tiles cut the same road (`TextSymbolizer`, next section); writing 4 px there disabled the floor. An
explicit `text-padding` still wins.

## A prefix test, and the two spellings of a regex

`["==", ["slice", ["get","ref"], 0, 1], "D"]` is how a style tests a **prefix**, and every
country-specific road shield in MapTiler streets-v4 is gated on one. CartoCSS has no substring, but
it has a full regex match, so a prefix is `D.*` — and a slice from 0 is the only use of `slice` that
survives; anything else is still refused.

The trap is that the **same operator has two spellings**. CartoCSS writes it `=~`
(`CartoCSSParser`), and the mapnik XML the compiler emits writes it `.match(...)`
(`ExpressionGenerator`). Emitting the XML form into a `.mss` does not warn — the whole style fails
to load, and the app throws `CartoCSS style loading failed: Syntax error` with a line number and
nothing else. `mapbox2css --validate` exists to catch exactly this by compiling its own output, and
it could not run here because `css2xml` overflows the wasm stack on a style this size.

## Where an icon sits relative to its label

The image is moved clear of the text by half its height, but **only when the style says nothing
about where the text goes**. A stated `text-offset` IS the style's own answer, and the vertical
alignment beside it already puts the text clear; adding half an icon on top doubled the gap, so a
POI name floated well below its pin where MapTiler draws the two nearly touching.

## Two operators that cost whole layers

Both were found rendering MapTiler streets-v4 and both failed silently — the coverage report named
them, the map just looked wrong.

**`["in", needle, ["literal", [...]]]`** is the expression *operator*, a different thing from the
legacy `["in", key, v1, v2]` *filter*, and only the second was handled. It is how a modern style
says "class is one of these", so a layer whose filter used it was dropped **whole**: streets-v4 lost
every minor-road FILL and drew its outline alone, which reads as grey roads. It becomes the or-chain
it is.

**`["interpolate", ["exponential", b], …]`** had no CartoCSS form, so the property was dropped and
the line fell back to its default width — a pathway drew as a fat solid grey line instead of a thin
one. CartoCSS has `linear` and `cubic` and no base, so the curve is **resampled** into extra linear
stops (4 per stop interval): it agrees at every original stop and stays close between them, where
substituting a plain linear is out by about a third at the midpoint at base 2. A stop that is not a
plain number has no curve to sample and still falls back to linear, reported.

## Retargeting at another tile schema (`--schema`)

MapTiler's `planet_v4` splits by **source layer** what OpenMapTiles splits by a **`class` field**: it
has a `forest` layer and a `grass` layer where OpenMapTiles has one `landcover` with
`class = 'wood' | 'grass'`. So `--schema openmaptiles` is a rename plus an extra filter clause, per
layer — the table is in [`schema.ts`](https://github.com/massif-maps/MassifMaps/blob/master/tools/style-cli/src/mapbox2css/schema.ts).

Only what is actually equivalent is listed. A source layer with no entry is **dropped and named in
the coverage report** rather than guessed at: a wrong guess draws the wrong features, which is worse
than drawing none and much harder to notice. On MapTiler topo-v4 that leaves two — `archipelago_label`
and `country_disputed_label`, both of which depend on fields OpenMapTiles has no equivalent for.

**Fields are the second half of the problem, and the harder one.** MapTiler gates every place label
on `iso_a2`; OpenMapTiles `place` carries `class`, `name` and `rank` and nothing else, so the test
can only fail and the layer draws nothing at all — strictly worse than not testing. `MISSING_FIELDS`
lists those per target layer and an existence test on one is dropped and reported. Substituting a
bare `true` is not enough: `["all", true, …]` is not a filter and the whole layer is then dropped as
malformed, so the constant is folded into its parent instead, and a filter that collapses to `false`
drops the layer with that reason.

Verified at La Clusaz z14.5 against an OpenMapTiles tileset: landcover, roads, buildings, water,
water names, road names, and village *and* hamlet labels all render.

**Draw order is the part that cannot be preserved, and it is separate from placement.** A CartoCSS
project entry pulls **every attachment of its source-layer** together, so MapBox layers of different
source-layers that interleave collapse into one block — the converter counts and names them ("N
layer(s) draw out of order"). `--schema` makes that worse by construction, since many MapBox layers
now share one target source-layer. Label **placement** does not go through that: the priority is
computed straight from the MapBox layer index (see below), so which label wins a collision is exact
even where draw order is approximate.

## Bracketed predicates, not `when()`

A CartoCSS selector is `*predicate`, and the two spellings cost different things: `[class = 'x']`
is a plain filter the decoder decides per rule, while `when(...)` carries a whole expression it
evaluates **per feature**. The converter used to bracket only the LEGACY filter forms
(`["==", "class", "x"]`), so a style written in expressions — which is every modern one — paid for a
`when()` on tests that are ordinary comparisons. The expression spelling now brackets too:
`["==", ["get", "class"], "x"]`, `["geometry-type"]` and `["id"]`, for every comparison operator.

`["match", input, [...], true, false]` is how MapTiler spells "input is one of these". The generic
match translation wrapped it in `? true : false`, which the decoder re-evaluates for every feature;
as a filter it is just the or-chain, and a one-label match is an equality that brackets.

Measured on MapTiler topo-v4: **389 `when()` down to 191**, all 91 `? true : false` wrappers gone,
declarations byte-identical, and 0.06% of pixels different at La Clusaz z14.5 (label jitter).

## A fill's outline

`fill-outline-color` has no polygon property to land on, so it becomes a second symbolizer — a line
on the same rule. The width is the trap: MapBox draws that outline with `gl.LINES`, which is **one
DEVICE pixel** whatever the display, while a CartoCSS width is multiplied by the pixel scale. `1`
therefore came out ~3 px on a 2.75x phone, and at z14.5 a village building is 10-25 px across, so the
outline covered it: measured at La Clusaz, **7456 outline pixels against 1747 of fill** — buildings
read as solid dark grey instead of the light fill the style asks for. The converter emits `0.4`,
which reads as a hairline from 2x up, and reports it as an approximation because no constant is right
at every dpi.

## How often a line label repeats

`symbol-spacing` is `text-spacing`, and the two defaults are opposites: MapBox repeats a line label
every **250 px**, CartoCSS's 0 means **one label for the whole line**. So the default has to be
written out, like `text-max-width` — without it a long road got its name once and a contour ring got
it once. Measured at La Clusaz, z14.5: one contour label on screen before, ten after.

`symbol-placement: line-center` is excluded — it draws one label at the middle whatever the spacing
says, and 0 is how CartoCSS spells that.

**Honest limits.** Porting `text-padding` changed placement only slightly. A deliberate A/B at
`text-min-distance: 24` removed roughly a third of the labels, so the lever works and its unit is
device pixels — but a converted style at z10 still shows far more places than MapTiler's own render
of the same style at the same zoom, and the padding is not what accounts for that. The cause is not
yet found; `--label-spacing` is a knob, not the answer.

## Which label wins a collision

`symbol-sort-key` orders symbols **within** a MapBox layer; between layers the style's own order
decides, later winning. The SDK's culler compares one `*-placement-priority` across every layer at
once and only falls back to the layer index — so translating the sort key literally let a village
with `rank 1` (priority −1) beat a town with `rank 12` (−12) whatever layer each came from. Annecy's
neighbours were drawn and Rumilly was not.

The layer's position is therefore the leading term:

```
text-placement-priority: (11200000 - (0 + [rank]));
```

`layerIndex × 100000`, minus the sort key (MapBox places the lowest first, the culler takes the
highest). The stride only has to exceed the range a sort key spans — MapTiler's widest is the
capital's `-1000`. A layer with no sort key still gets its base, so layer order alone is honoured.

## An icon and its text are ONE label

MapBox draws a symbol's icon and text as a single symbol that never collides with itself. Emitted as
a `markers` symbolizer beside a `text` one they are **two** labels, they collide, and the marker
wins: a city dot appeared with no name beside it, and removing the marker by hand brought "Annecy"
straight back. `ShieldSymbolizer` is the one-label construct, so a symbol layer with both becomes a
shield and every `text-*` declaration is renamed into it.

Most names just take the prefix. The exceptions exist because `shield-dx/dy` move the **image**:
the text's own offset is `shield-text-dx/dy`, and `text-opacity`/`text-transform` become
`shield-text-*` for the same reason.

Two things the shield cannot carry, both baked into the bitmap instead:

- **No `sdf`.** The distance field is resolved and the style's `icon-color` painted in, so one
  sprite drawn in two colours is two files (`circle-dot-000000.png`).
- **No image size.** There is no shield equivalent of `marker-width`, so `icon-size` is resampled
  into the PNG. A zoom ramp has no single answer and takes the mean of its stops (`-x65`).

`shield-unlock-image` is always on, and the image is moved clear of the text by half its height —
`text-anchor: bottom` anchors the text's bottom edge, so the name sits above and the dot below it,
which is what MapBox's anchoring produces without ever needing the two to be separate labels.

## Road shields become a text plate

A MapBox road shield is a sprite drawn BEHIND its ref, and the sprite is picked per feature —
`concat('transportation:', concat('road_', to-string([ref_length])))` — so there is no single image
to slice out, and `icon-image` used to be dropped with the whole shield. But the sprite is an SDF
**tinted by `icon-color`** and outlined by `icon-halo-*`, and CartoCSS draws exactly that shape
without any image:

| MapBox | CartoCSS |
|---|---|
| `icon-color` | `text-background-fill` |
| `icon-opacity` | `text-background-opacity` |
| `icon-halo-color` | `text-background-border-fill` |
| `icon-halo-width` | `text-background-border-width` |
| the sprite's rounding | `text-background-radius: 2` |

The country artwork is lost; the ref stays readable, and every note says so.

What makes a layer a shield rather than a POI ([`shield.ts`](https://github.com/massif-maps/MassifMaps/blob/master/tools/style-cli/src/mapbox2css/shield.ts)):
the sprite name reads the **feature**, and the text sits **on** the icon. A POI icon is per-feature
too but its text is pushed below it (`text-anchor: top`, `text-offset: [0, 0.8]`); a town's circle
is centred but picked by **zoom**, not by the feature. Both stay markers. An `icon-color` is
required — without a fill there is only a border round nothing.

Two consequences worth knowing:

- The plate colours are usually a `case` over `iso_a2`/`network` with dozens of branches, so they
  pass the split cap and keep their **fallback** — one plate colour for the world.
- A shield whose `text-field` cannot be translated draws **nothing**. A plate is a background *for*
  text; on its own it is a floating box. MapBox's own shields `slice` the ref per country and
  CartoCSS has no `slice`, so a text-field that branches is retried with its fallback branch —
  `[ref]` for everything outside Brazil — rather than losing the label.

MapTiler streets-v4 yields 10 plates across its four shield layers; topo-v4 has no shields at all.

## Three traps that only show on a device

None of these fail a compile, and each looked like an SDK bug until the style was read.

**An SDF sprite has to be re-encoded, not copied.** MapBox's distance field (tiny-sdf: cutoff
0.25, radius 8) puts the edge at **0.75** with 1/8 per texel; the SDK's `BitmapCanvas::drawSDFPixel`
puts it at **0.5** with 1/16. Copied straight across, the SDK reads MapBox's edge as four texels
*inside* the shape: the hole in `circle-dot` fills in, every icon comes out fat and solid, and the
halo is squeezed to a quarter of its width. `(v - 0.75) * 8 * 16 + 127.5` is the whole fix.

**`text-opacity` fades the halo too — in MapBox.** In CartoCSS it fades only the fill and the halo
keeps `text-halo-opacity`. MapTiler hides a label with `step(zoom, 0, …, 13, 1)`, so the fill went
to 0 and the halo stayed at 1: a solid white ghost of the name at every zoom it should have been
absent from. Both are now emitted from the one MapBox value, and `icon-opacity` likewise reaches
`marker-halo-opacity`.

**A lone `marker-*` declaration builds a marker with no file**, and `MarkersSymbolizer`'s default
fill is `#0000ff`. Emitting `marker-allow-overlap` from `icon-overlap` while the sprite itself had
been dropped put a **blue ellipse on every airport**. Anything `marker-*` belongs inside the block
that emits `marker-file`, never in the property loop.

## One project, one datasource — and a style may use several

A CartoCSS project has ONE datasource; a MapBox style says per layer which tileset it draws from.
Flattened into one project that difference disappears, and every source-layer belonging to another
tileset silently draws nothing. MapTiler topo-v4 uses three vector sources:

| source | source-layers |
|---|---|
| `maptiler_planet_v4` | roads, places, water, buildings … |
| `contours` (contours-v2) | `contour` |
| `landform` | `peak`, `volcano` |

This is why **peaks never appeared** at any zoom — not a filter and not placement: a z13 planet tile
carries no `peak` layer at all. The conversion is right; the wiring is the missing half. The
coverage report now names each extra tileset, its layers and its URL, so the app can point a layer
or a composite slot at each one.

Contours are the worked example: the demo puts a `ContourTileDataSource` over the DEM into the
style's `contour` slot (`--es base composite --es contour true`), so the converted `#contour` rules
draw contours traced live from elevation instead of from MapTiler's contour tileset.

## Contours: `--contour-schema div`

MapTiler's contour layers say "every 5th or 10th line" (`nth_line`); tiles built with the gdal
ladder say "this line is a multiple of N metres" (`div`). **Neither style states the other's unit** -
the base interval is nowhere in the style - so the schemas cannot be mapped exactly, and only the
major/minor split survives:

```sh
massif-style mapbox2css topo-v4.json out/ --contour-schema div --contour-major-div 100
```

```
#contour[zoom >= 10][nth_line ...]   ->   #contour[zoom >= 10][div >= 100]
#contour[zoom >= 11][!nth_line ...]  ->   #contour[zoom >= 11][div < 100]
```

Only the predicates change. Colours, widths and zoom ranges stay whatever the source style asked
for, and every rewrite is counted in the coverage report — MapTiler's 5-vs-10 distinction collapsing
to one threshold is a real loss and is reported as such.

## What could be better

- **No browser build.** `NODERAWFS` rules it out. A `MEMFS` variant would give a web playground that
  compiles a style in the page, which is the natural home for a "does my style still work" check.
- **`carto2css` does not exist.** The reverse direction (mapnik XML back to CartoCSS) has no
  generator; `MapGenerator` only goes one way.
- **Only the size is measured.** Startup time and peak memory for a large style project are still
  unknown; a slow load would push the design towards keeping the module warm across subcommands.
- **A large style project compiles very slowly, then stops compiling at all.** Three MapTiler styles
  compile in well under a second; a 212-layer OpenMapTiles one had not finished after 25 minutes,
  natively as well as under wasm. Streets-v4 (140 attachments) aborts the wasm with `memory access
  out of bounds`, and bisecting the attachment list puts the threshold at 105 — no single rule
  fails, so the cost is in the total. `buildMap` runs `compileLayer` over the full zoom range per
  layer, so something there is super-linear in attachment count. It affects any app loading a large
  style project, not just this tool.
