---
title: Mac Catalyst builds
description: Why the Catalyst slices are a macOS project in disguise, what that breaks at link time, and the workaround.
sidebar_position: 2
---

# Mac Catalyst builds

The Catalyst slices (`x86_64-maccatalyst`, `arm64-maccatalyst`) are not a real Catalyst build. They
are a **macOS** Xcode project with the Catalyst target triple forced onto the compiles:

```cmake
# scripts/build/CMakeLists.txt
if(APPLE AND (NOT IOS))          # = Mac Catalyst
  add_compile_options("-target" "${SDK_IOS_ARCH}-apple-ios-13.0-macabi" ...)
```

`build-ios.py` then rewrites `-apple-ios-13.0-macabi` to `-apple-ios13.1-macabi` in the generated
`project.pbxproj`. The project itself keeps `SDKROOT = macosx` and `MACOSX_DEPLOYMENT_TARGET`, and
never sets `SUPPORTS_MACCATALYST` / `IS_MACCATALYST`.

## The consequence

Every **compile** produces an object stamped `Mac Catalyst`. Every **link** Xcode drives is targeted
at plain `macOS`. `ld` rejects that combination:

```
ld: building for macOS, but linking in object file built for Mac Catalyst, file '...'
Command PrelinkedObjectLink failed with a nonzero exit code
```

Measured with the shipping toolchain (Xcode 26.5) — the deployment target is irrelevant:

| link | input | result |
|------|-------|--------|
| `ld -r -platform_version macos 11.3` | `-macabi` Mach-O | rejected |
| `ld -r -platform_version macos 13.1` | `-macabi` Mach-O | rejected |
| `ld -r -platform_version macos 14.0` | `-macabi` Mach-O | rejected |
| `ld -r -platform_version macos 13.1` | LTO bitcode | **accepted** (bitcode carries no platform) |
| `ld -r -platform_version macos … -platform_version mac-catalyst …` | `-macabi` Mach-O | rejected (reads as zippered) |
| `libtool -static` | `-macabi` Mach-O, `-macabi` `.a` | **accepted** (no platform check) |

That last row is the whole workaround, and the bitcode row explains why this stayed hidden for
years: `-flto=full` turns every C/C++ translation unit into bitcode, so the prelink only ever saw
platform-less input. The offenders are the things LTO cannot dissolve:

- `libs-external/zstd/.../huf_decompress_amd64.S` — hand-written assembly (x86_64 only)
- `libs-external/angle-metal/x86_64-maccatalyst/libangle.a` — a prebuilt static library, pulled in
  through `PRELINK_LIBS`

It surfaced in August 2026 only because the toolchain moved: the last green CI build was
2026-06-03 on Xcode 16.x, and Xcode 26's linker enforces what ld64 let through.

## What the build does about it

For Catalyst only (`SDK_MACCATALYST` in `scripts/build/CMakeLists.txt`; `scripts/routing/CMakeLists.txt`
tests `APPLE AND (NOT IOS)` and only drops the prelink):

- **no `GENERATE_MASTER_OBJECT_FILE`** — no `ld -r` prelink, so nothing platform-checks the objects
- **no `-flto=full`** — the prelink was what ran the LTO codegen; without it the shipped `.a` would
  contain bitcode, which every consuming app would then need a matching toolchain to read
- **`OTHER_LIBTOOLFLAGS` instead of `PRELINK_LIBS`** for `libangle.a` — the libtool step merges
  archive members without looking at their platform

Catalyst therefore ships **without LTO**. iOS and the simulator slices are untouched.

`libs-external/zstd` also drops its amd64 assembly on Catalyst (`ZSTD_DISABLE_ASM`). That was the
first fix for this failure, before the prebuilt `libangle.a` showed the problem was general; with
the prelink gone it is redundant, and it can be reverted whenever someone wants the assembly
huffman path back on x86_64 Catalyst.

## The real fix, when it is worth it

Make it a genuine Catalyst build instead of a macOS one: `SUPPORTS_MACCATALYST=YES` and
`xcodebuild -destination 'platform=macOS,variant=Mac Catalyst'` over a project whose `SDKROOT` is
`iphoneos`. Xcode then emits `-platform_version mac-catalyst …` for the links, the prelink works,
LTO comes back, and the `-target …-macabi` hack plus the `pbxproj` string rewrite in `build-ios.py`
can go. It needs `build-ios.py` and the CMake project reworked together, so it is a project, not a
patch.

Until then: **anything added to an Apple build that is not compiled from C/C++ source in this tree
will break Catalyst again** — another `.S`, another prebuilt `.a`, or any target that opts out of
LTO. The failure always names the offending file.
