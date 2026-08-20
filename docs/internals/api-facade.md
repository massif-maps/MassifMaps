---
title: Facade API
description: The six-verb API being built over the SDK's object model — its shape, and the generated property table it rests on.
sidebar_position: 5
---

# The facade API

Scope: the second, opt-in API surface being built over the existing 495-class object model — why it
is six verbs rather than forty, and the generated property table that makes `set`/`get` work without
anything being hand-listed. The object API is unchanged and stays public; this page does not
describe it.

Design discussion and the full plan live in
[issue #146](https://github.com/massif-maps/MassifMaps/issues/146). **Only the property table is
built so far** — see [Known gaps](#known-gaps).

## Why a facade at all

Every capability of the SDK is an object you construct and wire by hand. That buys two things worth
keeping — a `TileDataSource` usable *outside* a map, and one source or decoder shared by several
maps — but the features tied hardest to the map (fog, sky, terrain, light) fit it worst, each being
its own options class with its own setters. And nothing reaches the SDK from React Native,
NativeScript or WASM without paying for JNI or a per-class binding.

## Six verbs

```c
mm_obj create (mm_ctx, const char* kind, const char* id, const char* json);
int    destroy(mm_ctx, const char* kind, const char* id);
int    set    (mm_obj target, const char* path, /* typed value */);
int    get    (mm_obj target, const char* path, /* out */);
int    call   (mm_obj target, const char* method, const char* json, char** out);
int    on     (mm_obj target, const char* event, mm_handler h, const char* optsJson, void* ud);
```

Everything else is **data**: a layer is `create("layer", …)`, a reorder is `set(layer,"index",2)`, a
route is `call(router,"route",…)`, fog is `set(map,"fog.rangeStart",1.2)`.

**The invariant: adding a feature never adds an ABI function.** It adds a row in a generated table
or an entry in a factory. Bindings get a *closed* hand-written sugar layer over the six — roughly 30
methods covering only concepts that will not grow (map lifecycle, camera, layer ordering, registry
CRUD, event subscription). A new source type or option arrives as data through the existing sugar
and never adds a method. Without that rule, hand-written sugar reintroduces exactly the per-platform
maintenance the facade exists to remove.

## The property table

`set` and `get` resolve against a table generated from the **Swig attribute macros**, which already
declare every settable property of every wrapped class. Nothing is hand-listed, so a new setter
becomes a new path on the next build.

`scripts/gen-api-tables.py` walks `all/modules` and `android/modules` and emits
`generated/api/PropertyTable.inc`; `all/native/api/PropertyTable.{h,cpp}` define the structures and
the lookups. Current output: **719 properties over 160 classes, 352 of them read-only.**

Six macro forms carry the declarations, and they do not all mean the same thing — the table records
a value type per row, not just an accessor:

| macro | count | meaning |
|---|---|---|
| `%attribute` | 386 | a scalar — bool, int, float, `Color`, or an enum constant |
| `%attributeval` | 150 | a by-value struct: `MapRange`, `MapBounds`, `MapPos`, a vector |
| `%attributestring` | 126 | a `std::string` **or** a `shared_ptr` — string vs object reference |
| `!attributestring_polymorphic` | 49 | an object reference, addressed by registry id |
| `%staticattribute` and friends | 6 | static, flagged and otherwise the same |

Resulting distribution: `FLOAT` 152, `OBJECT` 110, `STRUCT` 106, `INT` 103, `BOOL` 85, `STRING` 71,
`COLOR` 48, `ENUM` 44.

Both tables are emitted sorted, so a lookup is a binary search over static data — no `std::map`, no
allocation, nothing built at load time.

```cpp
const ClassEntry*    cls  = findClass("massif::FogOptions");
const PropertyEntry* prop = findProperty(cls, "rangeStart");
```

### Path spelling

An attribute's name is decapitalised with the `java.beans.Introspector` rule: an acronym keeps its
case, so `RangeStart` becomes `rangeStart` while `HTTPHeaders` and `TMSScheme` are unchanged.
Predictability matters more than beauty here — readable aliases are a separate table.

### Why this satisfies "the API must not know about style properties"

Three different things get called a style property, and none of them needs API work:

- **CartoCSS `param::` values** are already dynamic — `setStyleParameter(name, value)` takes any
  name. Adding one to a style changes nothing anywhere.
- **CartoCSS paint properties** (`line-color`) live in the style. The facade is a facade over
  CartoCSS, so it never learns them.
- **SDK option properties** (`fog.rangeStart`) come from the macros above, so they appear on the
  next build.

### Redraw granularity comes for free

The invalidation channel is already string-keyed: `Options::notifyOptionChanged("MainLightColor")`
is dispatched per name, with prefix rules for `TerrainOptions*` and `FogOptions*` (see
[the frame](rendering/01-frame.md)). A facade that forwards to the existing setter inherits today's
exact granularity — `set(map,"fog.rangeStart",1.2)` costs what `fogOptions.setRangeStart(1.2)`
costs. No new invalidation design was needed, and none should be added.

## Build wiring

The table is generated at CMake **configure** time rather than checked in, so there is no second
step to remember. `CMAKE_CONFIGURE_DEPENDS` lists every `.i`, so editing a module re-runs the
generator — a stale table would present as a property that silently does not exist, which is an
expensive thing to debug.

Run it by hand with:

```sh
cd scripts && python3 gen-api-tables.py
```

## Known gaps

- **Only the table exists.** The six verbs, the handle table, the registry, specs, events and the C
  ABI are not built. Plan and slices in
  [#146](https://github.com/massif-maps/MassifMaps/issues/146).
- **Accessor names are strings.** The table records `getRangeStart` / `setRangeStart`, not function
  pointers. Binding them to real member functions needs the class headers and belongs with target
  resolution, in the next slice.
- **Nothing calls it yet**, so `--gc-sections` strips the whole translation unit from the shipped
  library. That is correct for now and will stop being true as soon as `set`/`get` land.
- **The 6 static attributes are flagged but have no resolution path**, since a static has no target
  object.
- **No alias table.** Every path is the mechanical spelling; mapbox-familiar aliases
  (`fog-range` for the `rangeStart`/`rangeEnd` pair) are not implemented.
