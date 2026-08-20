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
[issue #146](https://github.com/massif-maps/MassifMaps/issues/146). **Built so far: the property
table, the handle table, the registry and `set`/`get`.** See [Known gaps](#known-gaps).

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
the lookups. Current output for the full profile: **716 properties over 158 classes**, 495 of them with accessors.

Six macro forms carry the declarations, and they do not all mean the same thing — the table records
a value type per row, not just an accessor. Counts below are every declaration in the tree; a build
sees fewer, because modules behind a support define it does not set are skipped:

| macro | count | meaning |
|---|---|---|
| `%attribute` | 386 | a scalar — bool, int, float, `Color`, or an enum constant |
| `%attributeval` | 150 | a by-value struct: `MapRange`, `MapBounds`, `MapPos`, a vector |
| `%attributestring` | 126 | a `std::string` **or** a `shared_ptr` — string vs object reference |
| `!attributestring_polymorphic` | 49 | an object reference, addressed by registry id |
| `%staticattribute` and friends | 6 | static, flagged and otherwise the same |

Resulting distribution for the full profile: `FLOAT` 152, `OBJECT` 110, `STRUCT` 106, `INT` 103,
`BOOL` 82, `STRING` 71, `COLOR` 48, `ENUM` 44. A `lite` build skips 56 modules and lands at 608.

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

## Handles, the registry, and set/get

`Context` (`all/native/api/Context.{h,cpp}`) owns a handle table and the per-kind id registries.
There is one default context — what the static bindings address — but nothing is a raw global, so a
second context is a second isolated world, which is what tests and a WASM module instance want.

A `Handle` is a `uint32_t`: **20 bits of slot index, 12 bits of generation**. 1M live objects, and
4096 reuses of a slot before the generation wraps. It fits a JavaScript number, which is what the C
and WASM bindings need — a 64-bit handle would force `BigInt`.

The generation is the point: destroying an object bumps it, so a handle held across the destroy
resolves to `RESULT_BAD_HANDLE` instead of silently addressing whatever took the slot. Slot 0 is
never handed out, so `NULL_HANDLE` cannot collide with a real object.

```cpp
Handle h;
ctx->registerObject("options", "fog", fogOptions, "massif::FogOptions", h);

PropertyValue v; v.floatValue = 1.25;
ctx->setProperty(h, "rangeStart", v);   // calls FogOptions::setRangeStart
```

`setProperty` calls the class' **own setter** through the generated thunk, so
`notifyOptionChanged("RangeStart")` fires exactly as a direct call would — verified, not assumed.
That is what makes the redraw granularity above true in practice rather than by construction.

`PropertyValue` is deliberately not a union: the `std::string` member makes one impossible, and
these are configuration calls, not a per-frame path.

## Build wiring

The tables are generated at **build** time and are not checked in, so there is no second step to
remember.

**Configure time does not work here, and the failure is silent.** The obvious wiring —
`execute_process` plus `CMAKE_CONFIGURE_DEPENDS` on every `.i` — looks right and does nothing under
Gradle: AGP decides whether to re-run CMake from its own hash of the configuration, so editing a
module never triggers a reconfigure and the table goes stale. A stale table presents as a property
that silently does not exist. The working form is `add_custom_command` with the modules as
`DEPENDS`, which ninja checks on every build, plus `OBJECT_DEPENDS` on `PropertyTable.cpp`.

Two more things that bit, both worth knowing before touching this:

- **The build passes its own `-D_MASSIF_*_SUPPORT` defines, not a profile name.** The thunks must be
  emitted for exactly the classes being compiled — generating for a different profile fails at link
  time, not at generate time.
- **Arguments are comma-separated, not semicolon-separated.** ninja runs the command through a
  shell, which splits an unquoted `;` into a second command; CMake also treats `;` as its own list
  separator.

Run it by hand with:

```sh
cd scripts && python3 gen-api-tables.py
```

## Known gaps

- **`create`, `destroy`, `call` and `on` do not exist.** Specs, factories, events and the C ABI are
  the following slices — plan in [#146](https://github.com/massif-maps/MassifMaps/issues/146).
- **`OBJECT` and `STRUCT` properties have no accessor** (216 of the 716). They need the registry for
  object references and JSON marshalling for structs, so their table rows carry null thunks and
  `set`/`get` return `RESULT_UNSUPPORTED_TYPE`.
- **No dotted paths.** `set(map, "fog.rangeStart", …)` does not work: traversal would follow an
  `OBJECT` property, and those have no accessor yet. Worse, the four option groups are plain getters
  (`Options::getFogOptions()`), not `%attribute` declarations, so even with object accessors they
  need either an alias entry or an `.i` change.
- **No binding.** Nothing reaches this from Java or Obj-C, so the fog A/B on a device has not been
  run; verification so far is a native harness.
- **The 6 static attributes are flagged but have no resolution path**, since a static has no target
  object.
- **No alias table.** Every path is the mechanical spelling; mapbox-familiar aliases
  (`fog-range` for the `rangeStart`/`rangeEnd` pair) are not implemented.
- **One translation unit includes 158 class headers**, which is a heavy compile and couples
  `PropertyTable.cpp` to most of the SDK. Splitting the accessors per module directory is the
  obvious fix if build time becomes a problem; it has not been measured.
