---
title: Native modules
description: Shipping Valhalla, routing and geocoding as separate native libraries an app can include or drop. Design study, three options, measured.
sidebar_position: 3
---

# Native modules: Valhalla, routing, geocoding as separate libraries

**Design study, nothing implemented.** Question: can an Android app include or drop Valhalla, offline
routing (`sgre`/`osrm`) and geocoding, instead of choosing one `libmassif.so` per profile, the way
`libsqlite.so` is split out ([build-and-size](build-and-size.md#sqlite-as-its-own-libsqliteso-android))?
Numbers are arm64-v8a Release, NDK 27.3, `full` profile, built from the tree of #450.

**Answer:** yes for Valhalla, through a small C ABI at the JSON level, with the existing
`libvalhalla_routing.so` as the engine. Not worth it for geocoding, `sgre`, `osrm` (0.4 MB between them),
and a C++ split (option b) is the wrong tool.

## What is actually movable

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

`libvalhalla_routing.so` (the `routing` AAR) is 4,423,072 B stripped: `.text` 2.37 MB, `.rodata` 1.07 MB,
`.eh_frame` 0.42 MB. It links `c++_static` and compiles its own SQLite (`NEEDED`: only `liblog` `libz`
`libm` `libdl` `libc`). An app that ships `full` and that AAR carries the Valhalla stack, a SQLite and a
libc++ twice, about 4.4 MB.

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
- **`routing-lib` already is that engine.** `routing::ValhallaRoutingProxy::CallRaw(databases, config,
  endpoint, json) -> json` runs the same `route` / `trace_attributes` pipeline. Two forks of one file, and
  an iOS clash: both ship `MSFRoutingRequest`, `MSFRouteMatchingRequest`, `MSFValhallaOnlineRoutingService`.
- **`valhalla-online` needs no engine**: HTTP in, JSON out, parsed in core.
- **SWIG directors.** Java may subclass `RoutingService` and `GeocodingService`, so a C++ boundary has to
  share their vtables.

One constraint decides the direction: the `routing` AAR is meant to work **without the map**, so
`libvalhalla_routing.so` cannot depend on `libmassif.so`. Core calls the module, never the reverse, and
core must not carry a `DT_NEEDED` on it or a dropped file fails `System.loadLibrary("massif")`.

## The options

| | (a) C ABI at the engine | (b) per-module SWIG + C++ surface | (c) one Valhalla, core calls it |
|---|---|---|---|
| boundary | `vr_*` C functions, JSON strings | C++ classes across `.so` | (a), Valhalla only, `routing-lib` as engine |
| exported from core | nothing | ~100 symbols + interface classes (`RoutingService` vtable/typeinfo...) | nothing |
| exported from module | ~8 `vr_*` functions | its classes + wrappers | ~8 `vr_*` functions |
| classes across | none (`const char*`, `int`) | `Variant`, `MapPos`, `Projection`, `RoutingRequest/Result`, `PackageManager` | none |
| SWIG / JNI | wrappers stay in core, Java API unchanged | wrapper list split per library, `loadLibrary` per module in the generated `ModuleJNI` | as (a) |
| exceptions, RTTI, STL | none cross; module may stay `c++_static` | needs one `libc++_shared`; `catch` and `dynamic_cast` of hidden-visibility types across libraries | as (a) |
| size | core 12.29 MB to ~9.5 MB, engine ~3.4 MB, no duplicate | same cut, plus ~300 dynsym entries | as (a) |
| iOS | static archive, weak entry point + force-load; or dylib framework | same plus ObjC proxies split by module | as (a) |

### (a) C ABI at the engine

Core keeps request serialization, result parsing and the adapters (`ValhallaOfflineRoutingService`,
`MultiValhalla...`, `PackageManagerValhalla...`). The engine exports, roughly: create from config + MBTiles
paths, `call(endpoint, json) -> malloc'd json`, `add_locale`, `default_config`, `last_error`, `free`.
Nothing but C types crosses, so no exception, `std::string`, vtable or RTTI does; the module's STL is its
own business. Core finds it at first use with `dlopen("libvalhalla_routing.so")` + `dlsym`, so "drop" means
"do not ship the file", and an absent engine throws a clear `GenericException` at the first offline call.

- **Direction and linking:** no `DT_NEEDED`. `vr_*` joins `scripts/android/version-script`, which the
  routing build already shares.
- **Databases cross as paths.** `GraphReader` takes `shared_ptr<sqlite3pp::database>` and `sqlite3pp` has
  no constructor from a raw `sqlite3*`, so handing over open handles would need a vendored change and would
  tie both libraries to one `sqlite3pp` ABI. The package handler already holds the file name
  (`PackageHandler::_fileName`). The engine opens its own read-only connections and must cache them by path.
- **Work:** one header and a `vr_` shim in `routing-lib`; the JNI and ObjC wrappers there call it; core
  swaps three `ValhallaRoutingProxy` functions for a client; `valhalla` and `protobuf` leave the core CMake;
  `routing-lib` links `libsqlite.so` instead of its own SQLite.
- **Cost:** the module dependency is a runtime one, so a build that has the profile but not the file only
  fails on use.

### (b) per-module SWIG wrappers + exported C++ surface

Move the Valhalla, geocoding, `sgre`, `osrm` classes and their wrappers into libraries that link
`libmassif.so`. Everything above `-fvisibility=hidden` has to be re-exported: ~100 core symbols and the
interface classes the module subclasses and the facade calls (`RoutingService`, `GeocodingService`,
requests/results, `Address`), each with vtable and typeinfo. The version script today exports `Java_*`,
`mm_*`, `sqlite3_*` only; exporting `massif::*` wholesale would multiply the 3138 dynsym entries, so each
class needs an explicit export macro that nothing enforces today.

- **SWIG:** wrappers are already one `.cpp` per class, so the split is a CMake glob; the generated
  `ModuleJNI` must `loadLibrary` its own library before `swig_module_init`.
- **Fragile:** an `.so` pair that must share a libc++, an ABI that changes with every core header edit, a
  module that must be rebuilt with every core release. `routing` standalone would need a different build
  from the one linked to core.
- **Only gain over (a):** the typed Java classes live in the module. Not needed, they are tiny.

### (c) one Valhalla: `libvalhalla_routing.so`, core calls it

(a) restricted to Valhalla, with `routing-lib`'s `CallRaw` as the engine: the two forks of
`ValhallaRoutingProxy` collapse to one, the duplicate disappears (4.4 MB for an app with both), the config
header `ValhallaDefaultConfig.h` exists once. On iOS the `ValhallaRouting` product stops duplicating
Valhalla and `libsqlite`; its three clashing ObjC class names still need a decision (rename them, or keep
the "never pair with `MassifMaps`" rule). This is the part of (a) worth doing first.

## Recommendation

**(c), built on the boundary of (a).** It removes the duplication the AAR has today, gives the "drop it"
switch for the one piece that is 80% of the weight, keeps the Java and ObjC APIs and the facade unchanged,
and keeps `libvalhalla_routing.so` usable without a map. Do not do (b). Leave geocoding, `sgre`, `osrm`
inside core: ~0.4 MB, and the same C ABI pattern applies later if someone asks, because the engines are
already leaves.

Order of work:

1. `vr_*` header and shim in `routing-lib`; its JNI and ObjC call it. No behaviour change.
2. `routing-lib` links `libsqlite.so` (follow-up of #450); check the standalone AAR still runs.
3. Core engine client behind the existing `_MASSIF_VALHALLA_ROUTING_SUPPORT`; drop `valhalla`, `protobuf`
   from the core build; offline Valhalla throws if the engine is missing. Host test with a stub engine.
4. Packaging: `full` AAR carries the engine in `jniLibs`, `core` does not; an app drops it with
   `packaging.jniLibs.excludes`. One of the two builds (`full` / `routing`) goes away.
5. iOS: engine as its own static xcframework and a SwiftPM product; settle the ObjC name clash.
6. Facade and `docs/getting-started/packages.md` follow the new layout.

## Not verified

- Anything at runtime: no engine was built, loaded or called; every size after the split is an estimate
  (core: 12.29 MB minus ~2.74 MB of Valhalla stack, unwind tables not attributed).
- `dlopen` by bare name from `libmassif.so` under Android linker namespaces, `extractNativeLibs=false`
  and the NativeScript runtime. Fallback: `System.loadLibrary` from the Java side, then `dlopen` with
  `RTLD_NOLOAD`.
- The `c++_static` footprint of `libvalhalla_routing.so` (what it would save on `c++_shared`).
- That `CallRaw` and the core's in-process pipeline return identical JSON for `route` and
  `trace_attributes` (same Valhalla calls by reading, config and locales not compared).
- Per-call cost of opening MBTiles by path versus the package handler's long-lived connection.
- iOS entirely: weak symbol against a static archive does not pull the archive member on ld64, so the
  module needs `-force_load`/`-ObjC` or a dynamic framework; neither was tried. Mac Catalyst neither.
- 32-bit ABIs, `x86_64`, and the `lite`/`standard` profiles.
- Symbol attribution is by first symbol of each input section under ThinLTO: good to a few percent.

## Method

`libmassif.so` relinked from the existing bitcode objects of a #450 build without `-s` and with
`-Wl,-Map`; module-to-core symbols from `llvm-nm` on the same sources compiled without LTO (55 files for
`all/native/{routing,geocoding}` and `libs-massif/{geocoding,sgre,osrm}`, 33 SWIG wrappers).
Sizes of `libvalhalla_routing.so` from `dist/routing-android/arm64-v8a` with `llvm-readelf`.
