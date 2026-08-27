# Tests

The SDK has no test framework and a full build takes an hour, so these are deliberately small:
a host-native binary over the parts that can be linked without the renderer, plus a plain
`TEST_CHECK` macro instead of a dependency.

```sh
cd tests && ./run.sh
```

Needs `cmake`, `python3` and a host compiler. Nothing Android or iOS. `libs-external` has to be
checked out with its nested submodules (`git submodule update --init --recursive libs-external`) —
`rapidjson` and `utf8` are both on the link — and `libs-external/boost` symlinked as in
[`BUILDING.md`](../BUILDING.md).

## What is covered, and what is not

`api/` covers the facade's [property table, handle table and registry](../docs/internals/api-facade.md):
generated-table lookups, the base-class chain, handle generations and the stale-handle rule,
`set`/`get` per value type, and dotted path resolution.

`style/` covers the mapnikvt style MODEL without the renderer: what `resolveLayerConfig` reads out
of a style layer that also carries ordinary line/text rules, which is what a converted MapBox style
produces. Only the TUs the model needs are linked — the symbolizer implementations pull vt's tile
builders in, so a case that needs one belongs behind a device check instead.

`vt/` covers the renderer's HEADER-ONLY maths — today the label plate cell (`LabelPlateBitmap.h`):
what the fill and border shapes in one atlas cell have to satisfy for one quad to draw both. No
object of `vt` is linked; anything reached through a `.cpp` of `vt` drags the tile builders,
freetype and harfbuzz in, and belongs behind a device check instead.

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

`vt/VtTest.cpp` is that `main` for the vt side. It links no source of `vt` at all, so a new case
there has to reach its subject through a header — extracting one is usually the right move, and
adding a `.cpp` is the signal that it is not.
