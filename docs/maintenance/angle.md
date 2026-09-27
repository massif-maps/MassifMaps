---
title: Rebuilding the ANGLE slices
description: How to rebuild the vendored MetalANGLE static libraries for iOS, the simulator and Mac Catalyst, and the fork patches that must survive.
---

# Rebuilding the ANGLE slices

`libs-external/angle-metal` is a **nested submodule** (`massif-maps/angle-metal`) that contains
prebuilt binaries only — one `libangle.a` per architecture plus the headers. There is no ANGLE
source in it, so "updating ANGLE" means building the `.a` files elsewhere and dropping them in.

Why MetalANGLE rather than upstream `google/angle`, and what it costs, is in
[Graphics API migration](../internals/rendering/16-graphics-api-migration.md). The short version:
MetalANGLE ships an Xcode project, so this page is `xcodebuild` and nothing else — upstream would
need depot_tools, gclient and a ~10 GB sync.

Verified 2026-08-18 with Xcode 26.5 (17F42), `iPhoneSimulator26.5.sdk`, on macOS 15 / arm64,
building `kakashidinho/metalangle` master `ec925142e`.

## Layout

| Path | Contents |
|---|---|
| `libs-external/angle-metal/<arch>/libangle.a` | the static slice; `<arch>` is `arm64`, `arm64-simulator`, `arm64-maccatalyst`, `x86_64`, `x86_64-maccatalyst` |
| `libs-external/angle-metal/include/` | `EGL/ GLES/ GLES2/ GLES3/ KHR/`, `angle_gl.h`, `export.h`, and the `MGL*.h` MGLKit headers |

`scripts/ios-dev/bootstrap.sh` symlinks `.angle` at one of those arch directories and
`project.yml` links `$(SRCROOT)/.angle/libangle.a`, so switching slice is a symlink swap and a
relink — no reconfigure.

## Build

```sh
git clone https://github.com/kakashidinho/metalangle.git
cd metalangle/ios/xcode
./fetchDependencies.sh
```

`fetchDependencies.sh` clones four pinned revisions (glslang, SPIRV-Cross, jsoncpp and its source)
from the chromium.googlesource.com mirrors into `third_party/`. It only clones; nothing is executed.

Then one `xcodebuild` per slice. The settings after `build` are **not optional** — they are what
turns a 446 MB archive into the ~14 MB slice that ships, and what makes the symbols link from a
static library into dependent projects:

```sh
xcodebuild -project OpenGLES.xcodeproj -target MetalANGLE_static \
  -configuration Release -arch arm64 -sdk iphonesimulator \
  IPHONEOS_DEPLOYMENT_TARGET=13.0 \
  ONLY_ACTIVE_ARCH=YES \
  DEPLOYMENT_POSTPROCESSING=YES STRIP_INSTALLED_PRODUCT=YES STRIP_STYLE=non-global \
  GENERATE_MASTER_OBJECT_FILE=YES KEEP_PRIVATE_EXTERNS=NO \
  GCC_SYMBOLS_PRIVATE_EXTERN=NO GCC_INLINES_ARE_PRIVATE_EXTERN=YES \
  GCC_PREPROCESSOR_DEFINITIONS='$(inherited) ANGLE_PLATFORM_EXPORT= ANGLE_EXPORT= ANGLE_UTIL_EXPORT=' \
  GCC_ENABLE_CPP_EXCEPTIONS=YES GCC_ENABLE_CPP_RTTI=YES GCC_ENABLE_OBJC_EXCEPTIONS=YES \
  build
```

Output: `build/Release-iphonesimulator/libMetalANGLE_static.a` → copy to
`libs-external/angle-metal/arm64-simulator/libangle.a` (note the rename).

Per slice, change `-sdk` and `-arch`:

| Slice | `-sdk` | `-arch` | Output dir |
|---|---|---|---|
| `arm64-simulator` | `iphonesimulator` | `arm64` | `Release-iphonesimulator` |
| `arm64` | `iphoneos` | `arm64` | `Release-iphoneos` |
| `x86_64` | `iphonesimulator` | `x86_64` | `Release-iphonesimulator` |

Mac Catalyst uses `-scheme`, plus the same settings block, once per architecture:

```sh
xcodebuild -project OpenGLES.xcodeproj -scheme MetalANGLE_static -sdk macosx \
  -configuration Release -destination 'platform=macOS,variant=Mac Catalyst' \
  SUPPORTS_MACCATALYST=YES CODE_SIGNING_ALLOWED=NO <same settings as above> build
# and for the Intel slice:
#   -destination 'platform=macOS,variant=Mac Catalyst,arch=x86_64' ARCHS=x86_64 ONLY_ACTIVE_ARCH=NO
```

Two traps here, both different from the 2021 recipe in the `angle-metal` README:

- **`-scheme` writes to DerivedData, not `build/`.** The `-target` invocations above land in
  `ios/xcode/build/Release-<sdk>/`; the Catalyst ones land in
  `~/Library/Developer/Xcode/DerivedData/OpenGLES-*/Build/Products/Release-maccatalyst/`. Read the
  `Libtool`/`Strip` line at the end of the build log for the actual path rather than guessing.
- **The result is thin, not fat.** The old recipe built one archive and split it with
  `lipo -extract`. On Xcode 26.5 the build resolves to the host architecture only (`-arch_only
  arm64` in the libtool line), so there is nothing to extract — build each architecture separately
  and copy each straight to its `<arch>-maccatalyst/libangle.a`.

See [Mac Catalyst](mac-catalyst.md) for why those slices behave like a macOS build at link time.

`armv7` and `i386` slices still exist in the submodule and are dead — the deployment floor is
iOS 13.0. Do not rebuild them.

### Bitcode is gone, and that is most of the size drop

The vendored 2021 device slice is **51.7 MB**; rebuilt at master it is **14.2 MB**. Almost all of
that is bitcode — the old README added `OTHER_CFLAGS="-fembed-bitcode"` for the arm64 and armv7
device targets. Apple removed bitcode in Xcode 14, so the flag is not carried forward and must not
be re-added. The simulator slices never had it, which is why they were 15.2 MB then and 14.2 MB now.

### Why each strip setting is there

Without them the archive keeps every object's debug info and every private symbol:
**446 MB unstripped vs 14.2 MB with them**, for the same code. `GENERATE_MASTER_OBJECT_FILE`
(single-object prelink) plus `STRIP_STYLE=non-global` is the pair that does most of it.
`GCC_SYMBOLS_PRIVATE_EXTERN=NO` must stay `NO` or the SDK cannot see the GL entry points.
The three empty `*_EXPORT=` macros neutralise ANGLE's dllexport attributes, which are meaningless
in a static library and produce visibility warnings otherwise.

C++/ObjC exceptions must be **on**: they are off by default for this project, and a build with them
off breaks exception propagation in *dependent* projects, not in ANGLE itself.

## Fork patches that must survive an update

Only one, and it is in the headers rather than the source:

- **`include/MGLContext.h`** — the `#include "EGL/egl.h"` is commented out and `eglDisplay` is typed
  `void*` instead of `EGLDisplay`, so that including `MGLContext.h` does not drag EGL's headers into
  the SDK's Objective-C surface. Re-apply after copying headers from a new MetalANGLE checkout; the
  rest of `include/` is byte-identical to the upstream tree.

The `angle-metal` README also documents an ES2-downgrade patch (`kEAGLRenderingAPIOpenGLES3` →
`...ES2` in `DisplayEAGL.mm`) for 32-bit devices. **Do not re-apply it** — it caps the context at
ES 2.0, which is the opposite of where the SDK is going, and the devices it was for are below the
deployment floor.

## Checking the result

Launch anything that creates a map and read the two startup lines:

```
GLContext::LoadExtensions: OpenGL ES 3.0.0 (ANGLE 2.1.0.ec925142edeb) (version 300), anisotropic filtering <0|1>
MapRenderer::onSurfaceCreated: renderer 'ANGLE (Metal Renderer: Apple iOS simulator GPU)', depth bits 24, stencil bits 8, vertex texture units <n>
```

The format is current (`GLContext.cpp`, `MapRenderer.cpp`); the values are from the 2026-08-18 run.

The **commit suffix on the ANGLE version is how you tell slices apart** — the 2021 vendored build
reports a bare `ANGLE 2.1.0.` with nothing after the last dot. If you swapped the `.a` and the
suffix did not change, the link picked up the old one.

`scripts/ios-dev` is the fastest way to exercise it:

```sh
cd scripts/ios-dev && ./bootstrap.sh            # or './bootstrap.sh device'
xcodebuild -project MassifDemo.xcodeproj -scheme MassifDemo -sdk iphonesimulator \
  -destination 'id=<simulator-udid>' build
```

`-destination` needs the UDID from `xcrun simctl list devices available`; the by-name form fails
with *"no available devices matched the request"* even when the name is right.

For a frame-time comparison add `PROFILE_RENDER=1` to the bootstrap — it compiles in
`MASSIF_FRAME_PROFILER` / `MASSIF_VT_RENDER_STATS` and their `PROF` / `RenderStats` lines, the
counterpart of android-dev's `-PprofileRender`. It is a compile-time flag, so switching it needs a
re-bootstrap.

## Known gaps

- All five live slices are rebuilt at master `ec925142e` — `arm64`, `arm64-simulator`,
  `arm64-maccatalyst`, `x86_64-maccatalyst`, `x86_64`. `armv7` and `i386` are dead and were not.
  **They are built but not yet vendored**: the submodule still carries the 2021 binaries.
- **Nothing has been run on a physical device** — ES 3.0 on the Metal backend is confirmed on the
  simulator only, and Catalyst was rebuilt but not launched. The checklist is in
  [Graphics API migration](../internals/rendering/16-graphics-api-migration.md#what-still-needs-a-physical-device).
- MetalANGLE's own README grades its ES 3.0 at 90% — primitive restart and last-provoking-vertex
  flat shading are unimplemented. Checked against this renderer: **neither is used** (no
  `PRIMITIVE_RESTART` call sites, no `flat` qualifiers in the vt shaders), so the gap does not bite
  today. Re-check if either appears.
