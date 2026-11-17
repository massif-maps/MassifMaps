# Tests

The SDK has no test framework and a full build takes an hour, so these are deliberately small:
a host-native binary over the parts that can be linked without the renderer, plus a plain
`TEST_CHECK` macro instead of a dependency.

```sh
cd tests && ./run.sh
```

Needs `cmake`, `python3`, a host compiler and the system zlib and sqlite3. Nothing Android or iOS.
`libs-external` has to be checked out with its nested submodules
(`git submodule update --init --recursive libs-external`) — `rapidjson`, `utf8` and `brotli` are
all on the link — and `libs-external/boost` symlinked as in [`BUILDING.md`](../BUILDING.md).

## What is covered, and what is not

`api/` covers the facade's [property table, handle table and registry](../docs/internals/api-facade.md):
generated-table lookups, the base-class chain, handle generations and the stale-handle rule,
`set`/`get` per value type, and dotted path resolution.

It is also where renderer-free SDK logic is tested (about 80 `*Test.cpp` files: the drape and terrain
LOD maths, camera clearance, caches, day-cycle light, the C ABI...), plus `ProfileGuardTest.py`, which
checks the build profiles' compile guards.

`style/` covers the mapnikvt style MODEL without the renderer: what `resolveLayerConfig` reads out
of a style layer that also carries ordinary line/text rules, which is what a converted MapBox style
produces. Only the TUs the model needs are linked — the symbolizer implementations pull vt's tile
builders in, so a case that needs one belongs behind a device check instead. The MVT decoder is
linked too (it needs only zlib): what an overzoomed tile keeps of a merged MultiPoint. So is the GeoJSON tile builder: the line progress it tags a cut line with, read back through the decoder, and the `line-gradient` ramp a stop list bakes to.

`vt/` covers what of the renderer links without the renderer: the label plate cell
(`LabelPlateBitmap.h`, header-only) — what the fill and border shapes in one atlas cell have to
satisfy for one quad to draw both — line-label layout (`Label.cpp`): that a glyph run stays on the
line it names — and line tesselation (`TileLayerBuilder.cpp` and the six TUs it names): how far out
of its own width a line reaches at a join, and the per-vertex progress a gradient line carries. That last chain wants tess2 and bidi but **no font** —
`TextFormatter` reaches `Font` through its interface only, so freetype and harfbuzz stay out.
Anything reaching GL still belongs behind a device check instead. Label placement over real tiles
is the exception that links more: see [Label scenarios](#label-scenarios).

`archive/` covers the MBTiles and PMTiles sources over small archives each test writes itself:
what they read out of their own metadata. It is a binary of its own because it links sqlite (the
system one) and brotli's decoder, which the `api/` link stays without.

It does **not** cover anything that needs a real map. Two things make that a hard boundary rather
than a choice:

- **`Options` drags the renderer.** Linking it standalone pulls `MapRenderer`, `ElevationManager`,
  the `Bitmap` codecs and more, so `Options -> FogOptions` traversal is checked on a device
  instead, through the demo's `apiSet` knob.
- **The property table takes the address of every accessor thunk**, so a table generated over the
  whole SDK needs the whole SDK to link. The tests generate a **reduced** table from an explicit
  list of `.i` files (`gen-api-tables.py --modules`), which keeps the link small — and exercises
  the generator on the way.

Spec factories (`create`) are in the same position: they reference the constructors of every source
type, so they are device-verified rather than covered here. What is covered is the **schema** those
factories are generated from (`SpecSchemaTest.cpp`): a second, schema-only generator run over the
archive sources, read as JSON, so a class whose `!spec` is missing or whose constructors stopped
resolving fails here rather than in an integration two repos away.

## Label scenarios

`vt/LabelScenarioTest.cpp` runs **real vector tiles** through the style and the label culler, and
checks what the placement decides. The chain is the renderer's, minus GL:

```
tile.pbf -> mapnikvt (LayerTileReader, the style) -> TileLabel -> Label -> LabelCuller::process
```

one pass per camera of a path, the way `VTLabelPlacementWorker` drives it (`culler.reset()`, one
`process`, and another pass while a line run was sent to another spot). It links all of mapnikvt but
the MLT decoder, and **no font**: `ScenarioFontManager.cpp` stands in for `FontManager.cpp` with one
monospace face whose glyphs are boxes, so only relative sizes carry. Between passes the harness
copies what `GLTileRenderer` does: tiles at `floor(zoom)` covering the screen and the label band, one
label per `(layer, id)` merged across tiles, and the placement state (visibility, side, size step)
handed on to what is built again when the tile set changes. Only `FontManager` drags freetype in; with
it stood in for, the symbolizers link here. It does **not** model the fade (a pass
finds the last one faded in), the POI take-over by spot, terrain occlusion or a tilted view.

A scenario is a few tiles and the camera paths that reach one final camera:

```cpp
list.push_back(champagne("name", { zoomIn(to, 15.5, 20), jump(to) }));
```

`jump` is the final camera at once; `zoomIn` many small zoom steps; `travel(from, to, steps)` a pan, a
pinch or both, centre and zoom together; `via(stops, steps)` several in turn (zoom out and back in, a
detour). A path is any list of `Camera`s (centre in tiles of the fixture's zoom, render zoom), each
placed `passes` times, and every path of a scenario ends on the same camera. `watch` names the labels
whose state is printed per path. Every scenario is checked for four invariants, and prints a score per
path (the visible names weighted by priority, an icon alone a quarter) to compare policies by a number:

| Invariant | Holds when |
|---|---|
| path independence | every path ends on the same visible set, POI sides and line-run size steps |
| settling | placing again at the final camera flips no visibility, side or size step |
| a POI keeps its text | no POI is left without its text by a street name on a side it could have taken |
| no overlap | no two placed labels overlap, in the boxes the culler tests |

An invariant the culler breaks today goes in the scenario's `knownFailures`: it prints `XFAIL`, and
the run **fails once it holds**, so the mark cannot outlive the fix. `LABEL_SCENARIO_VERBOSE=1`
prints every visible label of every path.

To **see** it, `LABEL_SCENARIO_HTML=/tmp/pages /tmp/massif-tests-build/vt/vt_tests` (the directory must
exist) writes one page per scenario: the final frame of every path side by side, as the culler placed it —
the screen, the band labels are placed in around it, every box it tested, the names squeezed into them.
What a path does differently from the first is red, dashed where it hid a label the first showed, and
the `watch`ed names are yellow. Open the page in any browser; no SDK build is involved.

Adding a case is a tile and a few lines:

1. Crop the tile to what the case needs, to a few KB: `label-scenarios/crop_tile.py in.pbf
   label-scenarios/tiles/<zoom>-<x>-<y>.pbf x0 y0 x1 y1` (window in the tile's extent units; keeps
   `poi` and `transportation_name` unless layers are named).
2. Add the scenario in `scenarios()`. The camera is in tiles of the fixture's zoom: tile
   `8410 + 350 / 4096` is unit 350 of tile 8410.
3. A layer the style does not draw needs a rule in `label-scenarios/style/scenario.mss` (values copied
   from `styles/massif/carto/style.mss`), compiled into the `scenario.xml` the host reads, since the
   CartoCSS compiler is not linked here:
   `massif-style css2xml label-scenarios/style/project.json label-scenarios/style/scenario.xml`
   (built by `libs-massif/cartocss/util`, see below).

## Bindings and CI

`bindings/run.sh` is a separate compile-only check of the generated Swift and TypeScript names; it is
not part of `ctest`. CI runs `tests/run.sh` and the binding drift check (`.github/workflows/tests.yml`)
on a PR carrying the `run-tests` label, or by hand; a release runs them too.

## The style XML round-trip — outside this suite

Whether the Mapnik XML parser can read back everything `css2xml` writes is checked by `css2xml`
itself, not here: the parser needs the symbolizers, and those drag `vt`, freetype and harfbuzz in —
the weight `style/` stays under by linking the style model alone.

```sh
cmake -S libs-massif/cartocss/util -B build/css2xml && cmake --build build/css2xml --target css2xml
build/css2xml/css2xml --roundtrip libs-massif/cartocss/util/fixtures/roundtrip/project.json /tmp/out.xml
```

It compiles the fixture, parses the result back, compiles that, and fails on any diff — a
symbolizer the parser has no case for, an operator missing from the expression grammar, a `Map`
setting only one side knows. Nothing runs it automatically: **run it when you touch mapnikvt's
parser, generator or expression grammar**, and add the construct to
`fixtures/roundtrip/style.mss` when you add one.

## Adding a case

`api/ApiTest.cpp` is one `main` with `TEST_CHECK(condition, "what it means")`. A failure prints the
line and the binary exits non-zero, which is all `ctest` needs. If a new case needs another class,
add its `.i` to `TEST_MODULES` in `api/CMakeLists.txt` and its `.cpp` to `TEST_SDK_SOURCES` — if
that turns into a long list, the class probably belongs behind a device check instead.

`style/StyleTest.cpp` is the same `main` for the style side; a new case is a function there plus
its `.cpp` in `style/CMakeLists.txt`. Keep `STYLE_SOURCES` short for the same reason.

`vt/VtTest.cpp` is that `main` for the vt side. It links `Label.cpp` and the tile-builder chain, so
a new case has to reach its subject through a header or through those objects — adding another
`.cpp` is the signal to check what it pulls first, because the font stack and GL are one include
away and neither belongs here. The label scenarios are the one place that links all of mapnikvt, and
they stay clear of the font stack by the stand-in `FontManager`.
