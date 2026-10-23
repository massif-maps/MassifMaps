---
title: Native modules
description: Valhalla as its own native library an app can include or drop, why only Valhalla, and the three boundaries that were compared.
sidebar_position: 3
---

# Native modules: Valhalla as a library an app can drop

Question: can an Android app include or drop Valhalla, offline routing (`sgre`/`osrm`) and geocoding,
instead of choosing one `libmassif.so` per profile, the way `libsqlite.so` is split out
([build-and-size](build-and-size.md#sqlite-as-its-own-libsqliteso-android))?

**Answer, built:** Valhalla yes, on Android, through a small C ABI and `libvalhalla_routing.so`. Not
geocoding, `sgre` or `osrm` (0.4 MB between them). The comparison and the numbers behind that are
below; what is implemented is under [What is built](#what-is-built).

Numbers are arm64-v8a Release, NDK 27.3, `full` profile.

## What is movable

Symbol attribution of a relinked, unstripped `libmassif.so` (linker map, ThinLTO so the first symbol of
each input section names it; `.text`, `.rodata`, `.data.rel.ro` only, unwind tables are not attributed):

| piece | bytes |
|---|---|
| Valhalla code (`baldr` `loki` `thor` `odin` `sif` `meili` `midgard` `tyr` `skadi`) | 2,444,191 |
| Valhalla protobuf messages + protobuf runtime | 299,890 |
| **Valhalla stack** | **2,744,081** |
| geocoding engine + JNI wrappers + directors | 299,290 |
| `sgre` + `osrm` | 82,852 |
| routing adapters + JNI wrappers + directors | 136,125 |
| package-manager routing/geocoding handlers | 56,168 |

That is ~3.3 MB, and `full` minus `standard` is 3.42 MB. **Valhalla is 80% of it**; the rest is a few
hundred KB spread over four small pieces, each with its own SWIG wrappers.

## Where the coupling is

- **Core to modules is thin.** Outside `routing/`, `geocoding/` and the SWIG wrappers, only
  `api/RoutingMethods.cpp`, `api/GeocodingMethods.cpp`, `api/SpecFactories.cpp` and
  `packagemanager/handlers/PackageHandlerFactory.cpp` name a module class, all behind `_MASSIF_*_SUPPORT`.
- **Modules to core is ~100 symbols.** Compiling `all/native/{routing,geocoding}` and the three
  `libs-massif` engines without LTO leaves 98 undefined `massif::` symbols: `Variant` 16, `Address` 14,
  `MapPos` 13, `MapVec` 5, `EPSG3857` 5, `NetworkUtils` 4, GeoJSON reader/writer 7, `Feature*` 6,
  `PackageManager` 3, `HTTPClient` 3, `Log` 3, `Projection` 2, plus the vtable and typeinfo of `Address` and
  three package handlers. The 33 routing/geocoding SWIG wrappers add only 6 more symbols (`ClassRegistry`,
  `Variant`, `MapPos`, `Log`, `Address`): each wrapper is self-contained.
- **The engines are leaves.** `libs-massif/{geocoding,sgre,osrm}` use `cglib`, `stdext`, `picojson` and
  `sqlite3pp` only, in their own namespaces. The core types appear only in the adapters.
- **Valhalla touches core at one point.** `ValhallaRoutingProxy` serializes a `RoutingRequest` to JSON,
  runs `loki`, `thor`, `odin` and `tyr::serializeDirections` (or `trace_attributes`), and parses the JSON
  back. Everything before and after is core code; the Valhalla part takes JSON and returns JSON.
- **`routing-lib` already was that engine.** `routing::ValhallaRoutingProxy::CallRaw(databases, config,
  endpoint, json) -> json` runs the same `route` / `trace_attributes` pipeline as the core's in-process
  code (same worker calls, read side by side). Two forks of one file, and an iOS clash: both ship
  `MSFRoutingRequest`, `MSFRouteMatchingRequest`, `MSFValhallaOnlineRoutingService`.
- **`valhalla-online` needs no engine**: HTTP in, JSON out, parsed in core.
- **SWIG directors.** Java may subclass `RoutingService` and `GeocodingService`, so a C++ boundary has to
  share their vtables.

One constraint decides the direction: the `routing` AAR is meant to work **without the map**, so
`libvalhalla_routing.so` cannot depend on `libmassif.so`. Core calls the module, never the reverse, and
core must not carry a `DT_NEEDED` on it or a dropped file fails `System.loadLibrary("massif")`.

## The options compared

| | (a) C ABI at the engine | (b) per-module SWIG + C++ surface | (c) one Valhalla, core calls it |
|---|---|---|---|
| boundary | `vr_*` C functions, JSON strings | C++ classes across `.so` | (a), Valhalla only, `routing-lib` as engine |
| exported from core | nothing | ~100 symbols + interface classes (`RoutingService` vtable/typeinfo...) | nothing |
| exported from module | ~5 `vr_*` functions | its classes + wrappers | ~5 `vr_*` functions |
| classes across | none (`const char*`, `int`) | `Variant`, `MapPos`, `Projection`, `RoutingRequest/Result`, `PackageManager` | none |
| SWIG / JNI | wrappers stay in core, Java API unchanged | wrapper list split per library, `loadLibrary` per module in the generated `ModuleJNI` | as (a) |
| exceptions, RTTI, STL | none cross; module may stay `c++_static` | needs one `libc++_shared`; `catch` and `dynamic_cast` of hidden-visibility types across libraries | as (a) |
| iOS | static archive, one library | same plus ObjC proxies split by module | as (a) |

(b) re-exports ~100 core symbols and the interface classes the module subclasses and the facade calls,
each with vtable and typeinfo, from a version script that exports `Java_*`, `mm_*`, `sqlite3_*` only;
exporting `massif::*` wholesale would multiply the 3138 dynsym entries, so each class needs an explicit
export macro that nothing enforces. It ties a module build to a core build, and its only gain over (a) is
that the typed Java classes live in the module, which are tiny. Rejected.

(c) is (a) restricted to Valhalla, with `routing-lib`'s `CallRaw` as the engine. That is what was built.

## What is built

**The boundary.** `routing-lib/native/engine/valhalla_engine.h`, five C functions: `vr_abi_version`,
`vr_default_config`, `vr_add_locale`, `vr_call(paths, config, endpoint, body) -> json | error`, `vr_free`.
`vr_call` takes MBTiles **paths**, not open handles: `GraphReader` wants `shared_ptr<sqlite3pp::database>`,
and `sqlite3pp` cannot wrap a raw `sqlite3*`, so handles would tie both libraries to one `sqlite3pp` ABI.
The engine opens a read-only connection per file and shares it between concurrent calls; the last call to
finish closes it. `vr_*` is in `scripts/android/version-script`.

**The engine library.** `libvalhalla_routing.so` is the only Valhalla. Its Kotlin and ObjC wrappers in
`routing-lib` call the same `vr_call` as the core does, so there is one path to the database. It links the
shared `libsqlite.so` ([#450](build-and-size.md#sqlite-as-its-own-libsqliteso-android)) instead of
compiling its own.

**The core.** `ValhallaEngine` (`all/native/routing/utils/`) is the client: on Android it
`dlopen`s `libvalhalla_routing.so` at the first offline Valhalla call and checks the ABI version; there is
no `DT_NEEDED`. `ValhallaRoutingProxy` keeps request serialization and result parsing and passes JSON to
the client. `valhalla` and `protobuf` are no longer compiled into `libmassif.so` on Android. The three
offline services pass package file names instead of `sqlite3pp` handles. An absent engine throws
`GenericException: Valhalla engine not found, ship libvalhalla_routing.so with the app` at the first
offline call (the default configuration comes from the engine, so at service construction); online
Valhalla never touches it.

**Everywhere else** (iOS, desktop) the same `routing-lib` engine sources are compiled into `libmassif`
and `vr_*` is called directly, so there is one Valhalla source tree and no run-time loading. On iOS the
static linking is the "drop" switch: `MassifMapsCore` and `MassifMapsLite` have no Valhalla.

**Packaging.** `build-android.py` builds the engine with `c++_shared` for every profile that defines
`_MASSIF_VALHALLA_ROUTING_SUPPORT` and puts it next to `libmassif.so` in `dist/android/<abi>`, so the
profile's AAR carries it. An app that does not route offline drops it:

```groovy
android { packaging { jniLibs { excludes += "**/libvalhalla_routing.so" } } }
```

The standalone `routing` AAR is the same code with `c++_static`, for an app without a map. Next to the
`full` AAR both carry `libvalhalla_routing.so` and `libsqlite.so`, so Gradle needs `pickFirst` on them
(as it already does for `libsqlite.so`).

## Measured

arm64-v8a Release, `full` profile, same `libs-external`, same machine, `origin/master` against this change:

| | before | after |
|---|---|---|
| `libmassif.so` | 12,300,936 | **9,952,000** (−2,348,936, −19.1%) |
| `libvalhalla_routing.so`, `c++_shared`, packed relocations (in the `full` AAR) | — | 2,609,872 |
| `libvalhalla_routing.so`, `c++_static` (standalone `routing` AAR) | 4,423,072 (own SQLite) | 3,238,984 |
| `libsqlite.so` | 601,336 | 601,336 |

- An app that drops the engine saves **2.35 MB** of native code per ABI.
- An app that keeps it pays **+0.26 MB** over the old monolith (12,561,872 against 12,300,936). By linker-map
  output section against the same tree, the extra is `.text` +147 KB, `.eh_frame` +45 KB, `.rodata` +15 KB (a
  second library's own copy of inline and template code, unwind tables and no cross-unit ICF) and the
  engine's relocations, which were another +99 KB until `--pack-dyn-relocs` (API 23, so only for the AAR
  flavor). `zstd` is not in the engine at all, so there is no shared library to carve out of it; the
  duplicated part is header-instantiated code (`std`, `boost`, `rapidjson`), which cannot be exported.
- An app that carried both the `full` and the `routing` AAR used to ship Valhalla, SQLite and libc++ twice
  (~4.4 MB); it now ships the engine once.
- `libmassif.so` has no `NEEDED libvalhalla_routing.so`; `libvalhalla_routing.so` exports `vr_*` and the
  JNI symbols, and `NEEDED libsqlite.so`, not `libmassif.so`.

## Not verified

- **No route was computed.** There was no Valhalla MBTiles package on any machine here. Checked on an
  arm64 emulator: the engine library loads, `vr_abi_version` and `vr_default_config` answer, `vr_call`
  returns clean errors for no paths, a missing file and an unknown endpoint, and `ValhallaEngine` (compiled
  on its own with the NDK) loads the library when present and throws the "not found" error when not. Core
  to engine to a real `GraphReader` over a package is untested.
- **The config now goes through a different JSON to ptree path.** The core used `rapidjson::read_json`;
  the engine uses `routing-lib`'s picojson converter. Numbers are written back by `ptree` rather than kept
  as text. Same Valhalla calls, not compared on a real route.
- **The `libmassif.so` to engine call inside an app.** A native executable cannot load `libmassif.so`
  (`libandroid_runtime`), and an `app_process` run is shadowed by `libsqlite.so`. Needs an app run.
- **Per-call cost** of opening MBTiles by path against the package handler's old long-lived connection.
- **`dlopen` by bare name** with `extractNativeLibs=false` and under the NativeScript runtime.
- **iOS and desktop link.** The changed iOS sources compile for `arm64-apple-ios15.0-simulator` and the
  Xcode project generates with the engine sources in it, but the full Xcode build stopped at an unrelated
  `zstd` compile error (Xcode 27 with this `libs-external`), so the static library was not linked. Desktop
  was not built. Mac Catalyst, `armv7`, `x86_64` Android, `lite` and `standard` untouched.
- The attribution table is by first symbol of each input section under ThinLTO: good to a few percent.

## What could be better

- Geocoding, `sgre` and `osrm` could use the same pattern (their engines are leaves) if 0.4 MB ever
  matters; the adapters would need a JSON or flat-struct boundary of their own.
- The remaining `+0.26 MB` is per-library overhead, not a shared dependency. Only a single library avoids
  it, which is the thing this design gives up to let the engine be dropped.
- iOS keeps the `ValhallaRouting` xcframework with its own ObjC classes; paired with `MassifMaps` it still
  clashes on `MSFRoutingRequest` and duplicates `vr_*`.

## Method

`libmassif.so` relinked from the existing bitcode objects of a #450 build without `-s` and with
`-Wl,-Map`; module-to-core symbols from `llvm-nm` on the same sources compiled without LTO (55 files for
`all/native/{routing,geocoding}` and `libs-massif/{geocoding,sgre,osrm}`, 33 SWIG wrappers). The "Measured"
table is two full `build-android.py --profile full --android-abi arm64-v8a` runs, one per tree.
