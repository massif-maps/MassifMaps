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

## Build time

45.6 min of CPU per ABI, four ABIs, each recompiling the same headers. By module: mapnikvt 24%,
valhalla 19%, generated JNI wrappers 14%, cartocss 8%. The individual hogs are all Boost.Spirit
grammars — `ParserUtils.cpp` 24 s, `CartoCSSParser.cpp` 22 s, `GeneratorUtils.cpp` 21 s,
`QueryExpressionParser.cpp` 12 s — roughly 15% of a full build between them, and they essentially
never change, so they are pure ccache fodder. With ninja + ccache one ABI goes 70.9 s cold to
13.8 s warm; raise the cache first (`ccache --max-size 30G`), since one ABI writes about 1 GB and
the 5 GB default has the four ABIs evicting each other.

## Open, roughly by value

- **iOS leftovers.** The static framework now gets `-flto=full` (#67; Mac Catalyst still does not —
  [mac-catalyst.md](../maintenance/mac-catalyst.md)), but bitcode is still wired in
  `scripts/build-ios.py` (`ENABLE_BITCODE=YES` for armv7/arm64, dead since Xcode 14) and `i386` /
  `armv7` are still in `IOS_ARCHS`, filtered out of the default arch list only.
- **Unwind tables on the C-only dependencies** (sqlite, libpng, libjpeg, libwebp, freetype, brotli,
  zstd, miniz) against that 1.69 MB. Risky rather than free: a C++ exception thrown from a callback
  has to unwind back through those C frames, and without tables that is a `std::terminate`, so it
  needs the callback paths audited before it can be trusted.
- Unset and cheap: `-Wl,--exclude-libs,ALL`, `-fmerge-all-constants`, `-fno-math-errno`. No PGO
  anywhere.

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
