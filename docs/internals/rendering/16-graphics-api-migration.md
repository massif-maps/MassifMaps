---
title: Graphics API migration
description: Why the SDK stays on OpenGL ES and reaches Metal, D3D and Vulkan through ANGLE, and the phased plan to an ES 3.0 baseline.
sidebar_position: 16
---

# Graphics API migration

Scope: which graphics API the SDK targets, on which platform, and how it gets there. Covers the
move to an OpenGL ES 3.0 baseline (dropping ES 2.0), the Apple situation (Metal), and what
Windows/Linux/macOS need later. Does **not** cover what the renderer draws — that is the rest of
[this set](index.mdx).

Status as of 2026-08-18: Phase 0 run — gates 0.1 and 0.2 pass, gate 0.3 is **not answered** (see
[Phase 0 results](#phase-0--results)). Phases 2 and 3 are done and **verified on an Adreno 610
device**; an iOS device is still owed. The Apple source was decided against upstream ANGLE, see
[MetalANGLE master, not upstream](#metalangle-master-not-upstream-angle). Phase 4 closed with
nothing shipped; what ES 3.0 is still worth taking, re-derived from measured costs rather than from
the feature list, is in [Second harvest pass](#second-harvest-pass-2026-08-26).

## Where we are

Everything renders through **one** API surface: `GLES2/gl2.h` + `gl2ext.h`. Roughly 1730 GL call
sites over 114 distinct entry points.

| Where | Call sites |
|---|---|
| `all/native` | 898 |
| `libs-massif/vt` | 654 |
| `libs-massif/nml` | 177 |

Shaders are GLSL ES 1.00 (`#version 100`) throughout: 27 shader literals across 11 files in
`all/native/renderers/`, ~2000 lines in `libs-massif/vt/src/vt/GLTileRendererShaders.h`, plus
`nml/GLMaterial`.

An ES 3.0 **context** is already requested everywhere, with an ES 2.0 fallback:

| Platform | Context | Notes |
|---|---|---|
| Android | `MapView.java` queries `reqGlEsVersion`, asks ES3, falls back to 2 | minSdk 21; manifest still declares `glEsVersion 0x00020000` |
| iOS | `MapView.mm` asks ES3, falls back to 2 | EAGL by default; MetalANGLE behind `--use-metalangle` |
| Mac Catalyst | MetalANGLE only | `build-ios.py` refuses a Catalyst build without it |
| UWP | `EGLContextWrapper.cpp` hardcodes client version **2** | already ANGLE, on the D3D backend |

So ANGLE is already shipping on two of the platforms. `scripts/ios-dev` builds against MetalANGLE
too — the iOS simulator on Apple Silicon does not run Apple's GL, which is why the local bench
already goes through Metal.

`vt` already carries most of an ESSL 3.00 path: `GLTileRenderer::createShaderProgram` emits
`#version 300 es` plus `#define attribute in` / `varying out` / `texture2D texture`, fragment
shaders write `glFragColor` (a macro), and a failed 3.00 compile falls back per-program to 1.00
(`hasShaderVersionFallback()`). Today exactly one program uses it: the hardware-PCF shadow pass.
`all/native` has no equivalent.

## The decision — ANGLE, not a native backend

The forcing argument is a property of this codebase, not of Apple's deprecation notice.

**The shader system composes GLSL at runtime from app-supplied strings.** `buildShaderProgram`
splices `_fogShaderSource` into a placeholder, prepends a DEM prelude to the app's lighting shader,
and builds one program per (`flags` × `lightingMode` × `filterMode`) combination. Five public
setters feed application GLSL straight in:

| API | Module |
|---|---|
| `SkyOptions.ShaderSource` | `all/modules/components/SkyOptions.i` |
| `FogOptions.ShaderSource` | `all/modules/components/FogOptions.i` |
| `TerrainOptions.SurfaceShaderSource` | `all/modules/components/TerrainOptions.i` |
| `CustomRasterTileLayer.ShaderSource` | `all/modules/layers/CustomRasterTileLayer.i` |
| `PostProcessEffect` fragment source | `all/modules/renderers/PostProcessEffect.i` |

Those permutations cannot be precompiled offline — the app's contribution is a runtime string. A
native Metal/D3D/Vulkan backend would therefore have to **ship a GLSL front-end plus N dialect
back-ends inside the SDK binary** (glslang + SPIRV-Cross, several MB, and a permanent supply of
dialect bugs). That is what ANGLE already is. Writing it ourselves is rebuilding ANGLE worse.

### Cost

| | ANGLE | Native backends |
|---|---|---|
| Renderer source change | **none** of 1730 call sites | all of them, rewritten onto a device/encoder/pipeline API |
| Shaders | preamble macros (Phase 3) | one front-end, three dialects, **shipped and run at runtime** |
| Public GLSL API | survives unchanged | breaks, or needs that runtime translator anyway |
| Cost per new platform | one context/window file + a build script | a backend, a dialect, a windowing path |
| tangram-ng as reference | intact | **lost** — every render file forks from it |
| Binary | one static lib (measure it — see Phase 0) | a shader toolchain, minus the driver layer |

### Native workload, per platform, if we ever do it

| Platform | API | Backend | Dialect | Windowing |
|---|---|---|---|---|
| iOS | Metal | new | MSL | `CAMetalLayer` |
| macOS | Metal | shared with iOS | shared | `NSView` |
| Windows | D3D11/12 | new | HLSL | HWND + swapchain |
| Linux | Vulkan | new | SPIR-V | X11/Wayland |
| Android | GLES | keep | keep | keep |

Three backends, three dialects, a runtime cross-compiler, four windowing paths.

### ANGLE workload, per platform

| Platform | ANGLE backend | Work |
|---|---|---|
| iOS | Metal (ES 2.0 and 3.0 complete) | EGL bootstrap + `CAMetalLayer` view |
| macOS / Catalyst | Metal | same file, `NSView`. Catalyst is already ANGLE-only |
| Windows | D3D11 — ANGLE's oldest and most mature | EGL + HWND; the shape exists in `winphone/native/utils/EGLContextWrapper.cpp` |
| Linux | **none needed** — Mesa serves GLES 3.2 over EGL | EGL + X11/Wayland surface |
| Android | none — native GLES | nothing |

Linux is the quiet win: ANGLE is not a dependency there at all, and the same GLES 3.0 source runs.

## What the others did, and why it does not transfer

| Project | Approach | Cost |
|---|---|---|
| Mapbox | Native Metal. v10 added pluggable backends with 1:1 GL/Metal parity; v11 is **Metal-only** | Commercial team, full rendering rewrite; `mapbox-gl-native` archived Aug 2023 |
| MapLibre | Native Metal. Evaluated a MetalANGLE branch ("wasn't perfect but worked") and rejected it | ~5 engineers, over a year, **for Metal alone**, and only after a renderer modularization phase. Shipped iOS 6.0.0 in Jan 2024 |
| [tangram-ng](https://github.com/farfromrefug/tangram-ng) | Nothing. Pure GLES, EAGL on iOS | — |

Two of three went native, both with dedicated teams, both only after building a backend abstraction
this fork does not have. MapLibre's modularization later paid for Vulkan and WebGPU — that is the
real case for native, and it is a case for a team.

The reference implementation is a non-participant, which matters here more than elsewhere: a native
backend permanently ends the copy-from-tangram workflow that
[the rest of these pages](11-tangram-diff.md) depend on.

## MetalANGLE master, not upstream ANGLE

Decided 2026-08-18, reversing this page's first draft. **MetalANGLE is itself a fork of Google's
ANGLE** — its README's first line says so — so there is no Google-free option here, only a choice of
revision.

`libs-external/angle-metal` is a **nested submodule** (`massif-maps/angle-metal`) holding prebuilt
binaries only: one `libangle.a` per arch plus headers, no ANGLE source. Last pushed 2021-08-31,
built from `kakashidinho/metalangle` at `8ef9aba` (2021-06-30). There is nothing to rebase; either
direction means building from source and re-dropping the `.a`.

What decided it is the build path, not the code:

| | MetalANGLE | upstream ANGLE |
|---|---|---|
| Build tooling | `ios/xcode/OpenGLES.xcodeproj` + `fetchDependencies.sh` — plain `xcodebuild` | depot_tools + gclient + gn, ~10 GB sync |
| Builds under Xcode 26.5 | **yes, unpatched** (measured, see below) | unmeasured |
| MGLKit | included — `MSFGLContext`/`MSFGLKView` keep working | absent; Phase 1 must write the EGL + `CAMetalLayer` bootstrap |

What this costs, accepted knowingly:

- MetalANGLE's own README grades its **ES 3.0 at "90% complete"**, with Primitive Restart
  ("doesn't work reliably") and last-provoking-vertex flat shading unimplemented. Neither is used
  by this renderer today; both become blockers if that changes.
- The fork's last code commit is Mar 2022. Four years of upstream Metal-backend fixes are foregone.
- **Two divergent ANGLE forks stay vendored**: `angle-metal` (2021) and `angle-uwp` (2022 binaries).
  Upstream would have collapsed them to one.
- Phase 5 loses its cheap path: MetalANGLE has no Win32/D3D and no AppKit slice, so Windows and
  native macOS need upstream ANGLE anyway, or another source.

The one argument *for* the fork on the merits — that Apple's upstream contributions prioritised
WebGL conformance over speed, rewriting index buffers on the fly for provoking-vertex rules — is a
hypothesis gate 0.3 was meant to test and **did not**. It remains unmeasured in both directions.

MGLKit is still the piece that is not upstream anywhere: `MSFGLContext` / `MSFGLKView` in
`ios/objc/ui/MapView.h` and the drawable-format block in `MapView.mm`. Staying on the fork defers
rewriting them onto plain EGL + `CAMetalLayer`; it does not remove the job, because Phase 5 needs
that bootstrap for Win32 and AppKit regardless.

## Phase 0 — results

Run 2026-08-18 on macOS 15 / Xcode 26.5, against `kakashidinho/metalangle` master `ec925142e`.
Build commands are in [`docs/maintenance/angle.md`](../../maintenance/angle.md).

### 0.1 — build and run on arm64-simulator: **PASS**

`MetalANGLE_static` built for `iphonesimulator`, `arm64-apple-ios13.0-simulator`, against
`iPhoneSimulator26.5.sdk`. **No patches were needed** — the 2022 tree compiles unmodified on a 2026
toolchain, which was the main risk of staying on the fork. `scripts/ios-dev` linked against it with
no API drift and ran:

```
GLContext::LoadExtensions: OpenGL ES 3.0.0 (ANGLE 2.1.0.ec925142edeb), depth texture 1, shadow samplers 0
MapRenderer::onSurfaceCreated: renderer 'ANGLE (Metal Renderer: Apple iOS simulator GPU)', depth bits 24, stencil bits 8
```

The commit suffix in the version string is how you tell the slices apart: the vendored 2021 build
reports a bare `ANGLE 2.1.0.`. Frames at lon 5.7606 / lat 45.2442 / z13.6 / tilt 55 are identical
between the two builds bar the status-bar clock — 1259 of 3162132 pixels, bbox `(227,79)-(288,117)`.

Stripped with the vendoring settings the slice is **14.2 MB**, against the vendored 15.2 MB. That
also fills one of this page's old gaps: the settings in the `angle-metal` README do reproduce the
shipped artefact, and the difference is Xcode 26.5 with no bitcode rather than anything structural.

`shadow samplers 0` is worth carrying into Phase 3 — the hardware-PCF program is the one place
`ESSL3_FLAG` is used today, and the Metal backend is not offering `sampler2DShadow` here.

### 0.2 — `#define gl_FragColor`: **ACCEPTED, so Phase 3 is not breaking**

Run through the real translator (`SH_GLSL_METAL_OUTPUT`, `SH_GLES3_SPEC`, with `CompilerMtl` /
`ShaderMtl`'s own compile options), not inferred from the spec:

| case | result |
|---|---|
| tangram header, shader body writes `gl_FragColor` | **PASS** |
| vt header, body writes `glFragColor` | PASS |
| tangram vertex header (`#define attribute in` …) | PASS |
| control — same body, no header | FAIL: `'varying' : Illegal use of reserved word` |
| control — header **minus only the two `gl_FragColor` lines** | FAIL: `'gl_FragColor' : undeclared identifier` |

The macro is applied, not ignored: `gl_FragColor = texture2D(uTex, vUV) * …` translates to
`_uTANGRAM_FragColor = texture(_uuTex, _uvUV) * …`.

So the comment in `GLTileRenderer::createShaderProgram` — *"a name starting with `gl_` cannot be
`#define`d, so that one had to be a real rename"* — **is wrong**. ESSL reserves the `GL_` prefix for
*macro* names; `gl_FragColor` is a built-in *variable*, and in ESSL 3.00 it is not declared at all.
Consequence: the five public GLSL setters (`SkyOptions`, `FogOptions`, `TerrainOptions`,
`CustomRasterTileLayer`, `PostProcessEffect`) need **no migration**, and Phase 3 can adopt tangram's
`shaderSource.cpp` preamble verbatim instead of renaming.

### 0.3 — frame-time A/B: **NOT ANSWERED**

Blocked on hardware, and the plan under-specified it:

- The **EAGL leg is impossible on any Apple-Silicon simulator** — it has no OpenGL ES at all, which
  is why `scripts/ios-dev` links ANGLE in the first place. EAGL vs Metal is a device-only comparison.
- No physical iOS device was attached (`xcrun devicectl list devices` → *No devices found*).
- The leg that *was* in reach — master vs `8ef9aba`, both on Metal — **saturated**: four interleaved
  75 s runs of the scripted pan returned exactly 60.0 fps for both. That is the simulator's vsync
  cap, not a tie. A capped test has no discriminating power and is not evidence of equal cost.

To answer it properly: build both with `MASSIF_FRAME_PROFILER=1 MASSIF_VT_RENDER_STATS=1` so the
per-section CPU/GPU milliseconds are visible *below* the vsync cap, and run on a device at a camera
that is actually GPU-bound. Frame rate against a 60 Hz cap cannot resolve this either way.

### What still needs a physical device

Nothing below has been observed on real Apple hardware. The `arm64` and Catalyst slices are built
and staged, so this is a session with a phone, not more build work.

```sh
cd scripts/ios-dev && PROFILE_RENDER=1 ./bootstrap.sh device
```

| # | Check | Why it needs a device |
|---|---|---|
| 1 | `scripts/ios-dev` runs at all on `arm64` | The device slice has only ever been built, never linked or launched |
| 2 | Startup reports `OpenGL ES 3.0.0 (ANGLE 2.1.0.ec925142edeb)` | Gate 0.1 is simulator-only; confirms the version string on the real backend |
| 3 | The ESSL 3.00 shadow program compiles — `hasShaderVersionFallback()` false, no `_essl3Failed` | It is the only current `ESSL3_FLAG` user, so it is the one existing proof that ANGLE takes a `300 es` program at all. Ignore the `shadow samplers` log field, see below |
| 4 | **Gate 0.3**, the real one — EAGL vs MetalANGLE `8ef9aba` vs master | EAGL does not exist on an Apple Silicon simulator at all |
| 5 | A screenshot at a fixed camera matches the EAGL build | Phase 1's "done when" |
| 6 | Catalyst still builds and runs | The Catalyst slices were rebuilt at master and are otherwise untested |

For (4), follow `scripts/android-dev/bench/README.md`'s discipline rather than a single run:
interleave the builds (A/B/A/B) and take medians over many windows — same-build drift on a device
was 14.6–17.4 fps across one morning. Read `PROF` / `RenderStats` from the device console. The
expected loss, if there is one, is **per-draw** — this renderer's uniform volume, not its shaders —
so the city camera with many small draws is the case to bench, not the terrain.

Bench a **Release** build. `RelWithDebInfo` is plain `-O2 -g`, while Release applies `-Oz` and thin
LTO; a number from the wrong configuration is not the shipped one.

#### `shadow samplers 0` is a red herring

The startup line reports it and it means nothing here. `GLContext` probes
`HasGLExtension("GL_EXT_shadow_samplers")`, which is an **ES 2.0** extension: in ES 3.0
`sampler2DShadow` and depth comparison are core, so a driver has no reason to export the old string
on an ES 3.0 context, and most do not.

`GLContext::SHADOW_SAMPLERS` is then **never read** — it is logged and nothing else. Hardware PCF is
gated on `_depthTextureMode && GLContext::ES3` in `TerrainShadowMap::create`, both true on the
simulator run. The probe is dead code and a deletion candidate for Phase 2, which already collapses
the `GLContext` extension probes; the log field should go with it or be re-pointed at
`GLContext::ES3`.

## What ES 3.0 actually buys

Nothing lands from the version bump itself. The wins are the work it unblocks:

- **Instancing** — billboards, markers, celestial objects; one draw per batch instead of per quad.
- **Texture arrays** — shadow cascades are a `_size * _cascades` wide atlas today; an array removes
  the manual slice offsetting.
- **`glMapBufferRange` + orphaning** — label vertex streaming, currently the dominant 2D LINE cost.
- **Packed vertex attributes** (`GL_INT_2_10_10_10_REV`, normalized shorts) — vertex buffers roughly
  halve, and `MAX_VERTEXBUFFER_SIZE = 65535` with its 16-bit index cap can lift.
- **Uniform buffer objects** — the per-draw uniform storm in `GLTileRenderer`.
- **Core, no extension probe**: VAOs, `fwidth`, `sampler2DShadow`, `glInvalidateFramebuffer`,
  `glTexStorage2D`, MRT, `textureLod` in fragment shaders, guaranteed ETC2.

Caveat: tangram-ng is GLES-2-baseline, so unlike most of this documentation set, these have no
reference implementation to copy from.

## The plan

### Phase 0 — decision gates

Three measurements, no production code. Results are in [Phase 0 — results](#phase-0--results):
0.1 pass, 0.2 answered (not breaking), 0.3 unanswered.

**Gate**: if (1) fails or (3) is bad, this plan stops and the native question reopens. (1) passed.
(3) is still open, so the native question is deferred rather than closed — but on the evidence of
0.1 nothing blocks Phase 1 starting.

### Phase 1 — ANGLE is the Apple graphics layer

- Re-vendor MetalANGLE master `ec925142e` into `massif-maps/angle-metal`. Slices: ios-arm64,
  ios-sim-arm64, catalyst arm64 + x86_64. Drop armv7 and i386. Carry the one fork patch on
  `MGLContext.h` (see [`angle.md`](../../maintenance/angle.md)).
- MGLKit survives this phase — that is what choosing the fork bought. The EGL + `CAMetalLayer`
  bootstrap moves to Phase 5, where Win32 and AppKit force it anyway.
- Delete: the EAGL path, `--use-metalangle` (unconditional now), the `ios/glwrapper` OpenGLES
  fallbacks, and Xamarin (unmaintained; it is also the only binding that blocks an ANGLE-only iOS).

**Done when**: `scripts/ios-dev` runs on device and simulator, Catalyst builds, and a screenshot at
a fixed camera matches the EAGL build.

### Phase 2 — ES 3.0 baseline, ES 2.0 dropped

Done, except the device check. Config plus dead-code removal; no shader work — an ES 3.0 context
compiles `#version 100` shaders, so the shaders move separately in Phase 3.

Contexts, no probes and no fallbacks left: Android manifests to `0x00030000` and
`setEGLContextClientVersion(3)` in both `MapView` and `TextureMapView` (each carried its own copy of
the `reqGlEsVersion` probe), `ConfigChooser` down to one ES3-renderable table, iOS `MapView.mm` with
no ES2 retry, UWP `EGL_CONTEXT_CLIENT_VERSION` 3, and the Xamarin binding raised to match — leaving
it at 2 would have handed the native side a context it now assumes it will never see.

What the capability flags became:

| Was | Now |
|---|---|
| `GLContext::ES3`, `DEPTH_TEXTURE`, `PACKED_DEPTH_STENCIL`, `TEXTURE_NPOT_*`, `DISCARD_FRAMEBUFFER` | gone — all ES 3.0 core |
| `GLContext::SHADOW_SAMPLERS` | gone — it was never read (see [above](#shadow-samplers-0-is-a-red-herring)) |
| `GLContext::DiscardFramebufferEXT` + its `eglGetProcAddress` | `InvalidateFramebuffer` → `glInvalidateFramebuffer` |
| `vt` VAO wrappers + `GL_OES_vertex_array_object` probe | `glGenVertexArrays` / `glBindVertexArray` / `glDeleteVertexArrays` |
| `GL_OES_standard_derivatives` probe | always on |
| `GL_DEPTH_COMPONENT24_OES`, `GL_DEPTH24_STENCIL8_OES`, `GL_TEXTURE_COMPARE_MODE_EXT` | the unsuffixed core tokens |
| `GLES2/gl2.h` | `GLES3/gl3.h` (`gl2ext.h` stays — extension tokens still live there) |

What is left of the probes is one flag, `TEXTURE_FILTER_ANISOTROPIC`, which is an extension in every
ES version. `GLContext::VERSION` replaces the booleans, parsed the way tangram parses it
(`Hardware::loadCapabilities`): first digit run of `GL_VERSION` times 100, so `300`. It gates nothing
today — it is there for logging and for a future 3.1/3.2 check.

Two traps, both of which a `-fsyntax-only` check passes straight through:

- **Android must link `GLESv3`, not `GLESv2`.** The ES 3.0 entry points are only exported by
  `libGLESv3.so`; with `GLESv2` the compile succeeds and the *link* fails on `glGenVertexArrays`,
  `glInvalidateFramebuffer` and friends. It is a superset, so it replaces `GLESv2` rather than
  joining it.
- **`vt`'s derivatives flag still has to emit the `#extension` line.** `fwidth` is core on an ES 3.0
  *context*, but the shaders are GLSL ES 1.00 until Phase 3, and in ESSL 1.00 it is still an
  extension. `commonFsh` keeps `#extension GL_OES_standard_derivatives : enable` under
  `!defined(ESSL3)`; only the runtime probe went away.

`vt::GLExtensions` survives as a near-empty class holding the anisotropic probe. Deleting it would
change `GLTileRenderer`'s constructor, which is beyond this phase — fold it in whenever that
signature next changes.

Devices lost: pre-2013 GPUs (Mali-400, Adreno 200/305, Tegra 3, PowerVR SGX). At minSdk 21 and an
iOS 13 floor — where every device is A7+ — that is a rounding error. The app-facing consequence is
in [migration.md](../../migration.md#opengl-es-30-is-required).

**Done when**: an Adreno 610 device and one iOS device show no regression at the bench cameras.
The Adreno 610 leg is done — see [Phase 3](#phase-3--glsl-es-300), which was verified on the same
device and run, and exercises this phase's context and capability changes on the way. An iOS device
is still owed.

### Phase 3 — GLSL ES 3.00

Done on the simulator; the device leg is still owed. tangram's preamble
(`core/src/gl/shaderSource.cpp`) is ported verbatim into three places, and **not one of the 27
shader literals was edited** — they stay written in ESSL 1.00 and are translated on the way to the
driver, which is what keeps application GLSL working unchanged:

| Where | How |
|---|---|
| `all/native` | `Shader::TranslateToESSL3` rewrites the source in `LoadShader` |
| `vt` | `buildShaderProgram` ORs in `ESSL3_FLAG` for every program, not at the 20-odd call sites |
| `nml` | `GLResourceManager::createShader` gained a shader-type parameter so it can pick the right header |

Because the app's GLSL is spliced *into* the SDK's own literals, one translation point covers all
five public setters. `vt`'s per-program 1.00 fallback stays one release as a canary.

**`hasShaderVersionFallback()` had no callers.** vt sets `_essl3Failed` with the comment "the owner
logs it once", and no owner ever did — so the phase's own success criterion was unobservable.
`TileRenderer::onDrawFrame` now logs it once. Without that, a program silently rebuilt at 1.00 looks
exactly like success: the map still draws.

#### `#version` must be on the first line, not merely preceded by whitespace

The one real trap, and it failed *every* shader on the first run:

```
ERROR: 0:2: '' : #version directive must occur on the first line of the shader
```

The shader literals are raw strings that open with a newline and an indent, so replacing the
`#version 100` line in place leaves the new directive on line 2. Whitespace on the *same* line is
allowed; a preceding newline is not. The directive is emitted at offset 0 and whatever preceded it
is pushed after the header.

Worth noting how this was caught: the Android build was **green**, because a C++ build never
compiles a shader — that happens at runtime. Only running it found this.

**Done when**: every program compiles at `300 es` on both device families and
`hasShaderVersionFallback()` returns false.

Measured at lon 5.7606 / lat 45.2442 / z13.6 / tilt 55, terrain on, on two unrelated GL stacks:

| | iPhone 16 Pro simulator (ANGLE/Metal) | Crosscall HLTE556N, **Adreno 610**, ES 3.2 |
|---|---|---|
| shader compile / link failures | 0 | **0** |
| ESSL 3.00 → 1.00 fallbacks | 0 | **0** |
| GL errors | 0 | **0** |
| frame vs the ESSL 1.00 build | every map band 0.00% | see below |

The Adreno is the one that matters, because its driver is a real Qualcomm GLSL compiler rather than
ANGLE's translator, and it is where `GLContext::VERSION` first met a vendor version string —
`OpenGL ES 3.2 V@0502.0 (GIT@...)` parses to `320`, as tangram's parser is meant to.

The device A/B needs its control quoted or it says nothing. Same build run twice differs by 0.19%
of sampled pixels (labels are placed as tiles arrive, so no two runs match exactly). ESSL 1.00 vs
3.00 differs by 0.37%, and the per-band profile is the same shape — the excess is confined to
glyph pixels, black text against landcover green in both directions, i.e. sub-pixel label
antialiasing. Cropped and compared by eye, the two frames are indistinguishable: same contours, same
labels, same positions.

One thing the A/B settled that inspection alone would not: a **route line that draws in broken
chunks** over terrain is present *identically in both builds*. It is the known open terrain
line-following issue, not a regression from this phase.

Still owed: a real iOS device (Apple's driver, not the simulator's).

#### The `gl_FragColor` disagreement — settled, tangram was right

The two implementations contradicted each other and the answer decided whether this phase breaks the
public API.

- **tangram** does `#define gl_FragColor TANGRAM_FragColor` plus a `layout(location = 0) out`
  declaration. App shaders keep writing `gl_FragColor` and need no migration.
- **`vt`** renames instead, and its comment claims a name starting with `gl_` cannot be `#define`d.

Gate 0.2 put both through ANGLE's translator: **the macro is accepted and applied**. vt's comment is
wrong — ESSL reserves the `GL_` prefix for *macro* names, while `gl_FragColor` is a built-in
*variable* that ESSL 3.00 does not declare at all. So Phase 3 adopts tangram's preamble verbatim,
the five public GLSL setters need no migration, and this phase is **not** a breaking change.

vt's rename is still harmless and need not be undone; only the comment is misleading, and only the
`all/native` side has to choose. Full method and controls in [Phase 0 — results](#02--define-gl_fragcolor-accepted-so-phase-3-is-not-breaking).

### Phase 4 — harvest: closed, nothing shipped

Closed 2026-08-19 (#140). Three of the five items were implemented and measured on an Adreno 610;
all three were negative, and the two best remaining performance targets do not need ES 3.0 at all.
Method and numbers in [round 18](../performance-log.md#18-phase-4-opened-by-measuring-first-and-the-first-two-items-died-2026-08-19).

| Item | Result |
|---|---|
| 1. Instancing (billboards, markers) | **0.1 ms CPU, 0.0 ms GPU.** Already one draw per batch, so the premise was false |
| 2. Shadow cascades as a texture array | Implemented and correct, then **+28.5% GPU** (`shadowMask` 6.70 → 10.30 ms). Reverted. Explicitly a **per-GPU** verdict |
| 3. Packed vertex attributes | Not measured. Cannot cut draws — they come from tile × style layer, not the 16-bit index cap |
| 4. UBOs | Not measured. Targets `styleUpload` ≈ **0.46 ms/frame** once the profiler's own per-bucket overhead is subtracted |
| 5. `glMapBufferRange` for label streaming | **No-op** — the six label buffers are a few KB each. Reverted |

The list was written from what ES 3.0 *offers* rather than from what this renderer *spends*, and
ordered by expected payoff before anything was measured. The exercise re-run the other way round is
[Second harvest pass](#second-harvest-pass-2026-08-26). There are two cameras with two bottlenecks
and it addresses neither: the 2D city is CPU-bound on **320 geometry draws/frame**, and the terrain
frame is GPU-bound with **55% of it in the shadow pass** — a cost that is casters × cascades, which
item 2 could not touch however the texture is laid out.

What took its place, neither of which depends on this migration:

- **[#144](https://github.com/massif-maps/MassifMaps/issues/144)** — cut the 320 draws. Step 1 hoists
  the per-layer uniforms out of the per-tile loop (~1.5 ms/frame); the loop is *already* grouped by
  style layer and re-uploads identical style parameters per tile.
- **Instancing the shared-mesh per-tile draws** — tile masks (62/frame, measured at ~2.4 ms CPU),
  background quads (23/frame) and the terrain grid all bind one shared VBO and differ only by
  `U_MVPMATRIX`. This is where item 1 should have pointed. Instanced arrays are also an ES 2.0
  extension.

**This does not undo Phases 2 and 3.** They were never justified by frame rate — they are what makes
ANGLE-on-Metal and the desktop phase possible. On Android specifically, Phase 2 bought *simplicity,
not capability*: the bench device already exposed `GL_OES_vertex_array_object`,
`GL_EXT_discard_framebuffer`, `GL_OES_depth_texture` and `GL_OES_texture_npot`, so every capability
made core was already reachable through the probes that were deleted. The migration's payoff is in
Phases 1 and 5, and it should be judged there.

### Phase 5 — desktop

No renderer change, but this is where the MetalANGLE choice is paid for: the fork has no Win32/D3D
and no AppKit slice, so this phase needs upstream ANGLE *and* the EGL + `CAMetalLayer` bootstrap
that Phase 1 no longer writes.

- **macOS native (AppKit)** — upstream ANGLE on Metal, plus the shared EGL bootstrap.
- **Windows** — ANGLE on D3D11, reusing the UWP EGL wrapper shape.
- **Linux** — native EGL against Mesa's GLES 3.2. Pull in ANGLE-on-Vulkan only if a driver forces it.

#### What the ES 3.0 baseline costs on desktop

Almost nothing, and it settles the Windows backend question. ANGLE's D3D11 backend caps the ES
version by feature level (`GetMaximumClientVersion` in `renderer11_utils.cpp`; `Renderer11.cpp`
puts it plainly — *"Can't support ES3 at all without feature level 10.1"*):

| D3D feature level | max ES |
|---|---|
| 11_0 / 11_1 | 3.1 |
| **10_1** | **3.0** |
| 10_0 and below | 2.0 |

So the Windows floor is **FL 10_1**: Sandy Bridge (2011) and AMD HD 3000 (2007) clear it, NVIDIA
GeForce 8/9 (10_0) and pre-2011 Intel GMA (9_x) do not. Windows 11 already requires a DX12-capable
GPU, so it excludes nobody there, and WARP — what VMs and RDP sessions fall back to — reports 11_1.

That is the argument for **D3D11 over Vulkan** on Windows, which this plan previously assumed on
maturity grounds alone: ANGLE-on-Vulkan needs a 2016-or-later driver, so it is *narrower* in the
tail than D3D11's 2011 floor. Linux and macOS have no equivalent cut — Mesa serves GLES 3.x on
anything post-2012 (llvmpipe does 3.2 in software), and the macOS floor is "Metal-capable", which
macOS 10.14+ requires anyway.

## Second harvest pass (2026-08-26)

Phase 4 closed on a method error, not on a verdict about ES 3.0: it listed what the version
*offers* and ordered by expected payoff before measuring. This pass inverts that — it starts from
the costs already written down in this documentation set and asks which of them ES 3.0 can reach.

**Nothing below is measured.** The ordering is by *which recorded number an item attacks*, not by
expected size, and every one of them needs its own Adreno 610 A/B before it becomes work. The
tracking issue is [#189](https://github.com/massif-maps/MassifMaps/issues/189).

### 1. `GL_PIXEL_PACK_BUFFER` + `glFenceSync` for the occlusion read-back

The largest recorded stall in the tree that ES 3.0 addresses. `TerrainRenderer.h` records the
`glReadPixels` occlusion read-back at **55-62 ms on an Adreno 610, peaking at 134 ms** — a full
pipeline stall on top of the ~20 ms depth render. Two workarounds already pay for it: a second GL
context (`TerrainDepthWorker`) and a 500 ms throttle while the camera moves, and the throttle is
what makes occlusion lag a gesture.

A pixel-pack buffer plus a fence turns the read-back into "poll last frame's buffer, take it when
the fence signals". Both are ES 3.0 core, neither needs the second context to go away.

**What is needed**: a PBO ring in `TerrainDepthWorker::readPixels` (`TerrainDepthWorker.cpp`, the
`glReadPixels` at the end of the job) and in `MapRenderer::captureRendering`; a `glFenceSync` +
`glClientWaitSync(0)` poll instead of the blocking read; and the throttle constants in
`TerrainRenderer.h` (`DEPTH_READBACK_THROTTLE`, `DEPTH_READBACK_MOVING_INTERVAL`,
`DEPTH_SUBMIT_MOVING_INTERVAL`) re-derived once the stall is gone — they are the measurement of the
problem, so they are also the verification.
**Verified when**: the terrain camera holds its frame rate with the moving interval lowered toward
one read-back per frame, at the same mesh resolution the 13.3/14.3/14.9 fps table was taken at.

### 2. Uniform buffer objects for the per-draw style storm

Phase 4 listed UBOs and priced them against `styleUpload ≈ 0.46 ms/frame`. That is the wrong
bucket. The cost is in `GLTileRenderer::useProgram`'s own note: per-draw setup — everything before
`glDrawElements` — is **24-31 µs against 10-12 µs for the draw itself, at 250-560 draws a frame**.

What that setup is, for a 2D geometry draw: `U_COLORTABLE`, `U_WIDTHTABLE`, `U_OFFSETTABLE`,
`U_STROKESCALETABLE` and `U_PATTERNTABLE`, each `TileGeometry::StyleParameters::MAX_PARAMETERS`
wide, uploaded per *tile* — although the draw loop is already style-layer-major, so every tile of a
layer sends the same values. Vertex layout is not the problem; `bindGeometryVertexLayout` already
takes the VAO path.

This is the same target as [#144](https://github.com/massif-maps/MassifMaps/issues/144) step 1
(hoist the per-layer uniforms out of the per-tile loop, ~1.5 ms/frame estimated). A UBO is the
stronger form of that hoist: one `glBindBufferRange` per style layer, no dirty tracking, and the
block survives a program switch.

**What is needed**: a `std::uniform` block in the 2D geometry shaders
(`GLTileRendererShaders.h`), a per-style-layer buffer built where `StyleParameters` is resolved,
and `glBindBufferRange` at the point the layer loop changes layer in `renderGeometry2D`. ESSL 3.00
is already the compiled form, so no shader-version work.
**Verified when**: `geomStyleNs` (already in `RenderStats`) drops at the city camera and the frame
rate moves with it. Do #144 step 1 first if it is cheaper — they are alternatives, not a sequence.

### 3. 32-bit indices

`GLTileRenderer::renderLabels` flushes the label batch at `_labelVertices.size() >= 32768` for no
reason other than the 16-bit index cap. `GL_UNSIGNED_INT` indices are ES 3.0 core (they needed
`OES_element_index_uint` on ES 2.0), so the flush can go.

**What is needed**: `_labelIndices` to `std::uint32_t`, the `glDrawElements` type at the label draw,
and the flush condition deleted. `VertexArray<std::uint16_t>` appears in the `Label` build
signatures too, so the change reaches `Label.h`.
**Verified when**: flushes per frame at the city camera go to one, and the label pass time does not
regress — the batch gets bigger, so this is a trade, not a free win.

### 4. Vertex buffers for the vector-element renderers

`BillboardRenderer`, `PointRenderer`, `LineRenderer`, `PolygonRenderer`, `Polygon3DRenderer` and
`CelestialRenderer` contain **no `glGenBuffers` at all** — every frame passes `indexBuf.data()`
straight to `glDrawElements` from client memory, so the driver copies the whole vertex set per
draw and the attribute pointers are re-specified with it.

Not an ES 3.0 feature — ES 2.0 had VBOs — but it belongs on this list for one reason: **a bound VAO
makes client-side arrays illegal**, so the day these renderers gain a VAO they must gain buffers in
the same change. ES 3.0 is what makes the streaming side clean (orphaning, `glBufferSubData` into a
ring).

**What is needed**: per-renderer VBO + IBO + VAO, orphan-on-write streaming, and the client arrays
deleted. Six files, each independent.
**Verified when**: an app with many vector elements (the demo's `elements` layer) holds frame rate
with more of them. This only matters where those layers are used, so measure there, not at the
terrain camera.

### 5. Immutable and sized texture storage

`glTexStorage2D` appears **nowhere** in the tree, and `GL_LUMINANCE` / `GL_LUMINANCE_ALPHA` are
still in use (`GLTileRenderer::buildCompiledBitmap`, and `Bitmap::ColorFormat`'s `GRAYSCALE` /
`GRAYSCALE_ALPHA`, which are those two tokens spelled numerically).

ES 3.0 gives `GL_R8` / `GL_RG8` plus `GL_TEXTURE_SWIZZLE_R/G/B/A`, which reproduces luminance and
luminance-alpha sampling with no shader change, and `glTexStorage2D`, which lets the driver skip
per-level validation, pick a layout once, and makes NPOT mip completeness a non-question.

**What is needed**: the format swap first (it is the prerequisite — unsized formats cannot be used
with immutable storage), then `glTexStorage2D` + `glTexSubImage2D` at the drape cache, the tile
bitmap path and the shadow map.
**Verified when**: no visual diff at the bench cameras, and the glyph atlas still samples
identically — the swizzle is where this breaks if it breaks.

### 6. ETC2 for style bitmaps

No compressed texture format is used anywhere: no `glCompressedTexImage2D`, no ETC2, no ASTC. ES
3.0 guarantees ETC2/EAC on every device, which is what makes this worth raising now — on ES 2.0 it
needed a per-device probe and a fallback.

Style bitmap atlases and pattern textures upload as RGBA8; ETC2 is 4:1 on both memory and sampling
bandwidth. The cost is an encode path, so this is a project rather than a patch, and it should be
priced against [`build-and-size.md`](../build-and-size.md) rather than against frame rate.

### 7. MSAA renderbuffers and `glBlitFramebuffer`

`glRenderbufferStorageMultisample` and `glBlitFramebuffer` are ES 3.0 core and neither is used.
This is a **feature**, not a performance item: antialiased edges on the drape and the other
offscreen targets without supersampling them. `glBlitFramebuffer` is also the sanctioned
replacement for the hand-rolled depth-tested blit that froze the screen during the peak-finder work.

### Checked this pass and not proposed

| Idea | Why not |
|---|---|
| `GL_OVR_multiview2` for the 3 shadow cascades | On a tiler it is implemented as view-instancing, so the vertex work is unchanged and it saves draw *submission* only. The cascades are already snapped and cached, and the pass is 55% of the terrain frame because of casters × cascades — the same thing that killed Phase 4 item 2 |
| `gl_VertexID` fullscreen quads | Removes a VBO bind on ~6 draws a frame across `SkyRenderer`, `MapRenderer` and `GLTileRenderer`. Cleanup, not performance |
| Packed vertex attributes (Phase 4 item 3) | Parked for the right reason and it still holds: draws come from tile × style layer, not the index cap. Item 3 above is the load-bearing part of it |
| Instancing the shared-mesh per-tile draws | Still the best draw-count target (62 tile masks ≈ 2.4 ms CPU, 23 background quads), still open — but it is already recorded under [Phase 4](#phase-4--harvest-closed-nothing-shipped) and it is an ES 2.0 extension, so it is not a finding of this pass |

## What this plan deliberately does not do

**No graphics abstraction layer.** MapLibre needed one because they were adding real backends; we
are not. Building one speculatively buys nothing until a native backend exists. What is worth
adopting is the cheaper discipline: new GL calls go through the `renderers/utils/` wrappers, not
into logic files. Retrofitting the 898 already-scattered calls in `all/native` is its own priced
job and is not part of this.

**No native Metal.** Reopen only if the gate 0.3 bench shows ANGLE costing real frames at the city
camera — and price it against the table above, including the loss of tangram as a reference. That
bench has not been run, so this is deferred, not decided.

## Known gaps

- **Gate 0.3 is unmeasured.** Whether ANGLE adds per-draw overhead versus EAGL is the one result
  that could send this back to a native backend, and it needs a physical iOS device: the
  Apple-Silicon simulator has no OpenGL ES to compare against, and its 60 Hz vsync cap hides any
  delta the scene does not already exceed. Use `MASSIF_FRAME_PROFILER=1` per-section timings, not
  frame rate.
- Every number in "Where we are" is from static analysis of the tree, unchanged since.
- ES 3.0 on the Metal backend is confirmed **on the simulator only** (gate 0.1). No device run yet,
  and the fork's own README grades its ES 3.0 at 90%.
- `GLContext::SHADOW_SAMPLERS` is dead — probed from an ES 2.0 extension string, logged, never read.
  Phase 2 should delete it along with the other extension probes.
- The linked (as opposed to static-slice) binary-size delta is still unmeasured. The stripped
  arm64-simulator slice is 14.2 MB at master, 15.2 MB at the vendored 2021 build.
- Xamarin is assumed droppable. If it is not, it blocks an ANGLE-only iOS on its own.
- Whether MetalANGLE's two unimplemented ES 3.0 features (primitive restart, last-provoking-vertex
  flat shading) matter to this renderer has not been checked against the draw calls it makes.
- No decision on whether Windows should use ANGLE-on-D3D11 or ANGLE-on-Vulkan; D3D11 is assumed on
  maturity grounds only.
