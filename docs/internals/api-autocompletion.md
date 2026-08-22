---
title: Autocompletion for the facade API
description: "How a string-and-JSON API can still complete in Java, Kotlin, ObjC, Swift, TypeScript and C"
sidebar_position: 9
---

# Autocompletion for the facade API

**Status: investigation, nothing built.** The measurements are real; the designs are not
implemented. See [`api-facade.md`](api-facade.md) for the API itself.

## The problem

The facade's whole point is that adding a feature never adds an ABI function
([#146](https://github.com/massif-maps/MassifMaps/issues/146)). It pays for that with strings:

```java
map.options().set("fogOptions.rangeStart", 2.5);
layer.call("getElevations", positions);
Massif.layer("base", Spec.of("vector").set("source", "osm"));
```

Every interesting token there — the kind, the type, the property path, the method name, the event
name, the keys inside a spec — is a string. No editor completes a string, nothing is checked until
it runs, and a typo is a warning in logcat rather than a compile error.

The object API gets completion for free because SWIG emits a typed method per property. That is
exactly the per-platform maintenance the facade exists to remove, so the answer cannot be "go
back". It has to be: **generate the completion, from the same tables the API is already generated
from.**

## What the tables already know

`scripts/gen-api-tables.py` reads the `%attribute*` and `!spec` macros in `all/modules/**/*.i` and
already carries, for the whole SDK:

- every class, and its base chain;
- every property: name, value type (`BOOL INT FLOAT COLOR ENUM STRING OBJECT STRUCT VARIANT`),
  read-only/static/position/projection flags, and for an `OBJECT` the class it points at;
- every spec kind and type, with its constructor parameters, aliases and defaults.

That is most of a type system. Two numbers, measured against the current tables (routing and
search on):

| | |
|---|---|
| classes | 136 |
| direct properties | 670 |
| **distinct property paths, full transitive closure** | **1,169** |
| worst single class (`Options`) | 106 |

The closure converges at depth 3 — 670 → 1,066 → 1,169 → 1,195. **This is small.** The reflex
worry about generating a literal union of every legal path is unfounded: a TypeScript union of
1,200 strings is nothing, and 106 completions on `Options` is a usable list rather than a wall.

## What the schema is still missing

Four gaps, none language-specific. All four have to close before any emitter is worth writing,
because an emitter can only expose what the schema carries.

| Missing | Where it lives now | Cost to fix |
|---|---|---|
| **Methods** — 22 of them | `registerMethod("massif::TileDataSource", "loadTile", &loadTile)` in C++ only. No argument count, no argument types, no return type. | Declare them in the `.i` beside `!spec`, e.g. `!method(loadTile, tile: MapTile) -> object(TileData)`, and have the registry check that what C++ registers matches. Biggest of the four. |
| **Events** — 7 names | String literals in `MapEventBridge.cpp` and again in `MapEvents.java`. | Declare per listener class, with the payload's class so a handler's payload paths can be typed too. |
| **Enum values** | `namespace PanningMode { enum PanningMode { PANNING_MODE_FREE, … } }` in the C++ headers, with doxygen per constant. | The generator already reads headers for the base chain; extend that scan. Cheap. |
| **Doc comments** | Doxygen on the C++ getter/setter. The `.i` attribute macros carry none. | Same header scan. Cheap, and it is what turns completion into *useful* completion — hover text, units, defaults. |

Closing these is worth doing **even if no emitter is ever written**: they are the same "declare it
once, generate the rest" rule the property table already follows, and the method gap in particular
is why `call` is the least discoverable verb in the API.

## One schema, many emitters

```
all/modules/**/*.i  ──┐
all/native/**/*.h   ──┼──> gen-api-schema.py ──> massif-api.json ──┬──> massif.d.ts
(methods, events)   ──┘                                            ├──> Props.java / Spec builders
                                                                   ├──> MassifProperty.h (NS_TYPED_ENUM)
                                                                   ├──> massif_props.h (C enum)
                                                                   └──> the generated reference on the site
```

One JSON, versioned with the SDK, is also what a third-party binding needs — and the docs work
already wanted for the website ([api-docs-generation]) falls out of the same file.

## Per language

### TypeScript — NativeScript, React Native, web

The easy one, and the only one that gets *checking* as well as completion.

```ts
type Handle<C extends ClassName> = number & { readonly __class: C };

// generated: 106 entries for Options, 1,169 across the SDK
type Path<C extends ClassName> = …;
type ValueOf<C extends ClassName, P extends Path<C>> = …;

declare function set<C extends ClassName, P extends Path<C>>(
  h: Handle<C>, path: P, value: ValueOf<C, P>): void;
```

Specs become a discriminated union on `type`, so `{type: "http"}` completes `url`, `maxZoom`,
`encoding` and rejects `cartocss`. Enum-valued properties become string-literal unions. Events
become an overload set keyed on the event name, so the handler's payload is typed.

Nothing is invented at runtime: the JS still calls the same six functions with the same strings.

### Java

Java has no literal types, so completion has to come from real symbols. Two layers, both
generated, useful independently:

1. **Typed keys** — completion *and* value-type safety, with no class explosion:

   ```java
   public static final Key<Float> RANGE_START = key("fogOptions.rangeStart");
   map.options().set(Options.FOG_RANGE_START, 2.5f);   // completes after "Options."
   ```

   `Key<T>` makes `set(Key<Float>, float)` the only overload that compiles, so a colour cannot be
   passed to a float.

2. **Thin typed wrappers** — the full object-API feel, generated:

   ```java
   public final class VectorTileLayerRef extends MassifLayer {
       public VectorTileLayerRef opacity(float v) { set("opacity", v); return this; }
   }
   ```

   Plus a builder per spec type (`HttpSource.url(…).maxZoom(…)`) that just emits the JSON.

**This does not undo the facade.** The rule is that a feature must not add an *ABI* function; a
generated wrapper is per-language source that compiles down to the same six calls. There is no new
native code, no new JNI stub, and nothing hand-maintained — which is precisely what separates it
from the object API it replaces.

### Kotlin

Everything Java gets, plus what Kotlin makes cheap: a `@DslMarker` spec DSL, and extension
properties so `map.fog.rangeStart = 2.5` reads as a field while still going through `set`.

### Objective-C and Swift

The good trick here is `NS_TYPED_ENUM`:

```objc
typedef NSString *MassifProperty NS_TYPED_ENUM;
FOUNDATION_EXPORT MassifProperty const MassifPropertyOpacity;
```

ObjC gets completing constants; **Swift sees a struct with static members**, so it writes
`map.set(.opacity, 0.5)` with completion and type checking, and never sees a raw string. One
generated header serves both languages, which is the cheapest win of the whole set. Typed wrappers
are optional on top.

### C ABI

Generate an enum of property ids alongside the string table:

```c
typedef enum { MASSIF_PROP_OPACITY = 17, … } massif_property;
int massif_set_float(massif_handle h, massif_property p, double v);
```

Completion in any C editor, and it skips the per-call string lookup — so this one is a small
performance win as well. The string form stays for callers that are themselves dynamic.

## Risks and open questions

- **Two ways to say everything.** Once `Props.OPACITY` exists, `"opacity"` still works. That is
  deliberate — the string form is the escape hatch for a newer SDK — but the docs have to say
  which one an app should reach for first.
- **Emitters drift.** Six emitters is six things that can lag the schema. They should be generated
  in CI and the build should fail when the checked-in output differs, the way the property table
  already regenerates on every build.
- **Method declarations are new syntax** in the `.i` files, and the C++ registry has to be checked
  against them or the two will disagree silently — the failure mode this API keeps producing.
- **Wrapper source size** is unmeasured. 136 classes × a few hundred properties of Java is not
  free in dex terms, and the answer may be to emit wrappers only for the classes an app actually
  names.
- **Deep paths in wrappers.** `fogOptions.rangeStart` is natural as a path and awkward as a method
  chain if `FogOptions` has no handle of its own. Reading an object property back as a handle is
  [a known facade gap](api-facade.md) and would want fixing first.

## Recommendation

1. **Close the four schema gaps** — methods, events, enum values, doc comments — and emit
   `massif-api.json`. Useful on its own: it is also what the generated reference on the website
   needs, and it makes `call` discoverable for the first time.
2. **TypeScript first.** Highest value per unit of work, and the bindings that need it
   (NativeScript, React Native, web) are the ones arriving next.
3. **`NS_TYPED_ENUM` next** — one generated header, and Swift and ObjC both stop seeing strings.
4. **Java/Kotlin typed keys**, then wrappers only if the keys prove not to be enough.
5. **C enum ids** whenever the C ABI gets a real consumer.

## Known gaps

- No emitter is written; the schema does not exist yet.
- The `NS_TYPED_ENUM` behaviour is well established but has not been tried in this project.
- Wrapper size and TypeScript compile time at this scale are both unmeasured.
