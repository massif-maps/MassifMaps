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
table with its base-class chain, the handle table, the registry, `set`/`get`, dotted path
traversal, `create`/`destroy` from JSON specs, events with a chosen delivery thread and real map
payloads, per-read and per-subscription projections, `call`/`callAsync` with binary and bulk
results, the flat C ABI, and an idiomatic hand-written sugar layer for Java and Objective-C.**
See [Known gaps](#known-gaps).

## Why a facade at all

Every capability of the SDK is an object you construct and wire by hand. That buys two things worth
keeping — a `TileDataSource` usable *outside* a map, and one source or decoder shared by several
maps — but the features tied hardest to the map (fog, sky, terrain, light) fit it worst, each being
its own options class with its own setters. And nothing reaches the SDK from React Native,
NativeScript or WASM without paying for JNI or a per-class binding.

## Six verbs

```c
int mm_create(mm_ctx, const char* kind, const char* id, const char* json, mm_handle* out);
int mm_destroy(mm_ctx, const char* kind, const char* id);
int mm_set_double(mm_ctx, mm_handle, const char* path, double value);
int mm_get_double(mm_ctx, mm_handle, const char* path, double* value);
int mm_call(mm_ctx, mm_handle, const char* method, const char* args_json, mm_handle* result);
int mm_on(mm_ctx, mm_handle, const char* event, mm_handler, void* ud, const char* opts_json,
          mm_subscription* out);
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

`scripts/gen-api-tables.py` walks `all/modules` and emits
`generated/api/PropertyTable.inc`; `all/native/api/PropertyTable.{h,cpp}` define the structures and
the lookups. Current output for the full profile: **724 properties over 159 classes**, 234 classes
in the chain — 546 with a value accessor, 113 with an object accessor.

Six macro forms carry the declarations, and they do not all mean the same thing — the table records
a value type per row, not just an accessor. Counts below are every declaration in the tree; a build
sees fewer, because modules behind a support define it does not set are skipped:

| macro | count | meaning |
|---|---|---|
| `%attribute` | 387 | a scalar — bool, int, float, `Color`, or an enum constant |
| `%attributeval` | 151 | a by-value struct: `MapRange`, `MapBounds`, `MapPos`, a vector |
| `%attributestring` | 131 | a `std::string` **or** a `shared_ptr` — string vs object reference |
| `!attributestring_polymorphic` | 49 | an object reference, addressed by registry id |
| `%staticattribute` and friends | 6 | static, flagged and otherwise the same |

Resulting distribution for the full profile: `FLOAT` 152, `OBJECT` 114, `INT` 103, `STRUCT` 101,
`BOOL` 85, `STRING` 71, `COLOR` 48, `ENUM` 45, `VARIANT` 5. A `lite` build skips 56 modules and
lands at 613; the default profile skips 45 and lands at 639.

Both tables are emitted sorted, so a lookup is a binary search over static data — no `std::map`, no
allocation, nothing built at load time.

```cpp
const ClassEntry*    cls  = findClass("massif::FogOptions");
const PropertyEntry* prop = findProperty(cls, "rangeStart");
```

### Base classes

A lookup walks the class' base chain, because almost every useful property is declared on a base:
`MemoryCacheTileDataSource` declares none of its own and gets `capacity` from `CacheTileDataSource`.
The generator reads `class X : public Y` from the headers the modules pull in, and emits an entry
for **every** class it sees — with or without properties of its own, or the chain breaks at exactly
the classes that need it. 159 classes declare a property; 234 are in the table.

This was not designed in. It shipped without inheritance, and the first spec-built source on a
device answered `Context::create: demoApiSource.capacity ignored (2)` — `RESULT_UNKNOWN_CLASS`,
because the concrete class was not in the table at all.

### Variants

`Variant` is its own property type, not a struct, because a path does not stop at one — it keeps
walking:

```cpp
ctx->getProperty(payload, "feature.properties.name", value);      // one key, no bag parsed
ctx->getProperty(payload, "feature.properties.tags.1", value);    // a number indexes an array
ctx->getProperty(payload, "feature.properties.nested.a.b", value);
```

A leaf comes back as its natural type — string, integer, double, boolean — and an object or array
comes back as JSON, so a caller can take a subtree whole. A missing key, an out-of-range index, a
non-numeric index into an array, or walking past a leaf are all `UNKNOWN_PROPERTY`.

This is what makes a `hover` handler affordable: reading one property of a clicked feature does not
mean materialising and parsing the whole bag on every event. Writing *into* a Variant is not
supported — it would mean rebuilding the tree.

### Structs

`%attributeval` properties — `MapPos`, `MapRange`, `MapBounds`, `ScreenPos`, `MapVec`, `Variant` —
carry **JSON in the string field**, encoded by `StructCodec`. A position is `[x, y]` or `[x, y, z]`
and a range is `[min, max]`, because that is what an app writes in a spec.

```sh
adb shell am broadcast -a …CONFIG --es apiSet 'zoomRange=[3,17]'
```

Decoding is lenient in exactly one direction: a missing `z` is 0. Everything else — a wrong length,
a non-number, an object where an array belongs — **fails and leaves the property untouched**, so a
malformed spec cannot quietly write a default over a real value. Verified on a device: sending
`zoomRange=nonsense` after `zoomRange=[3,17]` leaves it reading `[3,17]`.

The other 59 struct properties — vectors, maps, `BalloonPopupMargins`, `ClickInfo` — have no
accessor. They are listed in `CODEC_TYPES` when someone needs them; nothing about the mechanism
changes.

### Projections

A `MapPos` carries no coordinate system, so a click position comes back in whatever the map uses —
EPSG:3857 in practice — and every binding then repeats the same `toWgs84`/`fromWgs84` chain. A read
can ask for a projection by name instead:

```java
MassifApi.getPos(payload, "featurePos", "EPSG:4326");   // [5.7606, 45.2442]
```

Three pieces, none of them payload-specific:

- **Which properties are coordinates.** The generator flags `MapPos` and `MapBounds` rows
  `PF_POSITION` (24 rows). A `MapRange` or a `ScreenPos` is not a coordinate and is never converted.
- **What projection the value is already in.** The generator also flags any `OBJECT` property
  pointing at a `Projection` as `PF_PROJECTION` (7 rows) — `Options.baseProjection`,
  `TileDataSource.projection`, `GeoJSONGeometryWriter.sourceProjection`. `findProjectionProperty`
  scans for the flag rather than looking a name up, so a class is free to call it whatever it likes
  and a new one costs nothing. A class that declares none — a click info — carries the projection
  **attached to its handle**; `PayloadEmitter` gives a payload the projection of the target the
  event fires on, which for a map is its base projection.
- **Which projection to convert to.** `Projections::find` maps a well-known name to a `Projection`,
  case-insensitively, with EPSG:3857 and EPSG:4326 built in and `registerProjection` for anything
  else. Names, not objects, because a C or JavaScript caller cannot hold a `Projection`.

A subscription can set a **default** for the reads its handler makes, which is the shape an app
actually wants — ask once, read plainly:

```java
MassifApi.on(handle, "vectortile.clicked", listener, 0, false, "EPSG:4326");
```

It is a `thread_local` set around the handler call (saved and restored, so a handler that emits
another event nests), which means it lives **for the duration of the call only**. A payload kept
and read later falls back to the source projection — so the per-read form is the reliable one, and
the per-subscription one is the convenience. A per-read name always wins over it.

With neither given the value is **left in the source projection**, and so is a value whose source
projection is unknown: a wrong guess is worse than an unconverted number. An unknown projection
name is an error at subscribe time and `RESULT_UNKNOWN_TYPE` at read time, never a silent
pass-through — a typo would otherwise show up as plausible coordinates in the wrong system.

One real edge: Mercator sends the poles to infinity, and `inf` is not JSON. A conversion that comes
out non-finite fails with `RESULT_UNSUPPORTED_TYPE` rather than handing over a string that will not
parse. Reading WGS84 world bounds as EPSG:3857 is exactly that case.

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
these are configuration calls, not a per-frame path. It **carries the type it was stamped with**,
and every direction coerces through `asBool()` / `asLong()` / `asDouble()` / `asString()`.

That is not tidiness, and it took three rounds to get right. Without the stamp each thunk touched
only its own field, so reading a bool as a float returned 0 — indistinguishable from a real 0 — and
writing a bool through `setFloat` wrote `false` whatever you passed; both shipped, and both were
caught on a device rather than in review. Text was the third: `set_string(h, "rangeStart", "3")`
wrote **0**, because `asDouble` never parsed a string, and the reverse wrote an empty string.

A binding whose only type is text — a C caller, a URL query, a scripting language — hits that on
its first call, which is where it was finally found. `asDouble`/`asLong` parse now; `asBool` is
spelled out rather than parsed, because `"false"` is not a number and `strtod` would make it true.
Garbage reads as 0, the same as every other unrepresentable conversion here.

Build values with `PropertyValue::ofBool/ofLong/ofDouble/ofString` rather than assigning a field,
so the type cannot be forgotten.

### What you can do with a handle

A handle from `create` is immediately usable for **properties**, including inherited ones:

```java
int h = MassifApi.create("layer", "base", spec);
MassifApi.setFloat(h, "opacity", 0.25);   // declared on Layer, reached through the base chain
MassifApi.setBool (h, "visible", false);
```

A method is `call` — `loadTile` on a source, `getElevations` on a hillshade layer — and an event is
`on`. See [Calls](#calls) and [Events](#events).

One thing a handle is **not** yet usable for: **replacing an object-valued property**, a layer's
style or a cache's inner source. Writing an `OBJECT` property needs a registry id and a checked
downcast, and only reading one is implemented — which is what traversal needs.

### Dotted paths

A path walks `OBJECT` properties: every segment but the last has to be one, and the walk keeps each
intermediate alive while it continues.

```cpp
ctx->setProperty(mapOptions, "fogOptions.rangeStart", v);   // Options -> FogOptions -> setter
```

Traversal is **derived, not hand-listed**. `Options::getFogOptions()` and its three siblings were
plain getters, so they are now declared with `%attributestring` in `Options.i` and appear in the
table like any other property. That is not a breaking change: `%attributestring` leaves the existing
Java getter and setter in place — verified against `getBackgroundBitmap`, which has been declared
that way all along.

Writing an `OBJECT` property is not supported yet: assigning one means resolving a registry id and
downcasting it, which lands with the spec factories.

The spellings are the mechanical ones — `fogOptions.rangeStart`, not `fog.rangeStart`. Shortening
them is the alias table's job.

## Specs and `create`

`create(kind, id, json)` builds an object and registers it. **A factory only handles what a
constructor needs**; every other key is applied afterwards through the property table, so adding an
option to a class costs nothing here:

```json
{"type":"memory-cache","capacity":33554432,
 "source":{"type":"http","minZoom":0,"maxZoom":19,"url":"https://…/{z}/{x}/{y}.png"}}
```

`capacity` is not a constructor argument — it reaches `CacheTileDataSource::setCapacity` through
the table. A nested `"source"` is an anonymous child built recursively; a **string** there names a
registry entry instead.

Three kinds build today. A `"source"` or `"style"` reference inside a layer spec is either a
registry id or an inline spec of that kind:

| kind | types |
|---|---|
| `source` | `http` `assets` `mbtiles` `memory-cache` `persistent-cache` `ordered` `combined` `multi` |
| `style` | `cartocss` — inline `css`, plus an optional `dir://` asset package |
| `layer` | `raster` `vector` `composite-vector` `hillshade` `solid` |

```json
{"type":"composite-vector","opacity":0.5,
 "source":{"type":"http","minZoom":0,"maxZoom":14,"url":"https://…/{z}/{x}/{y}.mvt"},
 "style":{"type":"cartocss","css":"#water{polygon-fill:#0000ff;}"}}
```

`opacity` is declared on `Layer`, not on `CompositeVectorTileLayer`, so it only applies because
lookups walk the base chain — which is what that section is for.

**The factory table is a registry, not a switch.** `Spec::registerFactory(kind, fn)` is the hook a
plugin would extend, and it is also what makes `create` testable: the tests register a fake kind
whose factory constructs something trivial, so reuse, conflicts and tolerant key application are
covered without linking every source and layer constructor. The built-ins live in a separate
translation unit (`SpecFactories.cpp`) for exactly that reason, and are registered from
`MassifApi` rather than from `Spec` itself.

Two rules, both checked on a device:

- **Parsing is tolerant.** A key the SDK does not know is dropped with a warning, so a spec written
  against another version still applies what it can. There is no `"version"` key.
- **An identical spec reuses.** Creating an id that already exists with the same spec returns the
  existing handle — that is how two maps come to share one source without coordinating. A
  *different* spec under that id is refused, never a silent replace. Comparison is on
  `Variant::toString()`, which sorts object keys, so writing order does not matter.

## The C ABI

`all/native/api/MassifApiC.h` is the flat surface NativeScript, React Native, a WASM build and any
other FFI bind to. No C++ types, no exceptions, no ownership rules beyond the ones the header
states. It compiles as C99, C11 and C17 — checked, not assumed.

```c
mm_ctx ctx = mm_context_default();
mm_handle source;
mm_create(ctx, "source", "osm", "{\"type\":\"http\",\"url\":\"…\"}", &source);

mm_handle tile;
mm_call(ctx, source, "loadTile", "[[8467,5852,14]]", &tile);
size_t size; mm_data_size(ctx, tile, "data", &size);
mm_data_copy(ctx, tile, "data", buffer, size, NULL);
mm_destroy_handle(ctx, tile);
```

**30 entry points, six concepts.** The count grows with *types* — a scalar setter per C type, a
size/copy pair per bulk shape — never with features. A new source type is a spec factory, a new
option a table row, a new event a bridge, a new method a table row. None of them touches this
header, which is what makes an ABI version worth having.

Conventions, each chosen because a binding author would otherwise get it wrong:

- **Everything returns `int`.** 0..99 mirror `massif::api::Result` one for one, kept in step by
  `static_assert` rather than by a translation table someone forgets. 100 and above are ABI-only:
  `MM_BAD_CONTEXT`, `MM_BUFFER_TOO_SMALL`. `mm_result_name` gives a readable name so a binding does
  not keep its own copy of the list.
- **Handles are `uint32_t`**, which is also a JavaScript number — no BigInt anywhere.
- **Every out-param is optional.** A caller that only wants the result code passes null.
- **A null `const char*` is an empty string**, so nothing has to carry `""` just to pass nothing.
- **Two-call buffer protocol** for strings and blobs: ask with a null buffer, allocate, ask again.
  A short buffer is `MM_BUFFER_TOO_SMALL` with the needed size filled in — refused, never
  truncated, and the retry costs one call rather than two.
- **A null `mm_ctx` is `MM_BAD_CONTEXT` everywhere**, not a silent fallback to the default. A
  binding that forgot to fetch it finds out on the first call.
- **`mm_on` takes its options as JSON**, not as parameters — delivery thread, consume, coalesce,
  projection. That is the invariant applied to the ABI's own shape: a new option never changes a
  signature. Each key is read only when present; `getObjectElement` on a missing key returns
  `"null"`, which would otherwise look like a projection nobody has heard of.

Two things that had to change underneath it:

- **`EventHandler` now returns `int`, not `bool`.** It IS `mm_handler` — identical types, so a C
  handler is passed straight through. Casting between them would have been undefined behaviour, and
  wrapping would have meant a trampoline whose lifetime the ABI cannot track, since a subscription
  can also die with its target.
- **`PropertyValue` coerces text both ways.** `mm_set_string(h, "rangeStart", "3")` wrote **0**:
  `asDouble` did not parse a string. A binding whose only type is text — a C caller, a URL query, a
  scripting language — would silently write zero over a real value. `asDouble`/`asLong` parse now,
  `asBool` understands `"false"`/`"no"`/`"0"` (which `strtod` would have made true), and `asString`
  renders a number so the reverse direction stops writing an empty string. Garbage still reads as 0,
  the same as every other unrepresentable conversion here.

**The Release build hid every one of them.** `scripts/android/version-script` exported `Java_*`,
`CSharp_*` and `SWIG*` and nothing else, so the ABI was present in a debug build and gone from the
shipped library. `mm_*` is in the list now. Verified with `llvm-nm --dynamic` on the arm64 `.so` and
`nm -g` on the iOS `.a`: every symbol on both. Watch for the stale-artifact trap while checking —
`build/intermediates/cmake/debug/obj/` holds an old `.so` that reports zero.

## The sugar layer

The two surfaces above are complete and neither is pleasant to write an app against. `MassifApi`
(`all/native/api/MassifApi.h`) is the generated verification surface — a handle on every call, a
result code rather than an exception. On top of it sits a **hand-written, per-language layer**:

| | where | entry point |
|---|---|---|
| Java | `android/java/com/massifmaps/api/` | `MassifMap.attach(mapView)` |
| Objective-C | `ios/objc/api/` | `[MSFMassifMap attach:mapView]` |

```java
MassifMap map = MassifMap.attach(mapView).eventProjection("EPSG:4326");
map.fog().set("rangeStart", 2.5);
map.addLayer("base", Spec.of("vector").set("source", "osm"));
map.onClick(e -> Log.i(TAG, "clicked " + e.position()));
```

**Hand-written, and that is the decision.** It does not move the way the property table does: the
concepts are map lifecycle, camera, layer ordering, registry CRUD, calls and event subscription, and
that list is closed. A new source type, option or event arrives as **data** through the existing
methods and adds nothing. Generating it would buy nothing and cost a generator to maintain.

### What it is for

The complaint it answers is transposing. Before, a click handler read
`MassifApi.getInt(payload, "featureId", -1)` and a JSON string it then parsed. Now it reads
`e.featureId()`, and `e.position()` is a `MapPos` already in the projection the map was told to use.
Four rules make that work:

- **Typed event classes**, one per event, thin views over the payload handle. Reads stay **lazy**:
  nothing is materialised until asked for, so a feature with a long geometry costs nothing unless
  the handler wants the geometry. An event is valid only for the duration of the handler.
- **`Spec` builders with one `set`**, not a named setter per option — a named one would have to grow
  with the SDK, which is the maintenance the facade exists to remove.
- **`PropertyGroup`** scopes a path prefix, so `map.fog().set("rangeStart", …)` is short without 700
  hand-written methods. Named accessors per property stay a non-goal.
- **Subscriptions are the language's own idiom** — `AutoCloseable` in Java, self-invalidating on
  `dealloc` in Objective-C — so removal is not a call an app has to remember.

Everything else delegates. The camera passes through to `MapView`, which already has the flight
code; what the wrapper adds is one `moveTo` that moves position, zoom, rotation and tilt in a
**single** flight, because four separate setters animate independently and visibly fight each other.

### Adopting what an app already built

`registerLayer` / `registerSource` give an object built with the object API an id, and with it
properties, methods and events — so an app moves to the facade a piece at a time rather than
rebuilding its map:

```java
MassifLayer base = map.adoptFirst("base", VectorTileLayer.class);
base.onFeatureClick(e -> …);
```

The **concrete** class is recovered at runtime, so an adopted `VectorTileLayer` answers to a vector
tile layer's properties rather than only to `Layer`'s. Every Swig-wrapped class already registers
its short name in `ClassRegistry` at static-init; the qualified name the table keys on is that plus
`massif::`. The names are interned, because a slot keeps the `const char*` and `GetClassName`
returns by value.

### Swift and Kotlin come for free, mostly

No `.swift` or `.kt` files ship. The Objective-C and Java surfaces are shaped so the modern
languages read well through plain interop:

- An ObjC method returning **void** with a trailing `completion:` block imports into Swift as
  `async` — `-loadTileX:y:zoom:completion:` is `try await source.loadTile(x:y:zoom:)` with no Swift
  code at all. That is why the async form returns void and the generic `callAsync:` (which hands
  back a call id for cancellation) is the separate, advanced one.
- `NS_SWIFT_NAME` drops the `MSF` prefix for Swift only: `MassifMap`, `Spec`, `VectorTileClickEvent`.
- `NS_ASSUME_NONNULL` makes every Swift type non-optional except where nil is real.
- A Java interface with one method is already a Kotlin trailing lambda;
  `suspendCancellableCoroutine` wraps the callback form into a `suspend fun` in about ten lines.

Real shims stay a later slice — see the gaps.

### What the devices found

Verified on an Adreno phone and the iOS simulator. Six things turned up, and only one of them was
visible without running it:

- **A layer had no projection.** Map clicks converted to lon/lat and feature clicks did not, because
  `PayloadEmitter` gives a payload the projection of the target it fires on — and a vector tile
  click fires on the LAYER, which declared none. The fix is an SDK method, not a facade one:
  `TileLayer::getProjection()` returning its data source's, declared `!attributestring_polymorphic`.
  The generator flagged it `PF_PROJECTION` on the next build and **nothing in the facade changed**.
  That is "add the flag, not the special case" paying for itself.
- **Swig's `std::string` typemap rejects a null default.** `getString(handle, path, null)` throws
  `NullPointerException: null string` — which is exactly what a nullable getter wants to pass. Both
  sugars pass a sentinel containing a NUL and map it back, so `e.property("name")` returns null for
  a feature with no name instead of crashing the handler.
- **`dataSource` was already declared on `TileLayer`.** Adding it again passed Swig and failed the
  C++ compile as a duplicate thunk: the generator emits one row per macro occurrence and does not
  deduplicate. The compiler catching it is enough.
- **Java cannot overload on functional interfaces.** `onFeatureClick(Handler)` and
  `onFeatureClick(ConsumingHandler)` compile, but `onFeatureClick(e -> …)` is *ambiguous* against
  them. The consuming one is named `consumeFeatureClick`. (The only one the compiler caught.)
- **ARC freed the Objective-C listener immediately.** The C++ side keeps the director as a raw
  pointer, so with no strong reference on the ObjC side the block was collected the moment
  `subscribe:` returned and the handler *silently never ran* — no crash, no warning, nothing in the
  log. `MSFSubscription` holds it now.
- **Nothing registered a UI dispatcher.** Every handler in this API is documented as main-thread,
  and every one of them was running inline on the GL or tile thread that produced the event, with
  one `Context: no UI dispatcher set, delivering inline` line to say so. The sugar owes the hop and
  now installs it: a `UiDispatcher` director posting to the main `Looper` on Android, and a plain C
  function calling `dispatch_async` on iOS, which needs no director because the sugar is
  Objective-C++. Proof is the thread id in the log — Android moved from `5398 5433` to `5398 5398`,
  iOS from `9889014` to `9919040`.
- **A director module needs `std_string.i` even with no strings in it.**
  `!polymorphic_shared_ptr` generates a `swigGetClassName` returning `std::string`; without the
  typemap it comes back as a pointer and the generated Java does not compile.

What the log looks like once it works:

What the logs look like once it works. Android, `--es apiSugar true`:

```
apiSugar on, map=1048577 fogRangeStart=0.800000011920929 layer=MassifLayer(1048578)
sugar map.clicked at MapPos [x=5.717578, y=45.183765, z=302.503276] type=0
sugar feature 0 layer=landcover at=MapPos [x=5.717461, y=45.183335] name=null geojsonLen=226
```

iOS simulator, `xcrun simctl launch --console-pty <device> com.massifmaps.MassifDemo -apiSugar true`:

```
apiSugar on, map=1048577 fogRangeStart=0.800000 layer=MSFMassifLayer(1048578)
sugar feature 0 layer=landcover at=[5.717181, 45.182109] merc=[636433.637688, 5650236.576489] …
```

Handle `1048577` is generation 1, index 1 — the same on both, since the encoding is the context's,
not the platform's. `at=` is lon/lat because the map was attached with
`eventProjection("EPSG:4326")`, and `merc=` on the same event is a **per-read** projection winning
over the subscription's — both conversions in one handler, which is the rule the host tests assert
and this is it running for real.

`CompletableFuture` is out — minSdk is 21 and it is API 24 — so async is a callback interface, which
is also what Kotlin wraps most cleanly. `MassifApi.isValid` / `mm_valid` exist for the sugar: a
wrapper needs to tell "destroyed" from "never existed" without a property read, which can
legitimately fail for another reason.

**A new Objective-C class needs two lines by hand** — one in `ios/objc/MassifMaps.h` and one in
`build-ios.py`'s `extraHeaders`, which copies it into the framework. The umbrella is not generated.
`extraHeaders` also used to prefix unconditionally, which would have produced `MSFMSFMassif.h`; it
now leaves an already-prefixed name alone.

Both generators picked the new module up **with no change** — a new `.i` directory is found by the
directory walk. Two platform details did need attention:

- **`id` is a keyword in Objective-C.** A parameter called `id` makes SWIG emit `arg1:` selectors
  (`registerOptions:kind:arg1:options:`), so the parameter is named `objectId`.
- **`create` does not attach a layer to a map.** It builds and registers it; the demo adds it with
  the object API through `getLayer`. Attaching needs the map verbs.
- **Element styles and services are not buildable from a spec**, and `destroy` is
  `unregisterObject`.
- **`zip://` asset packages are not supported** — the archive has to be read into `BinaryData`,
  which is platform work. `dir://` works.
- **`ios/objc/MassifMaps.h` is hand-maintained**, not generated, so a new Objective-C class has to
  be added to that umbrella by hand.

```java
int h = MassifApi.registerOptions("options", "demo", mapView.getOptions());
MassifApi.setFloat(h, "fogOptions.rangeStart", 2.5);
```

Both demos drive it, which is how the path is checked on a device:

```sh
adb shell am broadcast -a com.massifmaps.MassifDemo.CONFIG --es apiSet fogOptions.rangeStart=2.5
xcrun simctl launch <device> com.massifmaps.MassifDemo -apiSet fogOptions.rangeStart=2.5
```

The Android demo has a knob per verb, all in `demo/DemoLive.java` — a facade feature nobody can
exercise is unverified:

```sh
--es apiSet fogOptions.rangeStart=2.5              # set/get, dotted
--es apiEvents true                                # subscribe, and log a click payload
--es apiCall 'source:osm:loadTile:[[8467,5852,14]]'          # call, synchronous
--es apiCall 'source:osm:loadTile:[[8467,5852,14]]' --es apiAsync true
--es apiCancel true                                # cancel the last async call
```

The iOS demo has no live-config channel yet ([#154](https://github.com/massif-maps/MassifMaps/issues/154)),
so it takes the same keys as launch arguments only.

Measured on an Android emulator, reading the result code out of logcat:

| path | result | meaning |
|---|---|---|
| `fogOptions.rangeStart=2.5` | `0.8 -> 2.5`, `0` | the dotted write reaches `FogOptions::setRangeStart` |
| `fogOptions.nope` | `3` | unknown property |
| `fieldOfViewY.x` | `7` | not traversable - a dot into a scalar |
| `nosuch.rangeStart` | `3` | unknown intermediate |
| `zoomRange` | `5` | unsupported type — taken **before** the struct codec existed. `MapRange` has an accessor now; see [Structs](#structs) for the device check that replaced this row |

The handle came back as `1048577`, which is generation 1, index 1 — the encoding above, confirmed
end to end. The iOS simulator gives the identical line, handle included:

```
apiSet fogOptions.rangeStart 0.800000 -> 2.500000 (handle=1048577, result=0)
```

## Events

`EventBus` (`all/native/api/EventBus.{h,cpp}`) holds the subscriptions; `Context` owns one and
holds the lock. `on` returns a **subscription handle** with the same 20-bit index / 12-bit
generation encoding as an object handle, so `off` twice is an error rather than a cancellation of
whatever took the slot. Three removals, because all three come up: `unsubscribe` (one),
`unsubscribeEvent` (an event on a target), `unsubscribeAll` (a target).

Four rules, each of which is a bug if it is not there:

- **Subscriptions die with their target**, and so do its pending async calls. `unregisterObject`
  drops both, or the first destroy on an object with a handler is a use-after-free — and a queued
  call would keep the object alive, through the retain it took, long after the app dropped it.
- **Dispatch is in registration order** — *not* slot order. Slots are reused, so index order and
  registration order diverge, and dispatching by index would make which of two consuming handlers
  wins depend on allocation history. Entries carry a sequence number and `collect` sorts by it.
  This shipped wrong and the tests caught it.
- **Dispatch is two-phase.** The handler list cannot be walked unlocked, and handlers cannot run
  under the lock — they are app code, and one that calls back would deadlock a non-recursive
  mutex. So `collect` gathers handles under the lock, and each is resolved again immediately
  before it is called. A handler removed earlier in the same pass therefore fails that resolve and
  is skipped rather than called.
- **A non-consuming subscription cannot stop an event**, whatever its handler returns.

A subscription added *during* a pass is not delivered in that pass.

### Delivery thread

Per subscription: `DELIVERY_ORIGIN` (whatever thread produced the event), `DELIVERY_UI` or
`DELIVERY_BACKGROUND`. The embedder supplies the hop:

```cpp
context->setUiDispatcher(post, userData);   // Java: a main-thread Handler; iOS: the main queue
```

With no dispatcher registered, a queued subscription **runs inline** and says so once — dropping
the event silently would be worse than delivering it on the wrong thread.

**A consuming subscription must be `DELIVERY_ORIGIN`**, and that is rejected at `subscribe` rather
than discovered as a race: the SDK asks whether the event was consumed *now*, and a queued handler
answers later. Waiting for the answer would block the producer on the UI thread.

Only one drain is posted per batch — the drain empties the whole queue, so a second post would
find nothing.

### Coalescing

Per subscription. With it on, an event for a subscription that already has one pending **replaces**
the pending payload instead of adding a second, so a UI-thread handler for a per-frame event cannot
outrun the loop. Off by default, because a discrete event must never be dropped.

### Payload lifetime

A queued payload is **retained** while it waits, so destroying its id does not free it out from
under a handler that has not run yet. The slot is recycled only when the id is gone *and* the last
retain is released — including when the subscription was removed between the emit and the drain,
where the handler is skipped but the payload still has to be released.

`retain`/`release` are on `Context` and are what the C ABI exposes for a handler that wants to keep
a payload beyond its call.

## Event payloads

A payload is a registered object, so **most of a payload costs no payload-specific code**. A
vector-tile click carries a `VectorTileClickInfo`, and its `%attribute` declarations are already in
the generated table:

| path | where it comes from |
|---|---|
| `featureId`, `featureLayerName`, `clickType`, `featurePosIndex` | the generated table |
| `clickPos`, `featureClickPos` | the struct codec, as `[x,y,z]` |
| `feature.geometry.type` | traversal, plus `Geometry::getType` |
| `feature.properties` | reads as JSON |
| `feature.properties.name` | the path keeps walking **inside** the `Variant` |
| `feature.geometryGeoJSON` | `Feature::getGeometryGeoJSON` |
| `featurePos` | `VectorTileClickInfo::getFeaturePos`, MultiPoint-aware |

`Geometry::getType`, `Feature::getGeometryGeoJSON` and `VectorTileClickInfo::getFeaturePos` are
**new SDK methods**, not facade ones. There was no way to tell a
MultiPoint from a Point except a downcast, or — from a scripting binding — matching on the
wrapper's class name, which is what a real NativeScript app was reduced to; and serialising a
geometry meant every binding constructing a `GeoJSONGeometryWriter` itself, differently, and in a
scripting binding slowly; and the position of a clicked MultiPoint had to be rebuilt from
`getFeaturePosIndex` plus a downcast, because `getFeatureClickPos()` documents that it returns the
centre for points. Declared as attributes they appear in the table automatically, and the object
API gains them too.

That is the pattern for the rest: **the derived values a payload needs are SDK gaps, and fixing
them there gives the facade the path for free.**

`clickPos`, `featureClickPos` and `featurePos` are also readable in another projection — see
[Projections](#projections). A payload declares none of its own, so it inherits the one attached to
the event's target.

### Reaching a real map event

`MapEventBridge` turns the map's listener callbacks into facade events. `BaseMapView` has a
**single** listener slot, so the bridge **chains**: whatever the app installed keeps being called
before the event is emitted, or adopting the facade would silently disconnect existing handlers.

```java
int handle = MassifApi.registerOptions("options", "demo", mapView.getOptions());
mapView.setMapEventListener(
    MassifApi.createEventBridge(handle, mapView.getMapEventListener()));

MassifApi.on(handle, "map.clicked", listener, /* delivery */ 0, /* coalesce */ false);
```

The payload is a real registry object for the duration of the emit — registered under a throwaway
id, emitted, then dropped. A queued handler keeps it alive through the normal retain.

Measured on an emulator, tapping the map:

```
apiEvents on, handle=1048577 subscription=1048577
apiEvent map.clicked payload=1048578 clickPos=[5.71750206519,45.18356755721,302.83362844964]
apiEvent map.clicked payload=2097154 ...        <- generation 2, the previous slot reused
apiEvents off, removed=true
                                                <- a tap after off produces nothing
```

Layer-level events work the same way, installed on the layer instead:

```java
vector.setVectorTileEventListener(
    MassifApi.createVectorTileEventBridge(handle, vector.getVectorTileEventListener()));
```

`onVectorTileClicked` returns whether the click was handled, so the results are **OR-ed**: either
the chained listener or a consuming subscriber can claim it, and the facade's answer never
replaces the app's.

Tapping a real map, the payload arrives whole:

```
apiEvent vectortile.clicked id=0 layer=landcover type=2
         pos=[636464.89,5650430.07,0] name=- geojsonLen=226
```

`type=2` is `GEOMETRY_TYPE_POLYGON`, `name=-` is a missing key returning the caller's default, and
the geometry serialised. `pos` is in EPSG:3857 metres — the map's own projection — unless the read
or the subscription asks otherwise; see [Projections](#projections).

Wired so far: `map.clicked`, `map.moved`, `map.idle`, `map.stable`, `map.interaction`,
`vectortile.clicked`, `vectorelement.clicked`.

## Calls

`set`/`get` cover a property. A **method** — `loadTile`, `getElevations` — needs `call`:

```java
int tile = MassifApi.call(source, "loadTile", "[[8467,5852,14]]");
byte[] bytes = MassifApi.getData(tile, "data").getData();
MassifApi.destroy(tile);
```

**A result is always a handle, and the caller owns it.** An object result is that object; anything
else is registered as a `Variant` and read by path — `getFloat(result, "0", 0)` for the first
element of an elevation array, `getString(result, "", "")` for the whole document. One rule instead
of a result struct per binding, and `destroy(handle)` frees it. `destroy` takes a handle rather
than a kind and an id, because a result has no id an app chose; the slot now records both so the
lookup works either way.

**A `Variant` handle is a JSON document.** `lookup` short-circuits on it and reads the rest of the
path inside — including an **empty** path, meaning the document itself. That is the one branch in
`Context` that names a class, and it is what lets a scalar result travel without a result class
being invented for it.

### Where methods come from

The method table is **hand-registered**, not generated, and that is the one place the facade is not
derived from the `.i` files. A property is declared by a Swig macro a script can read; a method is
an ordinary C++ signature, and its arguments have to be decoded from JSON by something that knows
the types. So `Methods::registerMethod(cppClass, name, thunk)` and a thunk per method in
`MethodImpls.cpp`, ~15 lines each:

- Lookup **walks the base chain** from the generated table, so `loadTile` registered on
  `massif::TileDataSource` is callable on every source without being registered again.
- Registering an existing name **replaces** it, which is how a plugin specialises one.
- It is still data — a new method is a table row, not another verb. The ABI does not grow.

`CallArgs` is the JSON array, with one getter per type (`getPos`, `getPositions`, `getTile`, …),
each reporting whether the argument was there and of the right shape. Positional, not named,
because parameters are positional in every language the facade binds to and naming them would mean
maintaining a second name per parameter. An integer reads as a double — JSON has one number type,
and `3` is not a different argument from `3.0` — but a double does not read as an integer.

### callAsync

`loadTile` on an HTTP source blocks the caller. `callAsync` runs it on a worker and delivers the
result **as an event**, so subscribers pick their delivery thread with the machinery that already
exists:

```java
MassifApi.on(source, "loadTile.done", listener, 1, false);   // 1 = UI thread
MassifApi.callAsync(source, "loadTile", "[[8467,5852,14]]", "loadTile.done");
```

- The handle, the method name and the argument JSON are validated **before anything is queued**, so
  a typo is an error the caller sees rather than a log line minutes later. A failure while running
  is a payload of `0`, since the call has returned by then.
- The payload is freed once the handlers have run — the same retain the event queue already takes
  for a click payload — so nothing has to be destroyed by hand. A synchronous result does.
- The target is retained for the duration, or destroying the source mid-call would leave the result
  nowhere to go.
- **One worker thread, not a pool, and calls run in submission order.** The SDK's own
  `CancelableThreadPool` needs `Task::operator()`, which is implemented per platform
  (`android/native/components/Task.cpp`) and does not exist on a host build — using it would have
  taken the test suite with it. Serial execution is also what an app expects of a queue it filled.

### Cancelling

`callAsync` returns a `Call` id; `cancelCall(id)` stops it, `cancelCalls(handle)` stops every one
on an object, and destroying the object does the latter — the same rule subscriptions follow.

**Cancelling stops a call being STARTED and stops its result being DELIVERED. It cannot abort one
already running**, because `loadTile` has no cancellation token to pass on. So a cancelled call in
flight finishes its work and its result is dropped instead of emitted. Either way **no event
fires**: the caller asked for it to stop, and a "failed" payload of 0 would be a lie.

A `Call` is a plain counter, not the handle encoding: ids are never reused, so cancelling one that
already finished is simply not found, and there is nothing to confuse it with. `cancelCall` returns
whether it was queued or running, which is how a caller tells "stopped it" from "too late".

A queued call releases the retain it took on its target when it is cancelled, or the object stays
alive until a call that will never run would have finished.

`Context::call` keeps the typed `PropertyValue` return; `callHandle` is the wrapper both the
bindings and the async path use. The C ABI will want the typed one, to hand a scalar back without
allocating a handle for it.

### Binary and bulk results

Neither of these is allowed to become a string. A tile is a blob and a profile is thousands of
numbers; encoding either one as JSON is the thing the facade exists to avoid.

`getData(handle, path)` reaches a `BinaryData` — `byte[]` in Java, and `mm_data_size`/
`mm_data_copy` in the C ABI. The path is walked with the ordinary object traversal, so
`getData(tile, "data")` works because `TileData.data` is already an `%attributestring` in the `.i`;
an empty path is the handle itself being the blob.

`getDoubles(handle)` reads a bulk numeric result **flat, in one crossing**:

```java
int result = MassifApi.call(layer, "getElevations", "[[[5.76,45.24],[5.77,45.25]]]");
double[] metres = MassifApi.getDoubles(result);
MassifApi.destroy(result);
```

`getElevations` returns a handle onto a `std::vector<double>`, registered under
`Context::DOUBLE_VECTOR_CLASS` — a container, so it has no property-table entry and reading it as a
document is `RESULT_UNKNOWN_CLASS` rather than a coercion.

**The binding is where the work is, and it is a typemap per language**, declared in
`all/modules/api/MassifApi.i`:

| | shape | why |
|---|---|---|
| Java | `double[]` | `SetDoubleArrayRegion`, one JNI crossing |
| Objective-C | `NSData *` | the raw doubles; read with `-bytes` cast to `const double *` |
| C ABI | pointer + count | nothing to copy |

The default would have been the SWIG `DoubleVector`/`MSFDoubleVector` proxy, which is **one call
per element** — 2000 points, 2000 JNI crossings. Two things about the typemap that cost a round:
it has to be declared **after** the `%import`s, because `core/DoubleVector.i` installs its own
`!value_type` typemaps for `std::vector<double>` and the last declaration wins; and the Java
`jni`/`jtype` half applying while `javaout` did not is exactly what that looks like — a `double[]`
native signature with a `new DoubleVector(...)` body.

## Tests

`tests/` is a host-native binary over the parts that link without the renderer — see
[`tests/README.md`](https://github.com/massif-maps/MassifMaps/blob/master/tests/README.md).

```sh
cd tests && ./run.sh
```

**316 checks**, one file per layer:

| file | what it covers |
|---|---|
| `ApiTest.cpp` | table lookups, the base chain, handle generations and the stale-handle rule, `set`/`get` per value type in both directions, path-resolution failures, and `create` — reuse on an identical spec, conflict on a different one, key order not mattering, tolerant unknown keys, and the parse and factory failures |
| `EventTest.cpp` | the three removals, dispatch order, consumption, removal from inside a handler, death-with-target, and delivery — queueing, the single drain post, coalescing, the consume/queued rejection, payload retain across a destroy |
| `StructCodecTest.cpp` | round-trips, and the refusal of every malformed shape |
| `ProjectionTest.cpp` | the name registry, a declared source projection versus an attached one, the per-read argument, the per-subscription default and its expiry when the handler returns, the drain path, the non-finite refusal, and object writes — the subclass check in both directions, an unknown class failing closed, and the wrong kind of object leaving the property alone |
| `MethodTest.cpp` | argument decoding and its refusals, the base-chain lookup, result ownership and `destroy`, the binary and flat-numeric channels, a thunk that throws being caught rather than propagated, an async result arriving as an event and failing as a payload of 0, and cancellation — queued, running, by target, and dying with the target |
| `CAbiTest.cpp` | the two-call buffer protocol, the option JSON, out-params being optional, handle liveness, object writes, a null context refused rather than dereferenced |

Three things keep the link small, and all three are deliberate:

- The property table takes the address of **every** accessor thunk, so a full table needs the full
  SDK. The tests generate a reduced one from an explicit module list (`gen-api-tables.py
  --modules`), which exercises the generator as a side effect.
- The **built-in registrations are a seam**. `registerBuiltins()` is declared in `Builtins.h` and
  defined in `Builtins.cpp` for the SDK — which pulls in every source, layer and method — and
  defined *again* in `CAbiTest.cpp` over three test classes. A separate program, so a second
  definition is not a violation, and it is what lets `create` and `call` be tested at all.
- `Options` drags the renderer, so `Options -> FogOptions` traversal stays a device check.

Writing them moved `create` out of `Context` and into `Spec`: `Context` is the object registry and
should not depend on the JSON layer, which is what made it unlinkable without every source
constructor. The first run then failed on a colour round-trip — `getARGB()` returns `int`, so an
opaque colour sign-extended to a negative `long long`. The read is unsigned now.

## Build wiring

The tables are generated at **build** time and are not checked in, so there is no second step to
remember.

**Configure time does not work here, and the failure is silent.** The obvious wiring —
`execute_process` plus `CMAKE_CONFIGURE_DEPENDS` on every `.i` — looks right and does nothing under
Gradle: AGP decides whether to re-run CMake from its own hash of the configuration, so editing a
module never triggers a reconfigure and the table goes stale. A stale table presents as a property
that silently does not exist. The working form is `add_custom_command` with the modules as
`DEPENDS`, which ninja checks on every build, plus `OBJECT_DEPENDS` on `PropertyTable.cpp`.

**The tables go in the build tree, and cover `all/modules` only.** Two separate lessons, both
from the iOS build rather than from review:

- A shared `generated/api` breaks the second platform to build, because a build's table depends on
  its profile defines. Output is `${CMAKE_CURRENT_BINARY_DIR}/generated/api`.
- The **platform** module sets have to stay out. `ios/modules` pulls in `ios/native/utils/
  BitmapUtils.h`, which is Objective-C, and `PropertyTable.cpp` is a plain C++ translation unit —
  Foundation's headers fail to parse in one. They are glue (bitmap conversion, asset packages), not
  the cross-platform surface the facade is about, so the cost is two Android-only properties.

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

- **No binding uses the C ABI yet.** It is exercised by the host tests; the Java and Objective-C
  sugar goes through `MassifApi` instead, because Swig already generates that. NativeScript and
  React Native are what the ABI is there for.
- **The sugar has no automated tests.** It is Java and Objective-C, which the host ctest suite
  cannot link, so it is covered by the demo knob and a device run — see above for what that caught.
  Everything it calls underneath is tested.
- **iOS was exercised on the simulator, not on hardware**, and through a launch argument rather
  than live, since the iOS demo still has no live-config channel
  ([#154](https://github.com/massif-maps/MassifMaps/issues/154)).
- **`ClassRegistry` does not know the bridge classes.** Creating an event bridge logs
  `Could not find class: N6massif3api14MapEventBridgeE` once per bridge: they are internal and no
  Swig module wraps them, so the polymorphic proxy falls back to the base class - which is all the
  caller wants anyway. Noise, not a fault.
- **No Swift or Kotlin shims.** The interop above covers most of it; `suspend fun`, sealed event
  types and property syntax are a later slice, and Kotlin would add kotlin-stdlib to every Android
  consumer, which is a distribution decision rather than a code one.
- **One context.** `mm_context_default` is the only one; `mm_ctx` is a parameter everywhere so that
  a second isolated world can be added without an ABI break, but nothing creates one. A WASM module
  instance already has its own, since it has its own linear memory.
- **The bulk numeric channel is doubles only.** Positions, colours or integers arriving in bulk
  would each want their own accessor and their own typemap per language. Nothing needs one yet.
- **The method table is hand-registered.** Unlike properties, methods are not declared by a macro
  the generator can read, so each one is a thunk in `MethodImpls.cpp`. Three exist:
  `TileDataSource.loadTile`, `HillshadeRasterTileLayer.getElevation` and `.getElevations`.
- **`callAsync` has no progress, and cancelling cannot abort work already running.** The SDK's load
  paths take no cancellation token, so `cancelCall` prevents a call starting and prevents its
  result being delivered, and that is all it can honestly do. A `loadTile` already in flight
  finishes.
- **The async queue is one thread.** A slow call blocks the ones behind it — the right default for
  a queue an app filled in order, wrong for two independent sources being primed at once.
- **178 of the 724 rows have no value accessor** — every `OBJECT` (114, all of which are readable
  as a traversal step instead) and the 59 `STRUCT` types `StructCodec` does not know: vectors, maps,
  `BalloonPopupMargins`, `ClickInfo`. 65 rows have neither, and `set`/`get` on one returns
  `RESULT_UNSUPPORTED_TYPE`. Adding a struct type is a line in `CODEC_TYPES`.
- **Writing an `OBJECT` property** is unsupported: it needs a registry id and a checked downcast.
  Reading one works, which is what traversal needs.
- **`Options` cannot be linked into a standalone harness** — it pulls the renderer,
  `ElevationManager`, `Bitmap` codecs and more — so the native harness covers `FogOptions` and the
  path-walking failure modes only. The `Options -> FogOptions` happy path is checked on a device
  instead, through the Java binding above. Anything needing `Options` has to be verified that way.
- **`MassifApi` returns result codes, not exceptions**, which is not what the design calls for. It
  is a verification surface and will be replaced by the six verbs and their closed sugar.
- **The 6 static attributes are flagged but have no resolution path**, since a static has no target
  object.
- **No alias table.** Every path is the mechanical spelling; mapbox-familiar aliases
  (`fog-range` for the `rangeStart`/`rangeEnd` pair) are not implemented.
- **One translation unit includes 220 class headers**, which is a heavy compile and couples
  `PropertyTable.cpp` to most of the SDK. Splitting the accessors per module directory is the
  obvious fix if build time becomes a problem; it has not been measured.
