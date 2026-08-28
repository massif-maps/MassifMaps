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
      variables.ts       hoists the literals into the palette (see below)
      config.ts          a style's own `config`/`schema`/`imports` (Mapbox Standard)
      fold.ts            resolves those config reads to constants and evaluates what follows
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

## The palette: `variables.mss`

`mapbox2css` writes three files, not two. `style.mss` holds the rules and is generated — the next
conversion overwrites it. Every colour, font face and shared size comes out into `variables.mss`
instead, as `@name: value;`, and `project.json` lists it **first**:

```json
{ "styles": ["variables.mss", "style.mss"], "layers": ["…"] }
```

That order is load-bearing: `CartoCSSCompiler::buildPropertyLists` keeps the **first** declaration
of a variable it reads, so a stylesheet ahead of the palette wins. A dark or eink version is
therefore a copy of the palette plus a project of its own, and never a fork of the rules:

```json
{ "extends": "./project.json", "styles": ["dark.mss", "style.mss"] }
```

This is the shape the hand-written styles under `scripts/android-dev/.../assets/style` already use:
`eink/style.less` is nothing but `@name: value;`, the rules live in `shared/*.less`, and `eink.json`
lists the palette ahead of them.

**What gets a name.** Every colour and every font face, however few layers use it — a variant that
still has to edit `style.mss` for one colour is not a variant. Numbers only when **shared by two
layers or more**, only for the roles a variant retunes (`size`, `stroke-width`, `halo-radius`,
`spacing`, `opacity`, …) and never when the value only restates the property's own default. Without
those two filters the palette filled with `@airport_labels_placement_priority: 9400000` and
`@polygon_fill_opacity: 1`.

**What it is named.** A font by its face (`@font_roboto_medium`), because what a variant swaps is
the family. Everything else by the layers using it and the property's mapnik role:

| Used by | Name | Example |
|---|---|---|
| one layer | its id + the role | `@glacier_fill`, `@ferry_stroke` |
| layers sharing a prefix | the prefix + the role | `Minor road`, `Minor road bridge` → `@minor_road_stroke` |
| layers sharing one token | that token + the role | four railways and three railway tunnels → `@railway_stroke` |
| nothing in common | the symbolizer + the role | a white halo on 19 label layers → `@labels_halo_fill` |

Collisions take a `_2`, `_3` suffix, most-used first, so the colour a variant most wants to change
gets the bare name. The middle two rules are what earn the palette: naming by prefix alone left a
quarter of topo-v4's entries as `@line_stroke_2 … @line_stroke_9`.

A colour inside a zoom ramp is hoisted; **the ramp is not**. `linear([view::zoom], (8, @contour_stroke),
(16, @contour_stroke_2))` keeps the style's own animation in `style.mss` and puts its two ends in the
palette, which is the granularity a recolour needs. Spelling is not a difference either —
`hsl(0, 0%, 100%)` and `hsl(0,0%,100%)` are one entry, or a variant would recolour half its map.

`--no-variables` turns the whole pass off and leaves every literal inline.

**The check that matters** is that a palette compiles to the *same map*. `css2xml` on topo-v4 with
and without the pass produces byte-identical mapnik XML, and `test/variables.test.js` pins that
against the fixture style.

## A configurable style: Mapbox Standard

Standard is not a style document in the sense the rest of this page assumes. Fetched plainly it is a
**root document with no layers** — one `imports` entry pointing at the basemap fragment and a
`config` block overriding its defaults — and converting that yields nothing. Ask for it the way a
renderer does and the server flattens it:

```
https://api.mapbox.com/styles/v1/mapbox/standard?sdk=js-3.27.0&access_token=…
```

That returns 150 layers, a `schema` of 44 configurable values, and a `lights` block. `mapbox2css`
reports the import-only case by name rather than emitting an empty style.

### The config has to be resolved, not carried

Standard is written against its own config: **878 `["config", …]` reads across its 150 layers**, and
nearly every colour among them goes through one idiom — take the configured colour apart with
`to-hsla`, bind the channels with `let`, adjust them, and rebuild with `hsl`:

```json
["let", "l_colorLand", ["at", 2, ["to-hsla", ["config", "colorLand"]]],
  ["hsl", 20, 20, ["-", ["var", "l_colorLand"], 10]]]
```

CartoCSS has none of `let`, `var`, `at` or `to-hsla`. A style parameter would keep them in the
expression, so the config is **resolved to constants before translation** ([`fold.ts`](../../tools/style-cli/src/mapbox2css/fold.ts)):
substitute, then evaluate as far as the constants reach. That recovered 74 colour properties on its
own and took coverage from 46% to 54%. `--config key=value` overrides any of them.

Folding only removes branches whose test is now **decided**; nothing is reordered or rewritten. That
is load-bearing, not tidiness — it is what makes every preset emit the same rules in the same order,
which is what lets them share one `style.mss`.

### `measure-light`, and where day/night actually lives

`["measure-light", "brightness"]` appears **113 times**, always as a two-stop ramp switching a colour
between a lit and an unlit form (`[0.25, 0.3]` in 66 of them). It is how the style says "night"
without naming the preset, and our renderer has nothing that measures its own light back into a
style value.

The number is taken from the style's **own** `lights` block, which is config-driven like everything
else: the ambient light's lightness times its intensity. That is a proxy, not Mapbox's internal
formula — but it is read from the style rather than invented, and it separates the four presets the
way they are written:

| preset | dawn | day | dusk | night |
|---|---|---|---|---|
| brightness | 0.70 | 0.80 | 0.23 | 0.06 |

Against the style's own 0.25/0.3 thresholds that puts dawn and day on the lit side, dusk and night
on the unlit one. Where a ramp's stops are still per-feature expressions there is nothing to blend,
so the value **snaps to the nearer end** and the coverage report says so.

### The camera terms, which cost 27 whole layers

`road-label`'s filter is

```json
["case", ["<=", ["pitch"], 40], true,
  ["step", ["pitch"], true, 40, ["<", ["distance-from-center"], 1], 55, …]]
```

— true below 40 degrees of pitch, progressively stricter above it. Mapbox thins its labels by where
a tile falls on a pitched screen; nothing feeds a camera angle back into a style here, and our own
label culler does that job.

Left unresolved this is an **untranslatable filter, which drops the whole LAYER** — 27 of them,
every label in the style, while the coverage report still counted their properties as converted.
`pitch`, `distance-from-center` and `line-progress` therefore resolve to what a flat, centred view
sees, the clause folds to `true`, and the filter around it drops it.

Folding is **conservative by construction**: a node is simplified only where a substitution actually
happened. Without that rule it rewrote expressions in styles that have no config at all, and quietly
removed four layers from a MapTiler style whose render was already verified.

### A MapBox zoom is not an SDK zoom

Both span `2^z` tiles, but a MapBox tile is **512** style pixels and the SDK's `tileDrawSize` is
**256**, so the same ground scale is one level higher here:

```
m/CSSpx = WORLD_SIZE / (2^z * tileDrawSize)
```

Measured on the emulator by shifting the camera 0.01 deg and cross-correlating the two frames:
SDK z15 = **3.144** m/CSS px, mapbox-gl z15 = **1.573**. Exactly 2.

So every zoom the style names is shifted a level on the way in — `[view::zoom] - 1` as the input to
every ramp (one constant, `ZOOM_INPUT`), and `+1` on `minzoom`/`maxzoom`. Read straight, a road at
z15 is drawn with z15's width on a z14 view: a `street` comes out 1.74x too wide, while a service
road on a flat ramp moves under a pixel, so the symptom is "only the big roads are too fat".

Going the other way — `tileDrawSize` 512 — fixes the zoom and breaks the widths, because
`normalizedResolution = 2 * tileDrawSize * dpToPX` doubles with it and halves every line. The style
unit as it stands already equals a CSS pixel, DPI-independent, which is what MapBox means by one.

### `line-offset` runs the other way

MapBox offsets a line to the **right** of its direction of travel, mapnik to the **left**, so the
value is negated. Left alone, a cycleway drawn beside its road sits on the wrong side of it. Emitted
as `0 - x`, not `-x`: the grammar has no unary minus before a parenthesised value.

### A casing is not a line: `line-gap-width`

A `*-case` layer draws TWO strips either side of a gap, and **the gap is not drawn**: the gap is the
road the casing runs along, `line-width` is the strip on **one** side.

The SDK draws this itself now (`line-gap-width`, and `line-blur` with it), so the property passes
straight through and one rule stays one rule: the quad is extruded to the outer edge and the middle
is cut in the fragment shader, which costs no extra geometry and keeps the line's own joins and
caps. It replaced a two-rule form that offset one strip per side — 142 rules on Standard against
114 now, and twice the line geometry for those layers.

Its arithmetic is worth stating once, because getting it wrong is invisible on a straight road and
nowhere else: the strip is a **full** `line-width` beyond the **half** gap on each side, so the
outer edge is `gap/2 + width`. Adding the half-gap to the *half* width instead drew every casing at
half thickness, which reads as the road having no outline rather than as a bug.

`line-blur` widens the antialias ramp rather than moving either edge, and its inner ramp fades INTO
the gap — opaque at the gap edge, gone one ramp inside it. Fading the other way eats a whole ramp
off the strip, which at Standard's `line-blur: 10` bridge shadows is a 27-device-pixel bite and
turns a soft shadow into nothing. Dropped entirely, that same shadow is a hard dark bar with a
visible butt cap at each end, which is what "the outlines do not join" turned out to be.

### `*-emissive-strength`, approximated by the colour

Mapbox lights its 2D layers with the same scene lights as its 3D ones. `emissive-strength` is how
much of a colour is EMITTED rather than lit: at 1 it is drawn as authored, at 0 it is at the mercy
of the ambient light and goes dark at night. Standard sets it on 103 properties, and **that**, not
different colours, is how its night preset gets dark.

Our renderer draws every 2D colour as authored — emissive-strength 1 everywhere. So it is folded
into the colour at conversion time:

```
shown = authored x (emissive + (1 - emissive) x lit)
```

`lit` is the preset's brightness over the DEFAULT preset's. That ratio is the load-bearing part:
`sceneBrightness` is an ambient-only proxy whose absolute value is not a light level, but the ratio
between two presets of one style is meaningful — and normalising this way is what keeps the default
preset at factor 1, so a converted day render still matches the style it came from.

Measured on the Paris z15 bench, mean screen brightness went **225/255 by day to 56/255 by night**,
with labels staying bright because Standard marks them fully emissive.

:::caution This is an approximation
No directional term, no per-vertex normal, no colour cast from the light: only lightness moves. It
is applied only where the style STATES an emissive strength — an unstated one is left as authored
rather than assuming a default. A zoom-ramped strength takes the mean of its stops.
:::

:::caution What does not survive
Standard does most of its day/night with the **3D lighting**, not with different colours: 103
`*-emissive-strength` properties, which have no CartoCSS equivalent and are dropped. The converted
night palette is a real recolour — hillshade, ferries, landuse and labels all change — but it is
not the whole difference, and a converted Standard will not look as dark as Mapbox's own.
:::

### One style, four palettes

Every value of `lightPreset` is converted, and all of them are hoisted **together**, so a variable
stands for the same sites in each. The output is one set of rules and a palette per preset:

```
style.mss        the rules, shared
variables.mss    lightPreset = day (the schema's default)
dawn.mss  dusk.mss  night.mss     the same variable names, that preset's values
dawn.json dusk.json night.json    { "extends": "./project.json", "styles": ["night.mss", "style.mss"] }
```

Hoisting each preset separately is what this replaced, and it was subtly wrong: a colour that two
layers share by day and not by night named itself differently in the two files, so the night palette
declared variables `style.mss` never mentioned. Keying each entry on what it is in **every** pass
fixes that by construction. A preset whose rules do not line up is refused and named in the coverage
report rather than half-written — `--no-presets` skips them all.

Its sprite is named `mapbox://sprites/mapbox/standard/<hash>`, which is not a URL anything can
fetch — it resolves to `https://api.mapbox.com/styles/v1/<user>/<style>/sprite`, and the hash is a
cache token the path does not need.

Measured with sprites: **standard 69%** (660/958). Every preset project compiles with `css2xml`.
What is left is mostly genuine: `*-emissive-strength` (approximated, not carried) and
`feature-state` (runtime interaction state, which the SDK has no notion of).

**Its tiles are a separate problem.** Standard reads `mapbox-streets-v8`, whose layer names are not
OpenMapTiles' — `road` against `transportation`, `place_label` against `place`. Pointed at an OMT
source only the layers whose names coincide draw (`water`, `waterway`, `building`, `landuse`), so a
real check needs a Mapbox token with **tiles** scope; a styles-scoped one returns 403 for both
`/v4/<tileset>/{z}/{x}/{y}.vector.pbf` and its TileJSON. Retargeting the schema the way `--schema
openmaptiles` does for MapTiler would lift that, and is not written.

To try it on the bench, convert into a folder on the device and name the preset:

```sh
adb push out/ /sdcard/alpimaps_mbtiles/mbstd
adb shell am start -n com.massifmaps.MassifDemo/.BenchActivity \
  --es style dir --es styleDir mbstd --es lightPreset night
```

`--es lightPreset day|dawn|dusk|night` picks which project of the package `CompiledStyleSet`
compiles, and it is a `DemoLive` key, so `am broadcast … --es lightPreset day` switches it without
a relaunch. The package needs a `fonts/` folder: Standard asks for DIN Pro, which
`assets/style/fonts` already carries.

## Buildings get a switch

Every converted style that draws buildings declares a `buildings` style parameter and gates its
rules on it, in the three states the hand-written styles under `assets/style` already use:

| value | draws |
|---|---|
| `0` | nothing |
| `1` | the footprint only |
| `2` | the footprint and its extrusion (the default) |

```
#building['param::buildings'>0]::footprint { polygon-fill: @building_fill; }
#building['param::buildings'>1]::extrusion { building-height: [height]; }
```

A `fill-extrusion` layer takes `>1`; a plain fill on a source layer named like buildings takes `>0`.
It defaults to `2`, so a converted style keeps drawing what its source drew until an app says
otherwise — and an app that cannot afford the 3D pass on a given device turns it off with one
parameter instead of editing the CartoCSS. A style with no buildings declares nothing.

## A recolourable icon: the glyph is a field, the disc is a plate

Mapbox Standard names its POI and transit icons `["image", <name>, { params: { background,
background-stroke, icon, icon-stroke } }]` and colours those four slots per feature — the disc by
the POI's class, the ring and the glyph by the light preset. The sprite sheet cannot carry that: it
ships **one** flat render per icon, with the icon's own default colours baked in (a grey disc, a
lighter ring, a black glyph).

Two dead ends came first, both of them visible on a device:

- **Declare it an SDF anyway.** `shield-sdf` makes the renderer read the RED channel as signed
  distance, so a blue disc reads as *outside* and a white glyph as *inside*: the icons drew
  inverted, disc gone, and the user reported it as "inversed colors".
- **Draw the flat render as it comes.** Correct shape, but every POI is the sheet's neutral grey
  where the browser draws it orange, blue or pink.

What works is splitting the artwork (`extractIconPlate`). The flats are told apart by their distance
from the transparent surround — the outermost texel row is the ring, everything past it the disc —
and each becomes a different thing:

| artwork | becomes | takes its colour from |
|---|---|---|
| the glyph | a distance field, `shield-file` + `shield-sdf` | `shield-icon-fill` ← the `icon` param |
| the disc | the shield's icon PLATE | `shield-icon-background-fill` ← `background` |
| the ring | that plate's border | `shield-icon-background-border-fill` ← `background-stroke` |

Which colour inside the disc is the *glyph* is the part that took two tries, because MapBox composes
an icon as `icon-stroke` under `icon` and the sheet renders both:

- **Not the one with the most pixels.** An outlined glyph has more outline than fill, so `ⓘ` drew as
  a white ring with the disc showing through the middle — the "no white in the centre" report.
- **The one the other ENCLOSES.** Measured as the mean distance from the disc, because the two are
  parted by an antialiased row and neither actually touches it.
- And a **blend of the disc and the ring is not a flat at all**: it lies on the segment between
  them, which is the test. Counted as one, a 32-texel roundel has more antialiasing than glyph.

How much ink a texel holds is then its position along the **disc → ink axis**, not its nearest flat.
A partly covered texel is a linear blend of the two, so the projection *is* the coverage. Snapping
to the nearest flat instead cost every thin stroke: a bicycle's spokes never reach the ink colour
anywhere, each of their texels is a blend, and on a blue roundel that blend is also a blend of the
disc and the ring — so each read as "not ink" and the wheels drew as a ring of dots.

`icon-stroke` — the outline MapBox draws *under* the glyph — is what the SDK grows from a distance
field, so it becomes the icon **halo** (`shield-icon-halo-fill` / `-radius`, the radius measured off
the artwork), not the plate's border: that one is the disc's ring. Standard sets it transparent
while its POI background is a circle, so it is what a style asking for
`backgroundPointOfInterestLabels: none` gets — a coloured glyph with a white outline and no disc.

The field is cropped to the disc's own box, so the plate needs no padding and its border lands
exactly where the ring was. Its radius is measured rather than assumed: a rounded rect of side `S`
with corner radius `r` covers `S² - (4 - π)r²`, which reads 9 (a circle) off Standard's 20 px POI
icons and 3 (a roundel) off its 16 px transit ones, without fitting an arc to 40 texels.

**Nothing is baked.** The written PNG is greyscale — R=G=B=distance, alpha 255 — and all three
colours are ordinary declarations, so they go through the palette pass and a `lightPreset` swaps
them at runtime over the same files.

Resolution is the other half. A notch one texel wide never reaches full coverage and closes at the
size the icon is drawn, so the sheet is taken at the **densest variant the provider serves** —
`@4x` for MapBox, where a POI icon is 80 texels rather than 40 and a fork keeps the gaps between its
tines. MapTiler stops at `@2x` and the probe just falls through. It costs size: Standard's glyph
fields go from 0.5 MB to 1.3 MB.

The style's own antialias ramp had to be fixed with it, in the SDK — see
[labels](../internals/rendering/06-labels.mdx#the-icon-run-has-its-own-antialias-ramp).

**The params need not cover the whole layer.** Standard's transit label recolours seven networks by
name and lets every other one through to the sheet's artwork, so `paris-metro` keeps its own roundel
— white disc, blue ring, blue M — where the recoloured ones are blue squares. `shield-sdf` is a
`BoolProperty` read with an expression context like everything else, so ONE rule says both: the test
picks the field or the raster for `shield-file`, gates `shield-sdf`, and makes the plate's fill and
border transparent for the features it does not cover (a plate with neither draws nothing).

Two limits, both reported:

- One rule states one plate geometry, so the radius and border are the median of what that rule's
  icons measure. A `match` gives its sprite names as labels even when the branch resolves per
  feature, and those are used in preference to the whole sheet — otherwise the transit rule takes
  the POI circles' radius.
- Artwork that is not a disc with something on it is left out of the lookup entirely: a rule that
  declares `shield-sdf` declares it of *every* file it can name, and a raster cut left in the table
  is read as a field and drawn as a blob. MapBox's generic pin is the one that costs something — a
  plate is a rounded rect and cannot be a pin, so a POI with no icon of its own draws its label
  alone where the browser draws a coloured pin.

## Draw order: a project entry may name one attachment

A bare entry in a project's `layers` pulls **every** attachment of that source-layer, so it places
the whole layer at one depth. MapBox interleaves: Standard draws its pedestrian areas at index 3,
its parks at 4 and its road casings at 60 — two source-layers, and no single depth for `road` works.
Ordering by the median put the pedestrian slab over the Jardin Nelson-Mandela; ordering by the first
index sank all 82 road layers under `landuse`.

`CartoCSSMapLoader` now accepts an entry that names ONE attachment, so a source-layer can be drawn
at several depths:

```json
"layers": ["road::road_pedestrian_polygon_fill", "landuse", "…", "road"]
```

A **bare entry keeps every attachment no other entry claims**, so a project that splits nothing is
unaffected and none of the existing styles change. This is a loader feature, not a converter one —
a hand-written project can use it too.

`mapbox2css` emits one entry per RUN of consecutive attachments, which reproduces MapBox's order
exactly and leaves a layer that interleaves with nothing on its bare entry. Standard goes from
about 35 entries to 127, and six source-layers end up at more than one depth (`building`,
`landuse`, `natural_label`, `place_label`, `road`, `structure`) — the coverage report names them.

Compiling is the expensive step and does not depend on the attachment
([the cascade's cost](../internals/rendering/10-performance.md)), so a layer several entries name is
compiled once.

## Turning a marker on the map: `icon-rotate`

Standard's crosswalks are points carrying a `direction` in degrees, drawn with
`icon-rotate: ["get", "direction"]` and `icon-rotation-alignment: map`. Dropped, every crossing laid
its stripes on the same axis whatever way its street ran.

`marker-transform: rotate(<expression>)` carries it: the angle is an expression, so it is read per
feature, and `MarkersSymbolizer` switches a marker with a rotation to the POINT orientation — flat
on the map, turning with it, which is exactly `icon-rotation-alignment: map`. A layer that rotates
against the VIEWPORT is reported instead; the SDK has no screen-space rotation for a billboard.

**No sign flip.** MapBox's angle is clockwise from north, and `rotate()` is applied in the tile's
own frame where y grows *downward*, so a positive angle already reads clockwise. Negated, every
crossing sat at twice its own angle off its street — which looked plausible enough to ship.

## A marker that overlaps is still a label: `marker-clip`

`MarkersSymbolizer` defaults `clip` to `allow-overlap`, and the two paths it selects between are not
variations of one another: a clipped marker is tile **geometry**, drawn under the stencil tile mask
like a road fill, while an unclipped one is a **label**. The mask is what keeps an over-zoomed
source tile from painting outside the target tile it was handed to, so a glyph whose quad overhangs
the tile edge loses that half — permanently, since the edge is fixed in the world.

That is how Standard's oneway arrows drew: the arrow whose anchor sat 11 cm from the z16 boundary on
Rue du Pont Neuf (lon 2.34492 / lat 48.86112, z20.18, tilt 90) lost the head that overhung north, at
every zoom, and reordering the layer changed nothing because the cover comes from the *mask*, not
from a layer above.

So `marker-clip: false` is emitted with every `marker-allow-overlap`. A MapBox symbol is a symbol
whatever its collision setting; only the SDK's default couples the two. Markers that do not state
overlap already took the label path.

## Which labels a building may hide

`text-occlusion-opacity` and `icon-occlusion-opacity` are carried, and only where the source
**states** them. That is MapBox's own meaning: absent is "occluded by the terrain alone" — the
SDK's default, and free — while a stated value is "occluded by 3D content too". Standard sets `0`
on fifteen layers, all of them line and water names plus the road shields at `0.1`, and says
nothing on `poi-label` or `transit-label`. Converted, its road names go behind a building and its
POI labels stay drawn, which is the granularity the browser has.

Both land on one CartoCSS property, because the SDK's is a `TextSymbolizer` one and a symbol's
icon and text are one label: `text-occlusion-opacity`, renamed with the rest of the rule when the
layer is a shield. `icon-occlusion-opacity` is taken only where the text states none (Standard
states the pair together), and dropped on an icon-only layer — a marker is not a label.

Defaulting them instead of translating only what is stated would turn the occlusion pass on for
every label of every converted style; it costs ~0.85 ms a frame and both scopes above it default to
off. See [labels](../internals/rendering/06-labels.mdx#asking-for-it) for the three scopes.

## What mapbox2css does not carry

Every skipped property is counted and named — `mapbox2css` prints a coverage report, and `--strict`
turns any drop into a non-zero exit. What it refuses, and why:

- **Layer types** with no symbolizer: `heatmap`.
- **The gap list** — `line-blur`, `line-gradient`, `fill-extrusion-pattern`,
  every `*-translate`, most `raster-*` adjustments. These are the CartoCSS gaps, not converter bugs.
- **Expressions with no CartoCSS form**: `feature-state`, `within`, `number-format`,
  `image`, `%` (absent from the grammar), `abs`/`floor`/`ceil` (absent from `_basicFuncMap`), and
  any `interpolate` over something other than zoom or with an exponential base other than 1.
- **A model layer.** Standard plants its trees as glTF models from `mapbox-models-v1`; nothing here
  draws one.

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

## A field in a value reads the feature, and an unset one takes the default

**Corrected 2026-08-27.** This section used to say a property value cannot read a feature field at
all, and `split.ts` exists because of it. It can. The decoder binds the feature before it builds the
processor ([`TileReader::processLayer`](https://github.com/massif-maps/massif-maps-libs/blob/develop/mapnikvt/src/mapnikvt/TileReader.cpp)
calls `exprContext.setFeatureData(symbolizerFeatureData)`), and `Rule::calculateReferencedFields`
gathers the fields a symbolizer property references so they are in that data. Pinned by
`tests/style/DataDrivenPropertyTest.cpp`.

What actually failed was the **unset** field, and the two halves failed differently:

| the value | before | now |
|---|---|---|
| a colour, field absent | `parseColor("")` throws `Color parsing failed`; `TileReader` catches it and caches a **null** processor, so the *geometry* goes with the colour | the property's declared default |
| a width, field absent | silently **0**, which drops the line just as effectively and is harder to see | the property's declared default |
| a malformed non-empty value | throws | still throws — a style bug worth reporting |

`Property::evalExpression` is the one place that decides it: an evaluation yielding **unset** falls
back to the value the property was constructed with, which is what the style would have got had it
never set the property at all. An explicit guard — `[color] <> null ? [color] : '#0000ff'` — still
wins, and is still the clearer thing to write when the fallback is not the default.

Measured on MapTiler topo-v4, cleared cache, z14: two `line-color` declarations reading `[class]`
and `[paved]` produced 3 `Color parsing failed`. Not every feature carries every field, which is why
it looked like "some tiles have no roads, others do".

Worth knowing for anything built on this: a field-driven value folds to a **constant per feature**,
so two features answering alike hand back equal `ColorFunction`s and
[`TileLayerBuilder`](https://github.com/massif-maps/massif-maps-libs/blob/develop/vt/src/vt/TileLayerBuilder.cpp)
dedups them into one of the geometry's 16 style slots. A field-driven colour costs slots, not
batches.

[`split.ts`](https://github.com/massif-maps/MassifMaps/blob/master/tools/style-cli/src/mapbox2css/split.ts)
turns a `case`/`match` over a field into **one attachment per branch**, each with a constant value
and the branch's condition added to the filter. Later branches exclude the earlier ones, because
MapBox takes the first match. On topo-v4 that is 17 layers and **+24 attachments**.

It was written for the wrong reason above and **the decoder no longer requires it** — a field-driven
value renders, and a missing field takes the default instead of losing the feature. It has not been
removed: whether emitting the field expression beats 24 extra attachments is unmeasured, and the
branch cap below is what a review of that should start from.

- A `match` over a plain `["get", f]` uses the **legacy** filter spelling, which lands in brackets
  (`[class = 'motorway']`) instead of a `when()`.
- Past 8 variants a layer is left whole and its field-driven values keep only their **fallback**.
  Now an over-approximation: the decoder would have evaluated the branches per feature.
- A value with no fallback (`["get", "width"]`) is **dropped** with that reason, not emitted. This is
  the one to revisit first — a guarded emission renders, a drop cannot.
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

## Comparing against mapbox-gl: the zoom AND the tile level

Two things are a level apart, not one. The style expressions are shifted (`[view::zoom] - 1`)
because MapBox's tiles are 512 px wide and this SDK's are 256 — that part is handled. What is not
is which tile LEVEL gets fetched: at the same view, mapbox-gl asks for level `floor(z)` and the SDK
for `floor(z)+1`, and a level deeper carries a level's worth of extra POIs. At mapbox z13.67 we drew
bicycle parkings its z13 tile does not even contain, which reads as "the ranking is wrong".

`TileLayer::setZoomLevelBias(-1)` lines the two up — `--es vectorZoomBias -1` in the bench. It is a
property of the SOURCE, not of the style, so the converter cannot emit it.

The bench's readout prints `z=<sdk> (mb <sdk-1>)` and `wasm/mbref.html` prints `z=<mb> (sdk <mb+1>)`,
so a side-by-side is not read a level off.

## A dash is a multiple of the line width

MapBox's `line-dasharray` lengths are scaled by `line-width`; CartoCSS's are pixels, and CartoCSS
takes ONE pattern where MapBox takes a ramp. Two rules follow from that:

- **The pattern is the LAST stop that actually dashes.** MapTiler's disputed border ramps from a
  solid `[1, 0]`, so the base is not the answer; Standard's steps go `[0.2, 0.2]` at z17 then
  `[0.1, 0.1]` at z19, and between two dashing stops the last one has the widest line under it.
  A pattern with no gap at all writes nothing — `[1, 0]` scaled by a width was a 430 px "dash",
  which is a 430 px bitmap rasterized to draw an unbroken line.
- **The width is read AT that stop's zoom**, not averaged over the ramp. Standard's steps ramp to
  80 px by z22, so the mean is 43 px — a width nothing on screen ever has — and the 0.1 dash came
  out at 4.3 px where gl-js draws 1.5. The stairs drew as coarse bands instead of fine treads.

## The light, not the colours: how a preset gets dark

Standard's night preset uses the **same authored colours as day** — `colorLand` is
`hsl(20, 20%, 95%)` in both. What changes is the scene light, and gl-js applies it at draw time. The
SDK draws every 2D colour as authored, so the light is folded into the colour at conversion
([`emissive.ts`](https://github.com/massif-maps/MassifMaps/blob/master/tools/style-cli/src/mapbox2css/emissive.ts)):

```
shown_c = authored_c x (emissive + (1 - emissive) x brightness^(2/3) x cast_c)
```

Three things that each cost a round to get right:

- **Unstated emissive is not "leave it alone".** MapBox's default is **1 for a label and 0 for
  geometry**, so a name stays legible at night and a road does not. Read as fully emissive,
  Standard's `roads-case` — which states nothing — painted a white casing over the whole night map,
  and its trees stayed bright green.
- **The light has a colour.** Night's ambient is `hsl(217, 100%, 11%)` with the directional light at
  intensity 0, so the only light in the scene is blue; with the lightness alone the land came out
  brown. `cast` is this preset's ambient chroma over the DEFAULT preset's — 1 for a white light, so
  the default preset is still exactly as authored — floored at neutral, because a light tints and
  does not subtract.
- **The ambient-only proxy under-reads the light**, and both gammas are fitted to a MEASURED gl-js
  night render (`wasm/mbref.html` over Les Halles, 2.34580 / 48.86300, z16.78):

  | surface | authored | gl-js draws | emissive |
  |---|---|---|---|
  | land | `hsl(20, 20%, 95%)` | rgb 38, 40, 51 | 0 |
  | park | `hsl(115, 60%, 80%)` | rgb 72, 91, 86 | 0.25 |

  Solving both gives a lit term near (0.18, 0.175, 0.27) — luminance 0.18 where the proxy says
  0.075, and a blue lift of 1.5x where the raw chroma ratio says 2.9x. A gamma on each keeps the
  default preset at exactly 1 (`1^g = 1`), which no additive floor can do.

Still an APPROXIMATION: no directional term and no per-vertex normal.

## Where a label breaks: `text-wrap-before`

MapBox puts a word on the current line only if it fits and starts a new line otherwise. CartoCSS
defaults the other way: `TextFormatter::splitLines` measures the word it has just **finished**, so
the line overshoots by one word — and since the test only runs at a wrap character, the **last word
never moves at all**. "10TH ARRONDISSEMENT" and "Strasbourg – Saint-Denis" stayed on one line where
mapbox-gl draws two.

`text-wrap-before: true` is the greedy rule, and it is emitted wherever a non-zero wrap width is
(shields included, where it is renamed with the rest of the rule). The width itself was already
right: `text-max-width` ems × the text size.

## A tree is a canopy dot

Standard draws the whole `tree` source-layer as 3D models from z15, and a dropped `model` layer left
every park a bare green slab. There is no model to port, so the canopy is what is left: a filled
marker in the layer's own `model-color`, ringed by that colour darkened 18 points so two touching
trees still read as two.

- **The size is a ground size.** `exponential(2, …, (15, 2), (20, 64))` — base 2 across five levels
  is the doubling curve, so the dots keep their spacing as the map zooms. A real crown is wider, but
  a flat disc reads heavier than MapBox's textured canopy.
- **`allow-overlap` with `clip` ON**, the one place the two are not emitted together
  ([above](#a-marker-that-overlaps-is-still-a-label-marker-clip)): a tile carries hundreds of trees
  and the label culler must not see them.
- **`random(lo, hi, seed)` folds to the middle of its range.** Standard tints each tree from
  `hsl(random(…), 50, random(…))` around the greenspace colour; CartoCSS has no seed, so the bounds
  are what carries the intent and every tree gets the one green.
- The tiles must carry it: `tree` is in **`mapbox.mapbox-models-v1`**, not in `mapbox-streets-v8`,
  so the bench needs the composite tileset in `--es vectorUrl` or the layer is simply empty.

Every other `model` layer — buildings, wind turbines — has no such reduction and is still dropped.

## Where the sun is, not just how strong

The `lights` block carries a **direction** as well as an intensity — Standard's day preset is
`[180, 20]` — and only the intensities were read. The app's own sun lit the buildings instead: at
the bench's default the roofs measured **86%** of their colour where gl-js draws them at ~100%,
which reads as "the building colour is wrong" and is not. The colours themselves are exact —
`hsl(30, 53%, 93%)` on both sides.

`direction` is `[azimuthal, polar]`: the azimuth runs clockwise from north, as `sun-azimuth` does,
and the polar angle is measured from straight up, so `sun-altitude` is its complement.

**Known limit:** the Map block is written from the default config, and these two are not hoisted
into the per-preset palettes, so switching `lightPreset` to night keeps the day sun. Standard's dawn
is `[120, 50]`, so this is visible.

## MapBox's ground-attenuation is not the SDK's

`fill-extrusion-ambient-occlusion-ground-attenuation` is 0..1 in gl-js, "lower is crisper", default
0.69. The SDK's `building-ao-ground-attenuation` is the **exponent** of `(1 - d)^k` over the distance
from the wall, so a higher value is the crisp one and its own default is 1.75. Carried verbatim,
0.69 landed as a very low exponent: the band spread to the full radius and lost its contrast.

A stated value is now mapped `k = 1.75 × 0.69 / a`, which puts each renderer's default on the
other's; where the style states nothing — Standard's `3d-building` does not — the SDK's own 1.75 is
written instead of gl-js's number.

## A zoom ramp holds past its last stop

MapBox's `interpolate` clamps outside its stop range; cglib's `fcurve`, which
`InterpolateExpression` evaluates through, **extrapolates**. So Standard's POI collision padding —
`["interpolate", ["linear"], ["zoom"], 16, 6, 17, 4]` — reached **-0.36 px at z19** and the culler
stopped thinning anything. Every ramp read past its last stop was wrong the same way, line widths
and opacities included.

`InterpolateExpression::evaluate` now clamps its input to the first and last key, when both are
constants (a computed key cannot be clamped and still extrapolates). That is a **CartoCSS-wide**
change, not a converter one: a hand-written `linear()` holds past its stops too, which is the only
reading a zoom ramp ever had.

## `sizerank` hides an icon, and its plate with it

Standard drives `icon-opacity` from `sizerank` — from z17 a POI with `sizerank < 13` shows its
**name alone**, no icon. `ShieldSymbolizer` faded the glyph and kept the disc behind it, so every
prominent POI drew as a bare coloured dot beside its name ("Forum des Halles" blue, the Jardin
Nelson Mandela green). The plate is the icon's background, so `shield-icon-opacity` scales it and
its border — **per frame**, not baked at decode: the opacity is a zoom ramp, so a tile decoded on
the other side of the step kept its disc until it was decoded again, which is the coloured square
that flashed while zooming.

## Every sprite gets a glyph

The disc/glyph split only accepts a composite, and **433 of Standard's 595 sprites are not one** —
`marker`, MapBox's generic pin, among them, which ~150 POIs name. Left out of the whole-sheet table,
those labels drew with no icon at all.

A sprite that does not split becomes a field of its own **silhouette**, and the rule's own plate
stands in for the disc — which is how MapBox composes a POI icon anyway (`background` + `icon`). It
flattens a multi-colour sprite to one shape, which is what a POI rule's `icon-fill` means.

## What actually has to be split into attachments

Only what names a **resource**: `icon-image` (one file) and `text-font` (one loaded face, resolved
when the symbolizer builds its formatter — an unsplit `match` left the face literally named
`match`). Everything else stays one expression, because a property value that reads a feature field
is evaluated **per feature**: `GenericFunctionProperty::getFunction` rebuilds the function from the
bound context whenever the expression has context variables, and only memoises when it does not.

This net used to be far wider, on the strength of a measurement: two `line-color` declarations
reading `[class]` and `[paved]` produced `Color parsing failed` and lost their whole rule for that
tile. **Re-measured on the same style at the same camera with a cold cache, it is 0** — and a plate
colour reading `[iso_a2]` plus a regex on `[ref]` picks the right country's shield colour per
feature. Which of this converter's later fixes cured it is not established (`InterpolateExpression`
reading string keyframes as colours is the likeliest); what is established is that the observation
justifying the workaround no longer reproduces.

The workaround cost real fidelity, because splitting is a **cartesian product** over every
field-driven property. MapTiler streets-v4's `Road shields` has `icon-color` (28 branches),
`text-color` (27), `icon-halo-color` (10) and `text-font` (2) — 15,120 attachments against a cap of
8, so every one fell back and every road shield drew white. Narrowing it took topo-v4 from 43 branch
attachments to 18 and its "kept only its fallback" count from 8 to **0**.

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

## Room for an icon's halo

MapBox's distance field only describes what its tiny-sdf radius reached — **2 texels outside the
ink** at cutoff 0.25 — and a MapTiler sprite cell is cut tight around its artwork, so the field is
still well above "fully outside" where the bitmap ends. The renderer draws a halo wherever the field
is within the halo width of the edge, so it drew one along the **quad border**: a white rectangle
round every POI icon and a white outline round every tree.

An SDF icon is therefore padded by `SDF_PADDING` texels, with the ramp **continued outward** at the
field's own rate from the nearest border pixel. Filling a constant does not work — that just moves
the same step outward and the halo follows it there.

It grows the quad, and with it the label's collision box, so the padding is kept to what a typical
`icon-halo-width: 2` needs rather than the widest a style could ask for.

## Trying each side: `text-variable-anchor`

MapBox tries each anchor in `text-variable-anchor` until the label fits on one, and falls back to
drawing the icon alone when `text-optional` allows it. `ShieldSymbolizer` does the same from the
same list, so the four properties that describe it map straight across — all of them shield-only,
because a side to place the text on presupposes an icon to place it beside:

| MapBox | CartoCSS |
|---|---|
| `text-variable-anchor: ["right", "left", …]` | `shield-anchors: 'right,left,…'` (corners lose the hyphen) |
| `text-optional: true` | `shield-text-optional: true` |
| `text-radial-offset` | `shield-text-dx` — stated once, MIRRORED onto whichever side wins |
| `text-justify` | `shield-text-horizontal-alignment` (`center` is `middle`; `auto` follows the side) |

No MapTiler style uses a variable anchor today; `text-optional` alone covers 17 layers of
streets-v4 and 25 of outdoor-v4. That is the common case, and it was the one the SDK dropped:
`buildLabelVariants` returned early on an empty anchor list and so never reached the icon-only
fallback, which is why those styles drew no POI icon at all where MapTiler drops the name and keeps
the pin. `text-optional` is now a layout list on its own — the style's own placement, then the icon
alone.

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
- **No image size.** There is no shield equivalent of `marker-width`, so a static `icon-size` is
  resampled into the PNG. A zoom ramp is carried live by `shield-image-scale` instead.

`shield-unlock-image` is always on, and the image is moved clear of the text by half its height —
`text-anchor: bottom` anchors the text's bottom edge, so the name sits above and the dot below it,
which is what MapBox's anchoring produces without ever needing the two to be separate labels.

**A `text-offset` always ships its alignment.** MapBox's offset is a pure translation, but with no
alignment stated `TextSymbolizer::getFormatterOptions` reads a non-zero `dx`/`dy` as the anchor
itself — so MapTiler's `text-offset: [0, 0.05]` hung every road ref off the bottom edge of its
shield. A layer with no `text-anchor` now emits MapBox's default, `middle`/`middle`, beside the
offset.

## Road shields draw the real artwork

A MapBox road shield is a sprite drawn BEHIND its ref, and the sprite is picked per feature —
`concat('transportation:', concat('road_', to-string([ref_length])))`. Two things make that name
reachable, and together they get the country artwork itself rather than an imitation of it:

- **Every sheet is extracted under bare names**, not just the default one — MapTiler keeps its 70
  shield shapes in a separate `transportation` sheet, and a qualified `transportation_road_3.png`
  is a file no interpolation can land on. The default sheet still wins a collision.
- **The name becomes a path**, because mapnik interpolates every `[field]` in a string:
  `icons/[iso_a2]-highway_[ref_length].png`. A **numeric** field interpolates as readily as a string
  one, so `ref_length` reaches `road_5.png`. Where the name branches on several fields at once, each
  branch spells its own path and the case becomes a ternary. The `transportation:` prefix only chose
  a sheet and is dropped.

What is left below is the **fallback**, for a name CartoCSS cannot assemble at all — MapBox's own
shields `slice` the ref for some countries, and there is no `slice` here. The sprite is an SDF
**tinted by `icon-color`** and outlined by `icon-halo-*`, and CartoCSS draws that shape with no
image:

| MapBox | CartoCSS |
|---|---|
| `icon-color` | `text-background-fill` |
| `icon-opacity` | `text-background-opacity` |
| `icon-halo-color` | `text-background-border-fill` |
| `icon-halo-width` | `text-background-border-width` |
| the sprite's rounding | `text-background-radius: 2` |

The artwork is lost in that case; the ref stays readable, and every note says so.

What makes a layer take the plate ([`shield.ts`](https://github.com/massif-maps/MassifMaps/blob/master/tools/style-cli/src/mapbox2css/shield.ts)):
the sprite name reads the **feature**, cannot be spelled as a path, and the text sits **on** the
icon. A POI icon is per-feature too but its text is pushed below it (`text-anchor: top`,
`text-offset: [0, 0.8]`); a town's circle is centred but picked by **zoom**, not by the feature.
Both stay markers. An `icon-color` is required — without a fill there is only a border round
nothing.

Two consequences worth knowing:

- The plate colours are usually a `case` over `iso_a2`/`network` with dozens of branches, so they
  pass the split cap and keep their **fallback** — one plate colour for the world.
- A shield whose `text-field` cannot be translated draws **nothing**. A plate is a background *for*
  text; on its own it is a floating box. MapBox's own shields `slice` the ref per country and
  CartoCSS has no `slice`, so a text-field that branches is retried with its fallback branch —
  `[ref]` for everything outside Brazil — rather than losing the label.

MapTiler streets-v4 draws its shields from 486 extracted icons; topo-v4 has no shields at all.

## Three traps that only show on a device

None of these fail a compile, and each looked like an SDK bug until the style was read.

**`/` between two INTEGERS truncates.** `Expression.cpp`'s `DivOperator` has a `long long` overload,
so a shield's `((1) / 2)` — the default `icon-size` over a 2x sheet — evaluated to **0** and every
icon of thirteen POI layers drew at zero size. The divisor is written `2.0`. CartoCSS numbers look
like JavaScript's and are not; a hand-written style hits this too.

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

**A pattern is a FILE, and a sprite name is not one.** `fill-pattern: "misc:construction_pattern"`
reached the decoder verbatim, and no such file has ever existed — the `misc:` only said which sheet
to look in. Every construction area drew as a bare outline. Patterns now go through the same
extraction a marker's sprite does and come out as `url('icons/construction_pattern.png')`.

**And a pattern has its own opacity.** `polygon-pattern-*` is a different symbolizer from
`polygon-*`, so MapBox's `fill-opacity` sent to `polygon-opacity` faded a solid layer *under* the
hatch and left the hatch itself at full strength — MapTiler's 0.15 construction areas drew
saturated orange. It goes to `polygon-pattern-opacity`, and `fill-color` is dropped, because MapBox
disables it under a pattern.

**A dash ramped over zoom is not a dash the decoder can read.** CartoCSS takes ONE pattern, and the
converter only understood a literal array — so MapTiler's `step(zoom, [1, 1], 22, [1, 1.5])` was
dropped and every footway drew solid. It now takes the first stop that actually *dashes*: the base
is not always it, since the disputed border ramps from `[1, 0]`, which IS a solid line.

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
