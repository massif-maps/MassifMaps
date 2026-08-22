---
title: Autocompletion for the facade API
description: "How a string-and-JSON API can still complete in Java, Kotlin, ObjC, Swift, TypeScript and C"
sidebar_position: 9
---

# Autocompletion for the facade API

**Status: built.** The schema, and emitters for TypeScript, Objective-C/Swift, Java/Kotlin and C.
See [`api-facade.md`](api-facade.md) for the API itself.

```sh
# the schema, from the .i files and the C++ headers
python3 scripts/gen-api-tables.py --schema docs/api/massif-api.json
# and the four bindings' completion artefacts
python3 scripts/gen-api-typescript.py
python3 scripts/gen-api-constants.py
```

The schema is committed, so a pull request shows the API surface change as a diff.

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

## The four schema gaps, now closed

None of these was language-specific, and an emitter can only expose what the schema carries.

| Was missing | Fix | Result |
|---|---|---|
| **Methods** — registered in C++ with no signature anywhere | `!method(massif::TileDataSource, loadTile, arg(tile, tile), returns(object, massif::TileData))` in the `.i`, beside `!spec` | **23 declared** |
| **Events** — string literals in the bridge | `!event(massif::Options, map.clicked, payload(massif::MapClickInfo))`, on the class the event fires on | **7 declared** |
| **Enum values** — in the headers, unread | the generator already opens headers for the base chain; the scan now also reads `namespace X { enum X { … } }` | **24 enums, 99 constants** |
| **Doc comments** — doxygen on the C++ accessors | same scan, keyed by class and getter | on every property that has one |

The method and event declarations are checked against the C++ registry at startup
(`Methods::checkDeclarations`, fed by a generated `MethodDecls.inc`). Both directions are
reported: a method registered but undeclared is invisible to every emitter, and one declared but
unregistered completes to a call that fails. This is the same argument as `SPEC_KINDS`, and the
same failure mode this API keeps producing.

## One schema, many emitters

```
all/modules/**/*.i  ──┐                                        ┌──> bindings/typescript/massif.d.ts
  %attribute, !spec,  ├──> gen-api-tables.py ──> docs/api/  ────┤    (gen-api-typescript.py)
  !method, !event     │      --schema         massif-api.json   ├──> ios/objc/api/MassifApiNames.h
all/native/**/*.h   ──┘                                        ├──> android/.../ApiNames.java
  enums, doxygen                                               ├──> all/native/api/massif_api_names.h
                                                               │    (gen-api-constants.py)
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

1. **Typed keys** — completion *and* value-type safety, with no class explosion. This is what is
   generated:

   ```java
   /** Returns where the fog starts. */
   public static final MassifObject.Key<Double> RANGE_START = MassifObject.key("rangeStart");

   map.fog().set(ApiNames.RANGE_START, 2.5);            // completes after "ApiNames."
   map.options().set(ApiNames.RANGE_START.in("fogOptions"), 2.5);
   ```

   `Key<T>` makes `set(Key<Double>, double)` the only overload that compiles, so a boolean cannot
   be passed to a float. 414 of them, each carrying its doxygen.

2. **Thin typed wrappers** — the full object-API feel, generated. **Not built**, on the argument
   that the keys are most of the value for a fraction of the source:

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
`.opacity` with completion and type checking and never a raw string. One generated header serves
both languages, which is the cheapest win of the whole set.

Verified against the iOS simulator SDK, both ways — `MassifProperty.opacity` type-checks and
`.noSuchPropertyExists` does not:

```swift
let opacity: MassifProperty = .opacity
let clicked: MassifEvent = .mapClicked
let raster: MassifSpecType = .layerRaster
```

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

## What the compiler caught that review would not have

The TypeScript output is checked by `tsc --noEmit --strict`, and
[`massif.test.ts`](https://github.com/massif-maps/MassifMaps/blob/master/bindings/typescript/massif.test.ts)
is a type test: every `@ts-expect-error` only compiles when the line under it really *is* an
error, so it fails both ways — if the types stop catching a mistake, and if they start rejecting
something legal. Four bugs came out of it, none of which was visible by reading:

- **`readonly` does not stop `set`.** A `readonly` interface member blocks assignment, not
  `set(handle, path, value)` — the path is just a string. `set` now takes a `WritablePath<C>`
  computed with the standard writable-keys probe. This was the one negative case that passed
  when it should have failed.
- **A spec key can be both a constructor child and a property.** A source's `url`, a popup's
  `description` — declared twice, which is a type error.
- **A subclass may narrow a method.** `VectorTileFeatureCollection.getFeature` returns a
  different handle type from `FeatureCollection.getFeature`; the chain walk has to let the
  nearest class win, exactly as it does for properties.
- **An opaque struct has to satisfy the interface's index signature** — `unknown` does not.

And from the C emitter: there is a property called **`count`**, so the `MASSIF_PROP_COUNT`
sentinel was a redefinition of it. The generator now uses a different prefix for the sentinel and
reports any two names that spell the same symbol.

The Java side was checked the same way, by hand: `set(ApiNames.VISIBLE, 0.5)` fails to compile
with *"no suitable method found for set(Key&lt;Boolean&gt;,double)"*, which is the whole point of
`Key<T>` over a plain `String` constant.

`tests/bindings/run.sh` runs the TypeScript, Objective-C and Swift checks together, each in both
directions — a toolchain that is missing is skipped rather than silently passing.

```
typescript massif.test.ts type-checks                     ok
objc       MassifApiNames.m compiles                      ok
swift      MassifProperty.opacity resolves                ok
swift      an unknown name is rejected                    ok
```

## Known gaps

- **Typed wrappers are not generated.** Only the typed *keys* are, on the argument that they are
  most of the value for a fraction of the source. Whether that holds is unmeasured.
- **The emitters are not wired into CI.** They should run and fail the build when the checked-in
  output differs, the way the property table already regenerates on every build.
- **Property NAMES, not paths, outside TypeScript.** `MassifPropertyOpacity` is one constant for
  every class that has an `opacity`; only TypeScript scopes completion per class.
- **Kotlin gets nothing of its own** — it uses the Java keys. A `@DslMarker` spec builder is the
  obvious next step.
- **The C ids are not wired to a faster path yet.** `massif_property_name` maps an id back to its
  string; nothing yet takes the id directly, so the lookup is not actually skipped.
