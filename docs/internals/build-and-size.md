---
title: Binary size & build time
description: Where the shipped binary’s bytes and the build’s minutes actually go, measured.
sidebar_position: 2
---

# Binary size and build time

Where the shipped SDK's bytes and build minutes actually go, measured rather than assumed, plus
the mechanisms that make some of it hard to remove. Render-side performance lives in
[rendering/10-performance.md](rendering/10-performance.md); this page is about the artifact.

All numbers: Android **arm64-v8a, Release**, one build tree, NDK 27.3.

## What the profiles cost

The profile machinery (`scripts/build/sdk_profiles.json`) is not cosmetic — it is by far the
largest size lever in the project, and CI already publishes one AAR and one iOS zip per profile
(`.github/workflows/build.yml` matrix `["full","standard","lite"]`).

| profile | `.so` | vs full | JNI wrapper files |
|---|---|---|---|
| `full` (valhalla + geocoding + routing + packagemanager + sqlite) | 11,418,904 | — | 322 |
| `standard` (sqlite, search, offline, editable) | 7,997,064 | **−3.42 MB, −30.0%** | 256 |
| `lite` | 7,043,888 | **−4.38 MB, −38.3%** | 236 |

Nothing needs inventing to get those 3.4 MB back — an app that does not route offline should
consume the `standard` artifact. Measure a profile by regenerating its wrappers first, because
they are profile-specific:

```sh
cd scripts && python3 swigpp-java.py --profile standard --swig /path/to/mobile-swig/swig
```

`generated/` is **gitignored**, not checked in — that command overwrites whatever profile's
wrappers are currently in the tree, and there is no `git checkout` to undo it. Regenerate with the
profile you actually develop against when you are done.

## Where the bytes go (full profile)

Sections, and `.text` ownership by symbol attribution:

| section | size | note |
|---|---|---|
| `.text` | 6.7 MB | |
| unwind metadata | 1.69 MB | `.eh_frame` 1.04 + `.gcc_except_table` 0.36 + `.eh_frame_hdr` 0.28 — **16% of the file** |
| `.rodata` | 1.01 MB | |
| `.dynsym`+`.dynstr`+hashes | 0.41 MB | **3138 exported `Java_*` symbols** |
| `.bss` | 1.9 MB | RAM, not file; demand-zero |

`.text` ownership: valhalla ~22%, libc++/unwind ~16%, `all/native` ~15%, generated JNI wrappers
~5%, boost ~4%, freetype/sqlite/harfbuzz ~2% each. **vt + mapnikvt + cartocss together are 3.2%** —
the renderer is not where the size is.

## What the MLT decoder costs

`libs-external/mlt` builds the decoder half of maplibre-tile-spec's C++ implementation — 9 source
files plus FastPFOR's `bitpacking.cpp`. The encoder is not built, so `fsst`, `earcut` and
`nlohmann/json` never enter the tree and none of that repo's own submodules need checking out.

Compiled alone for arm64 at `-Oz` without LTO (so: an upper bound, before `--gc-sections` and
`--icf=all` see it), `.text`+`.rodata`+`.data` sum to **254 KB**:

| object | size |
|---|---|
| `vendor/fastpfor/bitpacking.cpp` | 131.5 KB |
| `decoder.cpp` | 67.9 KB |
| `decode/int.cpp` | 20.0 KB |
| everything else (7 files) | 34.5 KB |

Half of it is FastPFOR's unrolled 32x32 pack/unpack table, and the *pack* half of that is
encode-only, so the linked cost should land well under the 254 KB. It is reached from
`MBVectorTileDecoder` once an app sets `TILE_FORMAT_MLT`, but the code is unconditionally linked —
`--gc-sections` cannot drop it. Gating the subproject behind a profile flag is still open.

## What the dependencies are trimmed to

Each dependency was built alone for arm64 at `-Oz -flto=thin` with `--gc-sections`, before and after
(sizes are of that stub library, so they are the saving per ABI, not a share of the full `.so`). A
size-only flag has to be proven to link and run: a stub linked with undefined symbols allowed hid
that two of the first SQLite flags do not compile in this amalgamation.

| dependency | before | after | how |
|---|---|---|---|
| freetype | 525,480 | 316,384 (**−209 KB**) | `config/freetype/config/ftmodule.h` shadows the upstream module list (all drivers) with TrueType, CFF, SDF and the renderers; `ftoption.h` drops the bytecode interpreter, Mac fonts, LZW and the autofit-only HarfBuzz hook |
| harfbuzz | 559,608 | 514,032 (**−45 KB**) | `HB_NO_VAR`, `NAME`, `STYLE`, `OT_FONT_GLYPH_NAMES`, `OT_SHAPE_FRACTIONS`, `HINTING`, `SUBSET_LAYOUT` on top of the existing set |
| sqlite | 564,280 | 548,200 (**−16 KB**) | `OMIT_FOREIGN_KEY` (−12), `PROGRESS_CALLBACK`, `INTROSPECTION_PRAGMAS`, `UTF16`, `DEFAULT_MEMSTATUS=0` |

freetype was linking every driver because `ftmodule.h` is upstream's, and the SDK loads TrueType and
OpenType faces with `FT_LOAD_NO_HINTING` only (`vt` renders normal and SDF bitmaps), so Type 1, CID,
PFR, Type 42, Windows FNT, PCF, BDF, SVG and the autohinter were dead weight. WOFF1 (zlib) and WOFF2
(brotli) stay. Output equality was checked on the host against the old configuration: glyph
metrics, normal and SDF bitmaps and HarfBuzz shaping of Roboto, Arial Unicode (Latin, Arabic,
Devanagari, Thai, CJK, Hangul) and a CFF OpenType face (Javanese) hash identically.

`SQLITE_OMIT_ANALYZE` and `SQLITE_OMIT_ALTERTABLE` look free and do not link
(`sqlite3ExprForVectorField`), as `OMIT_WINDOWFUNC` and `OMIT_TRIGGER` do not compile. The wrapper
`sqlite3pp` calls `sqlite3_set_authorizer` and `sqlite3_column_decltype`, so `OMIT_AUTHORIZATION` and
`OMIT_DECLTYPE` stay off.

### SQLite as its own `libsqlite.so` (Android)

With `-DSQLITE_SHARED=ON` (on in `build-android.py` and the `android-dev` module) SQLite is built as
`libsqlite.so` and `libmassif.so` links it (`DT_NEEDED`) instead of compiling it in. An app that ships
another `libsqlite.so`, such as the NativeScript SQLite plugin, then keeps one copy: gradle `pickFirst`
on `lib/<abi>/libsqlite.so`, and the app's own `jniLibs` decide which one. Massif knows nothing of the
other library; it only needs a thread-safe SQLite exporting `sqlite3_*`.

The shared build drops two of the flags above, `SQLITE_THREADSAFE=2` (now `1`) and `SQLITE_OMIT_WAL`,
so that it can stand in for any other user of the name. The rest of the trims stay. `sqlite3_*` is
exported through `scripts/android/version-script`, and `SQLITE_API` is defined as default visibility so
the `-fvisibility=hidden` references in `libmassif.so` are not hidden. Without it the link fails.

Checked on a stub linked the same way (arm64-v8a): `libsqlite.so` is 601 KB with 219 exports, soname
`libsqlite.so`, and the stub shows `NEEDED libsqlite.so`. The full SDK build and a run are not done.

### Packed relocations, and why `minSdk` is 23

`.rela.dyn` was 574 KB of the `full` library, 24-byte `R_AARCH64_RELATIVE` entries for every vtable
and string table. `-Wl,--pack-dyn-relocs=android` encodes them as deltas: on freetype + harfbuzz
alone the section goes from 38,664 to 7,567 bytes (−80%), on a synthetic 24k-entry table −87%. The
platform linker reads the format from API 23, so `minSdk` is 23 and `build-android.py` builds against
API 23. RELR (`--pack-dyn-relocs=android+relr`) would cut it to a few KB but needs API 28, with
`--use-android-relr-tags` below API 30.

## Two mechanisms worth knowing

**`--gc-sections` cannot drop a translation unit that has a namespace-scope static.** `.init_array`
references its initializer, which roots the whole TU no matter what else the linker strips. This is
why unreachable Valhalla service actions stayed linked despite `-Oz`, `--gc-sections` and
`--icf=all`, and why the fix was to keep the sources out of the build list rather than to lean
harder on the linker. The full-profile library ran **309 static initializers at every `dlopen`**
before that prune, 291 after.

**`.bss` is not RAM until it is touched.** `baldr::BucketCosTable` is 1.5 MB of `.bss`, which reads
alarming, but the symbol is a function-local static (`_ZZ…E8instance` with a `_ZGV…` guard) reached
only from `graphtile.h` behind `has_predicted_speed()`. Demand-zero pages cost nothing until the
first predicted-speed decode. Check the symbol type and the guard before "fixing" a large `.bss`
entry.

## Compiler and linker flags, measured

Android **arm64-v8a, Release, `standard` profile** (the `core` AAR), one flag changed at a time
against a 9,221,584-byte baseline; a build relinks in under a minute because ThinLTO defers the work.
Sizes are the stripped `libmassif.so`; nothing here was timed on a device.

| change | file | where it went |
|---|---|---|
| `-fno-asynchronous-unwind-tables -funwind-tables` | **−107,232** | `.eh_frame` −107 KB: sync tables drop the epilogue CFI |
| `-Wl,-mllvm,-enable-merge-functions` | **−74,640** | `.text` −67 KB, merges what `--icf=all` cannot |
| `cartocss` at `-Oz` | **−171,656** | `.text`; compiles a style once |
| `freetype` at `-Oz` | **−186,856** | `.text` |
| all four above | **−504,944 (−5.5%)** | shipped |
| `vt` at `-Oz` | −285,320 | not taken: decode and label path |
| `mapnikvt` at `-Oz` | −347,144 | not taken: per-feature path |
| render and tile dirs at `-Oz` | −736,832 | not taken: [+614 KB buys 3.2% frame time](rendering/10-performance.md) |
| `tess2` / `zlib`+`brotli`+`zstd` at `-Oz` | −26,920 / −20,800 | not taken: too little for the risk |
| `-Wl,-mllvm,-machine-outliner-reruns=3` | −3,376 | noise |
| `-Wl,--exclude-libs,ALL` | 0 | the version script already hides them |
| `-fno-math-errno` | 0 | Android's clang defaults to it |
| `-fmerge-all-constants` | −144 | not worth the semantics |

**Sync unwind tables keep exceptions working** but the release build also has `-fomit-frame-pointer`,
so a crash unwinder walks `.eh_frame`, and a fault inside a function epilogue can now mis-unwind.
Check that native crash symbolication still resolves before a release. `cartocss` and `freetype`
are `-Oz` on Android only; iOS and the web were not measured.

**Where the unwind tables are.** `.eh_frame` is 22,189 entries of 34 bytes on average: libc++ and
runtime 156 KB, the SDK 139 KB, the JNI wrappers 72 KB (2,459 entries), the facade 41 KB, boost
38 KB, freetype and the image libraries together about 36 KB. So the C-only dependencies the
"Open" list names hold at most ~40 KB of the 1.2 MB; the JNI wrappers hold more.

**Where `.text` is.** 6.3 MB across 22,611 functions, no single owner: SDK core 26%, libc++ 13%,
`vt` 11%, `mapnikvt` 9%, JNI wrappers 5%. `std::variant` visitor tables are ~150 KB, `typeinfo`
names 181 KB of `.rodata` (266 `dynamic_cast` uses rule out `-fno-rtti`), the Brotli dictionary
123 KB and freetype's Adobe glyph list 56 KB.

## The object API as a variant of its own (Android)

`libmassif.so` carried a JNI wrapper for every class of the object API: 3,195 `Java_*` exports, their
dynamic-symbol tables, the director classes and the per-class static initialisers. The facade reaches the
whole SDK by id, handle and path and needs none of it, so those wrappers are now generated only for the
`objectapi` profile (`scripts/swigpp-java.py` keeps `MassifApi`, `EventListener`, `UiDispatcher`, `Log` and
`LogEventListener` otherwise). The C++ is the same; only the SWIG wrappers differ.

arm64-v8a Release, `standard` profile, same tree:

| profile | `libmassif.so` | `Java_*` exports | |
|---|---|---|---|
| `standard` (surface only) | 7,392,312 | 105 | **-1,324,328 B (-15.2%)** vs the `core` build below |
| `standard+objectapi` | 8,732,208 | 3,213 | +15,568 B: the three `MassifApi.attach*Events` exports |
| `core` before the split (`#467`) | 8,716,640 | 3,195 | |

That is more than the 0.83 MB the reach audit estimated from the linker map of the wrapper code and symbol
tables alone: the rest is `.rodata`, relocations and the wrappers' static initialisers. The Java side shrinks
too, from 578 SWIG classes (1.84 MB of `.class` bytes) to 10. Nothing here was run on a device beyond the
demo's gallery example (heatmap, tap, swipe) on an arm64 emulator.

## Build time

45.6 min of CPU per ABI, four ABIs, each recompiling the same headers. By module: mapnikvt 24%,
valhalla 19%, generated JNI wrappers 14%, cartocss 8%. The individual hogs are all Boost.Spirit
grammars — `ParserUtils.cpp` 24 s, `CartoCSSParser.cpp` 22 s, `GeneratorUtils.cpp` 21 s,
`QueryExpressionParser.cpp` 12 s — roughly 15% of a full build between them, and they essentially
never change, so they are pure ccache fodder. With ninja + ccache one ABI goes 70.9 s cold to
13.8 s warm; raise the cache first (`ccache --max-size 30G`), since one ABI writes about 1 GB and
the 5 GB default has the four ABIs evicting each other.

### The release workflow (`build.yml`)

Before this, a release run cached nothing: the ccache save failed (`tar` exit 2, no `ccache-*`
entry ever existed) and every job compiled from scratch, with `iOS lite` (72 min) setting the wall clock. Now:

- **Android** — ccache in `.ccache` inside the workspace, `CCACHE_COMPILERCHECK=content` (the NDK is
  restored from a cache on every run, so its `mtime` is new each time and the default check
  misses everything), 2 GB, and a `restore`/`save` pair with `if: always()` so a failed release
  still keeps the objects it compiled. One cache per profile: a profile changes `SDK_CPP_DEFINES`,
  which is on every compile line, so profiles share nothing. The scripts build `armeabi-v7a`,
  `arm64-v8a`, `x86_64` by default; 32-bit `x86` is gone from the scripts and the dev app too.
- **iOS** — the Xcode generator ignores `CMAKE_<LANG>_COMPILER_LAUNCHER`, so ccache cannot reach
  it. Xcode 26 compilation caching does: `COMPILATION_CACHE_ENABLE_CACHING` +
  `COMPILATION_CACHE_CAS_PATH` passed through `--cmake-options`, the CAS directory cached like
  ccache. Measured on one `lite` arm64-simulator slice, Xcode 26.5: 1024 misses cold, 1023 hits
  warm, 131 s to 74 s; the remainder is the single-threaded `-flto=full` prelink, which no cache
  covers.
- **css2xml** is no longer built here: it ships as an npm package (it was a serial `make` inside the
  `lite` job of both platforms, 430 s on Android and 979 s on the iOS job that set the wall clock).
- **SWIG's autotools** install only when the SWIG cache misses, instead of on every job.

## Open, roughly by value

- **iOS leftovers.** The static framework now gets `-flto=full` (#67; Mac Catalyst still does not —
  [mac-catalyst.md](../maintenance/mac-catalyst.md)), but bitcode is still wired in
  `scripts/build-ios.py` (`ENABLE_BITCODE=YES` for armv7/arm64, dead since Xcode 14) and `i386` /
  `armv7` are still in `IOS_ARCHS`, filtered out of the default arch list only.
- **Pruning the JNI layer**, the largest identifiable item left: ~0.58 MB if the classes the app
  never names go, up to ~0.9 MB for a surface-API-only SDK. It needs the Android map view to stop
  being a SWIG `BaseMapView` that the facade adopts.
- **`vt` / `mapnikvt` / render dirs at `-Oz`** (−285 / −347 / −737 KB), which needs a paired device
  benchmark per [10-performance.md](rendering/10-performance.md).
- Not worth doing, measured above: unwind tables on the C-only dependencies (~40 KB at best, with
  the `std::terminate` risk when a C++ exception crosses a C callback), `--exclude-libs`,
  `-fno-math-errno`, `-fmerge-all-constants`. No PGO anywhere.
- **RELR** once `minSdk` can be 28; see above.

## Measured NOT to work — `RegisterNatives`

The obvious-looking win is to stop exporting the 3496 `Java_*` wrappers and register them from
`JNI_OnLoad` instead. It was built and measured: the generator derives every descriptor from the
`*ModuleJNI.java` sources, and the emitted symbol set matched the 3496 exports exactly, in both
directions. **It makes the library 131 KB bigger.**

| section | exported | registered | delta |
|---|---|---|---|
| `.dynstr` | 307,725 | 4,202 | −303,523 |
| `.dynsym` | 92,160 | 8,280 | −83,880 |
| `.hash` + `.gnu.hash` + `.gnu.version` | 64,096 | 3,490 | −60,606 |
| `.rela.dyn` | 574,512 | 836,784 | **+262,272** |
| `.rodata` | 1,112,997 | 1,338,149 | +225,152 |
| `.data.rel.ro` | 209,168 | 298,352 | +89,184 |
| **file** | 11,418,904 | 11,547,912 | **+131,055** |

The −448 KB of dynamic symbol machinery is real and lands as predicted. What kills it is that
`JNINativeMethod` holds **three pointers** — name, signature, function — so 3496 methods put 10,488
relocated pointers in `.data.rel.ro`, each costing a 24-byte `R_AARCH64_RELATIVE` entry. A string
table the dynamic linker already packs efficiently gets traded for a relocated pointer table that
does not.

A version storing string *offsets* into one blob, and materialising the function pointers in code
rather than data, would remove ~178 KB of relocations and ~84 KB more, landing near **−150 to
−190 KB (1.3–1.6%)**. That is a hand-rolled offset encoding inside generated code, for less than a
twentieth of what moving an app to the `standard` profile gives. Not worth it on size alone. The
one argument not tested is load time: 3496 fewer dynamic symbols should make `dlopen` cheaper, and
if that turns out to matter the calculus changes.
