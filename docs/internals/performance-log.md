---
title: Performance lab notebook
description: Dated measurement rounds, dead ends and numbers behind the current render design.
sidebar_position: 4
---

# 3D terrain render performance — measurements, design differences, next steps

:::warning This is a lab notebook, not the design
Dated rounds, raw numbers and dead ends, kept verbatim because the dead ends are the expensive part.
For **how the renderer works today**, read [Render pipeline](/docs/internals/rendering/); for the
current method and hot list, [Performance](/docs/internals/rendering/performance). Anything here may
have been superseded by a later round on this page.
:::

Working document for the `perf/terrain-render` branch. It records what was **measured** (with the
method, so the numbers can be reproduced or refuted), how our renderer differs from
[tangram-ng](https://github.com/farfromrefug/tangram-ng) — the reference implementation we compare
against, because it renders the same data sharply, smoothly and with no see-through — and what is
worth doing next.

This file is the **lab notebook**: dated rounds, dead ends, and the numbers with the method that
produced them. The **current design**, split by subsystem so a reader can open one page and stop, is
in [`docs/internals/rendering/`](rendering/index.mdx) — start there to understand how the renderer works, come
here to find out what was already tried and measured.

When a design decision is made or reversed, record the evidence here and update the matching page
under `rendering/`.

---

## 0. Next session: start here

> ### THE RULE — tangram-ng is the reference, and we COPY it
>
> [tangram-ng](https://github.com/farfromrefug/tangram-ng) renders this data sharply, on the same devices, with no
> see-through. Where it does something differently, adopt its way; do not design an alternative and
> compare. **Copy its constants — every one this branch derived instead was wrong**: `depth_shift`
> scaled by the projection (theirs is a flat `0.02`), a proxy push of 8 (theirs is 1 per level, ×48
> for the terrain raster), an ordinal stride of 32 (theirs is the dense style-layer order).
> **And port each piece whole**: half of their depth model is worse than none of it, three times
> over (§10.6). Read the SCENE files, not only the shaders — `polygon.vs` sets `depth_shift = 0.0`
> "to allow blocks to modify", and the value that matters is in `res/scenes/terrain-3d.yaml`.
>
> The full model now landed is §10. What it replaced: the RTT drape, the per-layer depth pre-pass,
> the stencil tile masks, and all terrain subdivision of content.

**Where it stands.** The drape-free path is the demo default (`--es drape true` brings the drape
back for an A/B). On the emulator and on Martin's device the two long-standing symptoms — roads
snapping straight on zoom-out, and content see-through on mountains — are **fixed**, as is the
terrain poking through contours. The SDK option `TerrainOptions.DrapeFillsEnabled` still defaults to
the drape, so no app changes behaviour until it opts in.

**Next, in order** (rewritten after the render-thread profile of §12 — several older entries are
now measured to be dead ends, see §11.4 and §12.1):

0. **Re-take the numbers that matter now the build is optimized** (§12.6). The bench APK was
   compiled at `-O0` all along; `-O2` is +49% on the north pan, and it moves the frame from
   CPU-bound to waiting on the GPU. The tangram head-to-head (§11.2) has to be re-run before it
   means anything.
1. ~~**Stop rebuilding tile surfaces on the render thread**~~ — **measured not to happen at all**
   in the shipped configuration, see §12.1. What the render thread actually spends is in §12.
2. **Take the next two items off the render thread** (§12.4): `HillshadeRasterTileLayer::onDrawFrame`
   (10.5% of it) and `TerrainRenderer::updateDepthBuffer`, which spends 7.7% of the render thread
   *destroying* the previous depth buffer's CPU meshes there.
3. **Port `ContourTextStyleBuilder`** (§11.3). Contour lines are already a fragment block and cost
   nothing; the labels still drag in a whole contour tile set that tangram does not have.
4. **Attribute the rest of the layer pass.** `geometry` is a steady 6–9 ms per layer per frame and
   the per-draw counters explain ~7 ms of a 39–235 ms block — the remainder is still unmapped.
   Profile it, do not reason about it (§12.2).
5. **Shadows on the shared ground.** Wired and lit, but the caster pass is switched off there
   (`applyTerrainShadows(..., castShadows = false, ...)`) because the map reads as acne instead of
   cast shadows. The drape path is clean on the same scene — diff against it. §10.5.
6. **Delete the drape.** It now has the baseline it was waiting for.

Do **not** re-run the dead ends in §6 and §10.6 — they are measured.

---

## 1. How to measure (do this before believing anything)

Build the demo with the profilers on and read `PROF` / `PROF GPU` / `RenderStats` from logcat:

```sh
cd scripts/android-dev && ./gradlew :app:assembleDebug -x lint -PprofileRender
```

The native SDK is built **optimized by default** now, from the command line and from Android Studio
alike (`-PnativeOpt=false` goes back to `-O0` for stepping through native code). It was not: the
debug variant sets no `CMAKE_BUILD_TYPE` for the native side, so every number in this document that
predates §12.6 was taken at `-O0`, and the same code at `-O2` is 49% faster.

- `PROF` — CPU ms per frame section: `sky prelude prepare cover drape layers layers3D billboards`.
  `sky` is mostly the swap-buffer wait, not work.
- `PROF GPU` — the same sections timed on the GPU (`GL_EXT_disjoint_timer_query`, Android only).
  Switch off with `adb shell setprop debug.massif.gputimer 0`.
- `RenderStats` — draw counts, index counts, per-draw µs, surface/label/drape counters.

**Device numbers drift.** On the Crosscall (Adreno 610) the *same build* measured 14.6-17.4 fps
across a morning. Only **interleaved** A/B is trustworthy: build two APKs, alternate them, take
medians over ≥40 one-second windows. Helper scripts used for the numbers below:

They live in [`scripts/android-dev/bench/`](https://github.com/massif-maps/MassifMaps/tree/master/scripts/android-dev/bench) — `ab.sh`,
`ab2.sh` (mountain camera), `north.sh` (the slow case), `abapk.sh` / `abprop.sh` (interleaved A/B by
APK or by system property), `startup2.sh`, `absum.py` (median summary, discards idle windows).

**`bench/ab2.sh` passes `--es base plain`, and the `#hillshade` slot only exists inside the
COMPOSITE base layer** - so a hillshade run started from it silently measures a frame with no
hillshade in it. Add `--es base composite` for anything involving the hillshade or the satellite
slot. (Cost one whole device A/B round before Martin spotted the missing shading on screen.)
**Tilt is 90 = straight down**, so a "tilted" camera for occlusion work is t=20, not t=55: at t=55
the view is close to plan and no ridge occludes anything.

**The camera decides what you measure.** The demo default is Grenoble **city, z16.22, tilt 26** —
content-heavy. Panning *east* crosses the flat valley and is cheap; panning *north* climbs into the
mountains and is the case that gets slow (`--es animLatDelta 0.06`). Tilt matters just as much: at
tilt 85 most of the screen is sky.

| camera / motion | fps | frame | render tiles / frame |
|---|---|---|---|
| east over the valley, light config, mesh 64 | ~20 | 50 ms | 21 |
| mountain camera (45.244172/5.760595 z13.2 t55) | 26 | 32 ms | — |
| tilt 0 vs tilt 85, same camera | 51 ms vs 25 ms | | 21 vs 6 |
| **north into the mountains, contours + hillshade** | **6.8** | **131 ms** | **~130** |

---

## 2. What landed, with the measured effect

All on `perf/terrain-render` (main repo) and the matching `libs-massif` branch.

| change | commit | effect |
|---|---|---|
| GPU timer queries (`PROF GPU`) | `ff422a128` | tool |
| Occlusion depth read-back on its own thread + GL context | `d02002dda` | 13.7 → 14.9 fps |
| Demo terrain mesh 128 → 64 (tangram's value) | `8cd585887` | 8.5 → 15.2 fps |
| Draped lines cut at the surface lattice, not halved 3× finer | `0112fad65` | 16.7 → 18.1 fps, output unchanged |
| Tile decode pool knob (`--es tilePool N`) | `142e8095d` | no effect — see §5 |
| Switches to measure the tangram content model | `005998a37` + libs-massif `9c9319a` | 18.0 → 19.3 fps when enabled |
| Scripted pan can move north (`--es animLatDelta`) | `83eabcbcc` | benching tool for the slow case |
| Hillshade shades the terrain DEM instead of a tile set of its own | §9 | render tiles −22%, surface draws −16% (emulator counters; device bench pending) |

Cumulative at the demo camera: **16.7 → 20.3 fps (+22%)** with the optional switches on.

### 2.1 Occlusion depth on a worker thread

`glReadPixels` is a pipeline stall (55-62 ms measured). It now runs on a worker thread with its own
EGL context; the render thread only collects meshes (0.8 ms). The context is deliberately **not
shared** — the depth pass draws CPU meshes from client memory with its own program and FBO, so a job
just holds `shared_ptr`s and nothing crosses contexts.

Two GL contexts still share one GPU, and on this Adreno the per-job context switch is the real cost,
so the submit interval matters: every frame → 13.2 fps, 250 ms → 14.3, **500 ms → 14.9** (against
13.7 synchronous). Uploading the meshes into worker-side VBOs changed nothing (tried, reverted).

### 2.2 Lattice line splitting

Regular-grid mode used to subdivide draped lines to 0.35 of a grid cell so no segment chorded across
a cell's diagonal fold and sank below the surface under the zero-slack painter-order depth test.

Now each segment is cut exactly where it crosses `x = k·cell`, `y = k·cell`, `x + y = k·cell` (tile
uv space — the surface builder emits `y = 1 - v`, so the shader's `fg.x + fg.y = 1` fold reads as
`u + v` here). Every sub-segment then lies inside **one** surface triangle: exact instead of
approximate, with fewer vertices. Device screenshot diff at the ridge camera: 0.09% — unchanged.

**Do not** instead add depth slack for lines in painter-order mode: a forward clip bias there is
what leaks over ridges at range (`GLTileRenderer.cpp:2784`, and the rounds 45-56 history).

---

## 3. Where we differ from tangram-ng

Verified in their source, not assumed.

| | tangram-ng | us |
|---|---|---|
| **Draping** | none — vector geometry is displaced per vertex (`res/scenes/terrain-3d.yaml`: `position.z += TERRAIN_SCALE * getElevation()`, one `texture2D` fetch) | fills baked into per-tile RTT drape textures (1024² default), then the surface is textured |
| **Terrain surface** | ONE shared static 64-grid VBO for every tile (`rasterStyle.cpp:61`, `vec2 GL_SHORT`), per-tile uniforms only | same shared-grid design (`buildCompiledTerrainGridSurfaces`), resolution = `meshResolution` |
| **Content subdivision** | none at all | lines cut at the lattice (§2.2); fills subdivided to one cell |
| **Content vs surface depth** | constant clip-space pull: `gl_Position.z += (proxy - layer) * (2⁻¹⁹·w + depth_shift)`, `depth_shift = -0.02·u_proj[2][3]` | content follows the surface exactly, no bias (painter-order) |
| **Hillshade / contours / hypsometric** | fragment-shader blocks on the *same* terrain raster draw (`res/scenes/hillshade.yaml`) | separate tile layers, each with its own tile set, surface pass and stencil mask |
| **Tile LOD** | subdivide while screen area > `(2·pixelScale·256)²` (`tileManager.cpp:214,231`) — ~920 px edge on this phone | distance rule `zoomDistance < SUBDIVISION_THRESHOLD·√2`, ~256 px tiles → one zoom level finer |
| **Elevation texture** | raster bound directly, ancestor sampled through uv offsets; edges clamped, extrapolated in-shader | per-tile CPU re-encode with a 1-texel border from up to 8 neighbour grids, re-uploaded when any neighbour changes |
| **Terrain depth read-back** | worker thread, shared context, half res, never waited on | same now (§2.1) |
| **Stencil tile masks** | none | one full grid draw per tile per layer |
| **Tile decode threads** | 2 (`SceneOptions::numTileWorkers`) | 1 (`Options::setTileThreadPoolSize`) |

### 3.1 The depth shift, and why theirs is safe

`depth_shift` is a **constant clip-space** offset, so its NDC effect is `0.02/w`: strong near the
camera — where an un-subdivided segment chords furthest below the surface — and vanishing at range.
That is structurally different from a constant-**NDC** bias, whose eye tolerance grows as
distance²/near and which is what produced the see-through in rounds 45-56.

Our shader already has the term (`uLayerDepthOffset * (2⁻¹⁹·w + uDepthShift)`); we feed it 0.
Measurable with `adb shell setprop debug.massif.depthshift 0.02` plus
`debug.massif.linesourcedensity 1` (lines at source density): **18.0 → 19.3 fps, 2.7× fewer content
indices per render tile**, and no visible difference at the ridge camera beyond label placement.

**VERDICT (Martin, on device): rejected as a default — "a bit of see-through, on ridges I see bits of
contour lines from the other side".** Which is the expected worst case: contours lie exactly ON the
surface, so any pull towards the viewer lifts the far side of a crest over the near side.

The important conclusion is *why tangram does not have this problem*, and it is not the depth shift:
**their contours are not geometry.** `res/scenes/hillshade.yaml` computes hillshade, contour lines
and the hypsometric tint in the `color:` block of the terrain raster draw — painted onto the surface,
so they have no depth relationship with it and cannot show through a ridge. Their `depth_shift` only
ever has to separate roads and buildings, which are sparse and far less sensitive than lines lying on
the ground.

So the answer for contours is §4 (compute them in the terrain draw), not a depth bias. Keep our
lattice split (§2.2) as the default for line geometry: it is exact, needs no bias, and is only 7%
slower than the un-subdivided version. A smaller shift (0.005) was never measured — worth a try only
for road-like content, and only once contours no longer depend on it.

---

## 4. The dominant cost in the real config: layers multiply tiles

Render tiles per one-second interval, north pan, same camera:

| configuration | render tiles | surface draws |
|---|---|---|
| base only | 132 | 265 |
| base + hillshade | 693 | 926 |
| base + hillshade + contours | 492 | 618 |

**Adding the hillshade layer multiplies render tiles ~5×.** Each layer brings its own tile set, its
own terrain surface pass and its own stencil mask — in the north pan that reaches ~130 render tiles
and 161 surface draws per frame, 3.9M surface indices. Tangram computes hillshade and contours in
the fragment shader of the terrain raster draw it is already doing: zero extra tiles, zero extra
draws.

This is the biggest structural gap for the configuration that actually feels slow.

**Does that break the composite layer, where hillshade can sit at any style level?** Not
necessarily — tangram's mechanism is not "hillshade is fixed at the bottom". Any style can declare
`raster: custom` and call `sampleRaster()` / `getElevationAt()` in its own shader blocks, because
the DEM raster is *bound to the tile draw*. The port that preserves our composite slots is
therefore: bind the shared DEM to the tile draw and let the slot's shader compute shading where the
style says, instead of giving the slot its own tile layer. Ordering is preserved; what disappears is
the duplicate tile set and its geometry.

---

## 5. Startup: 3.8 s to first content

Launch → first drawn tile geometry, warm cache, demo camera:

| style source | first tile requested | first content |
|---|---|---|
| inline | 1.33 s | 3.77 s |
| zip | 1.25 s | 3.89 s |
| **assets** | **5.75 s** | **6.86 s** |

Two independent problems:

1. **The assets style costs ~4.4 s before the first tile is even requested.** (Consistent with the
   6.45 s style decode noted earlier.) Only affects `StyleSource.ASSETS`.
2. **Even with inline/zip it is 3.8 s**: ~1.3 s before the first tile request (JVM attach, GL init,
   and ~0.6 s enumerating 220 system fonts + loading Roboto), then ~2.4 s until tiles are decoded
   and drawn.

Tile decoding is **not** the limit: `Options::setTileThreadPoolSize` defaults to 1 where tangram
uses 2, but raising it to 4 changed nothing warm (3.8 → 3.9-4.7 s) or cold (3.2 → 3.6 s), and four
workers really do start. The decoder holds its mutex only to copy `shared_ptr`s, so decode does
parallelise. The remaining 2.4 s needs timestamps inside the fetch → decode → upload path; one-second
`RenderStats` intervals are too coarse to place it.

---

## 6. Measured to cost (almost) nothing — do not re-run these

Each was an interleaved A/B at the demo camera unless stated.

| hypothesis | result |
|---|---|
| Source-density fills (no fill subdivision) | 16.6 vs 16.7 fps — the existing comment in `TileLayer.cpp:280` was right |
| Lattice clamp on surfaces (16 taps → 4) | 16.8 vs 16.9 — correct but unmeasurable, reverted |
| Shadows off | no change (cached/snapped) |
| Sky shader off | ~0 (−1 ms GPU) |
| Drape bakes disabled entirely (`DRAPE_BAKE_TIME_BUDGET = 0`) | 16.6 vs 16.6 |
| Flat 1×1 stencil mask instead of the full grid | 16.6 → 17.3 (+4%, not the 19% first claimed from a drifted run) |
| Half display resolution (¼ the pixels) | 17.9 → 20.3 (+13%) — not fill-bound |
| Worker-side VBOs for the depth job | no change |
| Elevation texture border re-encode | 0.1-0.8 encodes/s at rest (<1 MB/s); **3.5-6.8 MB/s during zooms and when panning into new terrain** — a transition cost, not steady state |

**Unexplained:** timed GPU sections total 21-25 ms against a 53 ms frame, while `layers` shows ~28 ms
CPU of which only ~5 ms is attributable (per-draw counters + mask/drape timers); the rest is blocking
inside GL calls. `glFinish` brackets (which inflate everything ~2.4× by destroying pipelining) put
the layer block at 116 ms and the vt 2D pass at 78 ms of it. Placing that gap properly needs
per-pass GPU queries inside the layer pass.

---

## 7. Next steps, in the order I would take them

1. **Hillshade (and contours) as shader blocks on the terrain draw** — §4. Biggest structural win
   for the configuration that feels slow; needs the composite-slot design above so ordering
   survives. Expect it to remove most of the ~130 render tiles / 161 surface draws per frame.
2. **Adopt tangram's screen-area LOD rule** in `TileLayer::calculateVisibleTilesRecursive` —
   `--es maxTileZoomOffset -1` already emulates it: **17.6 → 19.5 fps**, `layers` 28.3 → 22.2 ms, and
   the near field actually fills *faster* (fewer tiles to load). It is a behavioural change for every
   layer, so it wants to be an option, not a silent default.
3. **Decide on the tangram content model** (§3.1) — +7% and 2.7× fewer content indices, needs a
   see-through verdict at several cameras.
4. **Startup** — split the 2.4 s fetch → decode → upload gap with real timestamps; move the ~0.6 s
   font enumeration off the critical path; look at the assets-style decode (§5) if that path matters.
5. **Drop the stencil mask pass** where nothing needs it (+4%), and revisit draping itself: it costs
   7-11 ms GPU, and tangram does without it entirely.
6. **Place the unexplained GPU/CPU gap** (§6) with per-pass timer queries.

---

## 8. Open questions worth an experiment

- Why does `layers` CPU stay ~28 ms regardless of pixels, vertices, subdivision, draws and bakes?
- Does the elevation border re-encode explain the "slower in some places" feel during transitions,
  and would tangram's clamp-and-extrapolate be acceptable visually?
- Is the drape still worth its cost once content follows the surface exactly (§2.2)?

---

## 9. Terrain paint: the hillshade without a tile set

Landed for hillshade. The layer keeps its class, its API and its place in the layer order; what
disappears is everything behind it.

### 9.1 The shape of it

A layer in **paint mode** holds no tiles at all. It shades the elevation texture the 3D terrain has
already bound, as ONE quad per terrain tile, baked into the shared drape texture at its own position
in the bake order (`MapRenderer` bakes drape layers in layer order, so the style's placement of the
hillshade is preserved by construction — nothing in the composite layer changed).

What is gone per frame: the DEM tile set (its cull, fetch, decode, normal map build and upload), its
stencil mask, and its share of the render tiles. What is added: one two-triangle draw per tile per
**bake** — and bakes are cached, so in steady state it costs nothing.

- `vt`: `GLTileRenderer::setTerrainPaint`, `renderTerrainPaint`, `terrainPaint*` shaders.
- `all/native`: `HillshadeRasterTileLayer` decides paint mode, `TileRenderer::setTerrainPaint`
  carries it, `TileLayer::drapeStackSignature` watches it.

The lighting is *the same code*: the normal-map lighting shader (built-in or a custom one) is
injected over a prelude that reads the terrain DEM, and is handed a normal rebuilt from the DEM
gradient. All five hillshade methods, the colours and custom `getElevation()` shaders work unchanged.

### 9.2 When it engages

3D terrain **with draped fills**, the layer's data source is the terrain's own, and the built-in
contour lines are off. Anything else keeps the normal-map tile path, untouched: a different DEM must
not be silently replaced by the terrain's, without the shared drape there is no layer-ordered bake to
paint into, and the contour lines live in the normal-map fragment shader *outside* the lighting
shader the paint reuses — in paint mode they would simply vanish (until the CONTOUR kind lands).

**It is not pixel-identical, by construction.** The sampling is the terrain's elevation grid, so the
layer's own zoom level bias no longer reaches it, and the gradient is recomputed per drape texel in
floats instead of interpolating an 8-bit-packed 256² normal map — crisper, and blockier where the DEM
grid is coarse. A custom normal-map lighting shader that reads `getRawColor()` sees the terrain's
re-encoded DEM texel, not the source tile's; `getElevation()` is the portable one.
Switch it off with `HillshadeRasterTileLayer.setTerrainPaintEnabled(false)` or, for an interleaved
A/B that also reaches a composite layer's internal hillshade child,
`adb shell setprop debug.massif.terrainpaint 0`.

### 9.3 Two things the port had to get right

**The relief boost follows the sampling density, not a tile id.** MapLibre's low-zoom boost is keyed
off zoom; the normal-map path multiplied the tile zoom by its bitmap resolution, so a 512-texel grid
at z11 was worth a z12 tile of 256 texels — which is exactly what the terrain's elevation grids are
(measured: z11 grid, 514² texture, 38.2 m/texel — the same density as an old z12 hillshade tile).
Keyed off the grid's own zoom the paint read the same data as one level coarser and came out ~1.5×
too strong. It now derives the zoom from metres per texel.

**A paint has no per-tile fingerprint.** It is not made of the layer's tiles, so the drape cache
cannot notice a parameter change through one. Reporting the previous frame's cover instead made every
tile that had just entered it look incomplete and bake twice (measured: surface draws *up* 12%). The
paint's appearance now rides `TileLayer::drapeStackSignature`, and the layer reports no tiles at all.
Because the paint is baked, a change re-bakes: with `IlluminationMapRotationEnabled` (the default)
that includes the map rotation, quantised to 2° so a rotation gesture re-bakes a bounded number of
times instead of every frame.

### 9.4 The DEM level: why full detail is off

The terrain caps the elevation grid at what the MESH can express (`ElevationManager::clampTileZoom`:
one texel per half surface cell), which drops two zoom levels. Shading is per fragment and resolves
far more than that, so on the paint that cap is visible as blur from z15 up - it is why the hillshade
"does not render to the DEM's max zoom". `getFullDetailDataTile` + `ElevationTextureCache::setFullDetail`
lift it for the paint's own cache.

It stays OFF by default, because the elevation texture pipeline cannot pay for it (Crosscall, north
pan, `debug.massif.paintdetail 1`): **2.5 fps against 6.7**, with `drape` at 172-218 ms. Measured
cause: each DEM grid is 512², re-encoded into a 514² RGBA texture **on the render thread** and
uploaded there (53 ms + 52 ms per texture at full detail), and the working set jumps ~16× past the
96-texture cache. At the mesh level the same path barely runs (fewer than 64 encodes over a whole
run, `drape` 6-8 ms) - it is the full-detail working set that breaks it, not the encode as such.

Tangram pays none of this: `elevation.yaml` binds the source raster (256², `filtering: nearest`)
as the tile's own texture, uploaded once when the tile loads, ancestors addressed through
`u_raster_offsets` uv sub-rects, edges extrapolated in the shader. Porting that means: build the
texture payload where the grid is decoded (a worker) instead of in the drape section, upload the
grid's own samples with no per-texel re-encode, and patch the neighbour border as small
`glTexSubImage2D` strips instead of rebuilding the tile. Our border machinery (cross-level backfill,
edge box filter) is a seam feature tangram does not have, so it has to survive the port - which the
strip-patch form allows.

One step of that already landed: the encode's interior is now a straight copy of the grid's own
rows. It used to run a per-texel lambda with neighbour dispatch and four edge-filter tests over the
whole tile (79 ms per texture measured; 45-53 ms after).

### 9.5 Measured, and what is left

Device (Crosscall, north pan, interleaved, `bench/abpaint.sh`). **Measure the hillshade with a
background-only style** (`--es minimal true`, which strips the inline style to the Map background
plus the composite slots): with the full style, the base map's own geometry is most of the frame and
hides the whole effect. 37-39 windows each:

| config | fps | frame | drape | layers |
|---|---|---|---|---|
| **terrain paint** | **22.0** | 38.7 ms | 2.8 | 10.0 |
| paint + full DEM detail | 13.2 | 66.6 ms | 26.6 | 11.6 |
| normal-map hillshade | 17.0 | 49.9 ms | 5.2 | 16.5 |

**+29% fps, 49.9 -> 38.7 ms per frame.** With the full style over the same pan the two are a wash
(6.7 vs 7.0 fps, 33-34 windows) - the hillshade's cost there is tile loading, which the frame timer
does not see, while `layers` is the base map:

| config (full style) | fps | frame | drape | layers |
|---|---|---|---|---|
| terrain paint | 6.7 | 134 ms | 10.2 | 47.2 |
| paint + full DEM detail | 2.5 | 400 ms | 218 | 79 |
| normal-map hillshade | 7.0 | 117 ms | 7.7 | 53.3 |

Emulator counters for the same pan (structural, fps there is capped and means nothing):

| | render tiles | style layers | surface draws | surface indices |
|---|---|---|---|---|
| normal-map hillshade | 7000-7300 | ~460 | 8750-9000 | 215-220M |
| terrain paint | 5300-5700 | 390-420 | 7000-7600 | 172-186M |

Appearance at the ridge camera (45.244172/5.760595 z13.2 t55): 25% of pixels differ by more than 12,
mean absolute difference 8.4/255 — the paint is sharper, because it samples the DEM per fragment
instead of magnifying a 256² normal map. No brightness shift.

### 9.6 Open bugs, as of 2026-08-02

- ~~**Tiles blink while zooming in.**~~ Gone with the drape: the shared ground has no bake, no
  generation swap and no stand-in textures. Confirmed by Martin on the emulator.
- **Artifacts at high zoom (z15+):** blurred ground with the background bitmap's pattern showing
  through. One 1024 drape texture per tile, magnified far past its resolution. Drawing the paint
  without the drape (`--es drape false`) is sharp at the same camera, which is corroboration, not
  proof.
- ~~**Tile edge stitching is probably not applied at all.**~~ **Fixed.** `buildTerrainEdgeCoarsening`
  ran only from `setVisibleTiles`, so the coarsening map was built from A LAYER'S OWN visible tiles
  while the surfaces actually drawn come from the drape cover (normalised leaves, drawn by
  `drapeLayers.front()`), from the shared ground cover (§10) or, for a paint, from the terrain
  cover. A paint has no tiles at all, so its map was empty and its surfaces stitched nothing. The
  renderer now keeps the cover it is handed (`GLTileRenderer::terrainSurfaceTileIds`: drape cover,
  else ground cover, else paint cover, else its own tiles) and rebuilds the map whenever that
  changes, so stitching follows the geometry that is drawn in every mode.

**Open: a hillshade-only stack draws nothing under the paint.** With no vector layer there is no
drape cover, so the paint is given the terrain's own cover (`TerrainRenderer::collectVisibleTiles`
via `TileLayer::needsDrapeCover`) - but nothing in such a stack ever loads elevation, and the drape
reports `tiles without elevation 12 of 12`. A DEM prefetch and a redraw pump from the seeding did not
close it; the load path needs a look. Note the same stack on the normal-map path is also flat (faint
shading, no relief), so 3D terrain in a hillshade-only stack is broken independently of the paint.

Left to do: that gap (it blocks benching the hillshade alone), the elevation texture port (§9.4), the
CONTOUR and HYPSO paint kinds (same quad, same prelude), contour labels as tangram generates them
(short seed-walk stubs from `ContourTileDataSource`, styled by the existing `#contour` text rules),
and a paint path for the no-drape configuration, where the layer still keeps its tiles.

---

## 10. The shared terrain ground: no drape, no per-layer pre-pass, no stencil masks

This is the change §0 called for, and it is one change because the three costs are one arrangement.
Nothing is baked any more: the layer stack shares ONE cover of terrain tiles, the ground is drawn
once for that cover, and every layer then composites straight onto it in layer order - which is
what tangram does (`core/src/style/rasterStyle.cpp`: one shared grid mesh, one draw per tile, no
pre-pass, no masks anywhere in `core/src`).

Active whenever 3D terrain is on and draped fills are off. **The demo now defaults to it**
(`DemoConfig.TERRAIN_DRAPE_FILLS = false`); `--es drape true` brings the drape back for an A/B.
The SDK option `TerrainOptions.DrapeFillsEnabled` still defaults to on, so no app changes behaviour
until it opts in — that default flips, and the drape code goes, once §10.2 lands and the device
numbers are in. **The drape is being dropped, not kept as an option.**

### 10.1 What each piece becomes

| | with the drape | shared ground |
|---|---|---|
| ground geometry | one grid surface per tile, textured with the tile's baked 1024² RTT texture | one grid surface per tile, in the ground colour, lit and shadowed |
| fills | baked flat into that texture (cached) | drawn as displaced 3D geometry, every frame |
| tile backgrounds / rasters | baked | drawn on the COVER tiles, with the source uv sub-rect the overzoom path already computes |
| depth | the drape surface is the only depth writer | the ground pass is the only depth writer |
| per-layer depth pre-pass | already skipped | skipped |
| stencil tile masks | one grid draw per tile per layer | none - the proxy loses on depth instead (§10.1.1) |
| content subdivision | every fill and line cut to the surface's mesh threshold at decode | none at all, as tangram (§10.4) |

Two rules hold the depth model together, both inherited from rounds 45-56 and unchanged:

1. **The ground is drawn at its TRUE depth and is never pushed back.** Everything after it is
   `GL_LEQUAL`, no bias in either direction: coincident content passes, content behind a ridge
   fails. A forward pull is what leaks far-slope content over a crest.
2. **Ground-shaped content is drawn on the cover tiles, not on the layer's own.** Two tesselations
   of one height field do not agree, so a hillshade at z12 drawn on its own tiles would z-fight the
   z14 ground. On the cover it is coincident to the bit. That is why the cover computation
   (`MapRenderer::collectTerrainCover`, extracted from the drape path) is shared by both modes.

#### 10.1.1 The depth model, which is tangram's, whole

Read out of their source rather than derived. `res/scenes/terrain-3d.yaml` is the file that matters
— `polygon.vs` and `polyline.vs` only set the defaults it overrides:

```glsl
// core/shaders/polygon.vs
gl_Position.z += (proxy - layer) * (TANGRAM_DEPTH_DELTA * gl_Position.w + depth_shift);

// res/scenes/terrain-3d.yaml, the 3D terrain block
// "use larger depth delta near camera to prevent terrain from covering geometry"
depth_shift = -0.02*u_proj[2][3];          // [2][3] is -1 => a FLAT 0.02, unscaled
#ifdef TANGRAM_RASTER_STYLE
// "need sufficient offset for proxy levels to prevent terrain poking through level above"
proxy *= 48.0;
#endif
```

with `TANGRAM_DEPTH_DELTA` = 2⁻¹⁹ on a 24-bit buffer (2⁻¹⁵ on 16-bit), `layer` = the style layer's
order, `proxy` = the tile's proxy depth, and — `core/src/style/style.cpp` — opaque and translucent
both drawing with depth test **and depth write**.

Ours, matching it: the ground pass writes depth for the cover (pushed back 48 per proxy level where
a tile stands in on a coarser one); content writes depth and carries `(proxy - layer)`, with the
style layers numbered densely across the whole stack, because our stack is several renderers where
tangram has one ordered style list; `depth_shift` = 0.02, flat.

**And the near plane, which is why any of it works.** `core/src/view/view.cpp:452` sets
`near = m_pos.z / 50.0` and bounds `far` to about twice the camera height — a far/near ratio of a
few hundred, fixed. Ours took `near` from the nearest visible ground point, floored at
`MIN_NEAR = 1/16` of an internal unit: centimetres with the camera near a slope, a ratio of
10⁴-10⁶, and NDC depth so non-linear that a constant-NDC bias is worth hundreds of metres at range.
That is the mechanism behind rounds 45-56 and every see-through since, and it is why tangram can
afford ordinals up to ~1200 while this branch could not afford 128. Terrain mode now floors `near`
the same way (their `far` is not taken — it would end the view closer than
`TerrainOptions.ViewDistanceFactor`).

#### 10.1.2 Four ways to get this wrong, all of them tried

Each of these shipped, was tested, and had to be undone. They are the argument for porting a model
whole rather than by parts:

| attempt | what it produced |
|---|---|
| masks dropped, content not writing depth | the previous zoom's roads painting through every gap in the new tile's content, blinking as the blend runs |
| content writing depth, no per-style-layer ordinal | fills shredded into stripes; washed road casings (round 52 again) |
| no subdivision, no `depth_shift` | all content sunk into the terrain and occluded by it — contours and roads swallowed |
| ground stand-in walking to an ancestor, content not | roads snapping to straight lines over ground that IS displaced |

Two more, from choosing constants instead of copying them: `depth_shift` scaled by `|m22|` (did
nothing) then by `|m23|` (dragged a landcover fill in front of the mountain); an ordinal stride of
32 per renderer (reached the leak range with five layers).

### 10.2 Measured (emulator, structural)

Ridge camera 45.244172/5.760595 z13.5 t20, hillshade + contours + elements, scripted pan, per
one-second interval. Emulator fps means nothing; these are counts.

| | drape | shared ground |
|---|---|---|
| stencil mask draws | 4209 | **0** |
| mask time | 24.7 ms | **0** |
| surface draws | 5612 | **3450** (−39%) |
| surface indices | 138 M | **85 M** (−38%) |
| geometry draws | 2806 | 9100 |
| geometry indices | **25 M** | **339 M** |

So the ground side is 39% cheaper and the mask pass is gone — and the fills that used to be baked
once now cost 13× more index throughput, every frame. That is the whole trade, and it is why the
next step is not optional:

### 10.3 The paint had to come with it (device, measured)

The first device A/B said **26.6 fps against the drape's 36.6** — and it was not the ground pass.
`HillshadeRasterTileLayer::isTerrainPaintActive` required draped fills, so turning the drape off
turned the *paint* off too and the layer went back to its own DEM tile set: fetch, decode, normal
map, upload and ~5x the render tiles, to draw what the terrain already had on the GPU. The paint now
only requires 3D terrain: with the drape it takes its place in the shared bake, without it it draws
itself as the terrain surface on the shared ground cover
(`GLTileRenderer::renderTerrainPaintSurfaces`, which until now had never actually run).

Two bugs surfaced the moment it did, both fixed:

- **The sky went black.** The paint surface pass left its VBOs bound, and `SkyRenderer` draws its
  quad from a CLIENT-SIDE array - with a `GL_ARRAY_BUFFER` bound, that pointer is an offset into it,
  so the sky quad flew off screen and the clear colour showed. Any renderer that feeds a client
  array is exposed to this; unbind after every vt draw loop.
- **White speckles over the shading.** The paint draws the same grid, displaced by the same DEM, as
  the ground pass already drew - but from a different program, so the clip z differs in the last
  float bits and GL_LEQUAL dropped a scatter of fragments, showing the bare ground colour. One
  `TERRAIN_LAYER_DEPTH_DELTA` of clearance (what backgrounds carry over the surface they share)
  fixes it.

**Ground-shaped draws per tile is now the number to watch.** The drape does ONE (plus a cached
bake); the shared ground was doing THREE - the ground fill, the paint, and the style's tile
background. The third is gone: a patternless background of exactly the ground colour is skipped,
because the ground pass has already painted it with the same displaced grid (emulator: background
draws 1234 -> 160 against 487 ground fills). Two remain wherever a paint is in the stack, and each
is a full grid draw whose vertex stage does ~20 elevation texture fetches. Folding the paint INTO
the ground pass would take it to one, which is exactly tangram's arrangement, but it moves the
hillshade to the bottom of the layer order - it needs a decision, not a patch.

**§10.4 The fills no longer subdivide - landed.** `TileLayer` used to decode every fill and line
subdivided to `tileMeters / meshResolution`, because an un-subdivided fill that was NOT draped sagged
below the surface. With no drape at all that reason is gone: content is displaced by the same
function the ground uses, and `depth_shift` covers the chord it makes between its own vertices -
which is precisely what tangram's comment says it is for. Both flags now follow "terrain without a
drape", in `calculateDrawData` and `resetTileTransformer` alike (they must agree, or tiles decoded
for the other mode stay cached forever).

That removes the 13× index throughput noted above, and with it the "roads go straight when zooming
out" report: the subdivision was a property of the tile's own zoom, so a coarse or proxy tile's roads
chorded across the terrain until the tile for the new zoom arrived.

### 10.5 What this path still does not do

- **The sun works; cast shadows are wired but switched OFF.** The light and shadow block came out
  of the drape path into `MapRenderer::applyTerrainShadows`, which both paths now call with their
  own cover - so the light boxes, the caster pass and the map cache are shared code, and the shared
  ground gets the resolved stack lighting before it draws. Two real bugs were in the way and are
  fixed: `TileRenderer` only enabled terrain lighting `if (drapeFills)`, so with the drape off the
  ground AND the hillshade paint over it were unlit (and a shadow multiplies the LIT colour, so
  nothing could show); and the paint, which covers the ground it is drawn on, had no lighting of
  its own - it now takes the same sun and shadow the surface takes, from the geometric normal, not
  from the hillshade's own exaggerated slope.
  What is still wrong: with the caster pass enabled the map reads as scattered **shadow acne**
  instead of the drape's cast shadows - same scene, same map, same emulator, the drape path clean
  and this one not. So `applyTerrainShadows(..., castShadows = false, ...)` for the shared ground:
  half-working shadows are worse than none. Flip it to true to work on it, and diff against the
  drape path, which is the reference.
- **A cover leaf coarser than a render tile** (only when the split hits its 256-tile cap) makes that
  tile draw on its own surface, one tesselation finer than the ground it stands on.
- **Device numbers have not been re-taken since the content model landed** - the change that should
  pay for the whole arrangement. Nothing in §10.2 reflects it.
- `proxy *= 48` is ported for the ground; the rest of tangram's raster-style handling (binding the
  DEM to the tile draw through `u_raster_offsets` instead of re-encoding a texture per tile) is not.


---

## 11. Head-to-head with tangram, and what it found (2026-08-03)

The first session where our fps and tangram's were measured **on the same device, in the same
motion, with the same instrument**. Most of what it produced is negative results, which is the
point: several things we were about to optimise are measured not to matter.

### 11.1 How to measure across the two apps

`PROF` is ours only and is **not comparable** to anything tangram reports — it read 20–27 fps for a
config that the cross-app instrument put at 11–13. Use SurfaceFlinger for both:

```sh
adb shell dumpsys SurfaceFlinger --timestats -disable ; --timestats -clear ; --timestats -enable
# drive the motion, then
adb shell dumpsys SurfaceFlinger --timestats -dump --maxlayers 8
```

Read `averageFPS` of the **`SurfaceView[<pkg>/...](BLAST)`** layer. Two traps: our app also reports
an activity-window layer that reads ~30–50 fps and means nothing, and `dumpsys gfxinfo` counts only
the UI layer (6 frames) because the map renders on its own thread. `--latency` returns just the
refresh period on this Android.

Their demo APK is prebuilt at
`platforms/android/demo/build/outputs/apk/release/demo-release.apk` (`com.styluslabs.tangram.android`).
**Tap the "3D" chip at (276,152) after every launch** — it sets `global.terrain_3d` *and* tilts to
1.0 rad, and it does **not** survive a restart. A forgotten tap silently compares their 2D map to
our 3D one; that produced a bogus "tangram 38 fps" before the chip pixel was checked for blue.
1.0 rad ≈ **tilt 33 in our convention**, so run ours at `--es tilt 33` to match.

**The device throttles over a session.** The same build measured 8.4, 11.2 and 16.1 fps in one
evening. Only interleaved arms inside a single run mean anything, 3 repeats minimum, report median
and spread. A conclusion drawn by comparing to a number taken earlier is worthless — that mistake
produced, and then unproduced, a "we are 1.5× slower" headline.

### 11.2 Where we stand

| config | fps |
|---|---|
| terrain only — tangram full stack vs ours (background + hillshade) | 17.9 vs **16.2** |
| with content — tangram full stack vs ours (roads + labels + hillshade) | 18.1 vs **8.3** |

**Terrain is at parity. The entire gap is the layer pass**, and ours draws less than theirs while
losing 2.2×.

### 11.3 Contours: lines ported, labels not

Their `core/src/style/contourTextStyle.cpp` builds contour labels from the **elevation texture**:
a `gridSize = 4` grid of seeds per tile, each marching along the elevation gradient to
`round(elev/elevStep)*elevStep` (≤12 iterations, bisecting once it brackets the level), then walking
the tangent for `labelLen = 32/tileSize` worth of points — a short polyline to lay the text along.
**No contour geometry, no contour source, no contour tiles.**

Ours draws the lines the same way (a fragment block on the terrain paint, measured **free**: 7.25 fps
without contours against 7.57 with) but gets labels from `ContourTileDataSource` or pre-baked
contour vector tiles — a whole tile set with fetch, decode, geometry and label placement. Porting
their builder would delete that layer and keep labelled contours.

### 11.4 Measured NOT to be the problem — do not re-run

- **Geometry volume.** Area subdivision off cuts vector indices 37.3M → 7.5M per interval (3× per
  render tile) and buys **+6.5%, inside the noise**.
- **Per-vertex DEM taps.** 16 → 1 (`debug.massif.demtaps`) with content on: 5.69 vs 5.86 fps, i.e.
  nothing. Worth ~20% in the terrain-only config and nothing once content is there.
- **The GPU.** `PROF GPU` with content: layers 29–43 ms, total 38–53 ms, against a CPU frame of
  120–175 ms. The GPU could sustain ~20 fps; we are CPU-bound.
- **Tile LOD granularity.** `--es maxTileZoomOffset -1` moved 11.46 → 11.16 fps. This supersedes the
  +11% in §7.2, which was measured with `PROF` on a different config.
- **Paint-as-ground** (`debug.massif.groundpaint 1`): 13.80/15.02 against a 13.96/15.66 baseline —
  nothing, twice. So the hillshade keeps its place in the layer order at no cost.
- **The far plane.** Porting `far = 2*height/cos(pitch+fovy/2)` changed neither tile count nor fps
  at that camera: the ground-derived far is already inside the bound their formula gives there, so
  it never binds. It binds at a LOW tilt, where the cosine goes negative and the 127-tile-width cap
  takes over - see `TerrainOptions.ViewDistanceFactor`, which now carries both terms.

### 11.5 What the layer pass actually spends, and the pan hang

Probing down the call chain, with roads + labels + hillshade on:

| | ms/frame |
|---|---|
| `CompositeVectorTileLayer::onDrawFrame` (the whole layers block) | 36–51 |
| ├ group 0 — the base map's own vt render | 17.8–24.2 |
| ├ hillshade slot child | 6.5–13.1 |
| └ `DRAW_ITEM_VT_GROUP` children | ~11–14 |

and inside one `TileRenderer::onDrawFrame`:

| | ms/call |
|---|---|
| waiting for `_mutex` | **0.00** — not contention |
| `prepareFrameUnsafe` | 0.00 |
| `renderGeometry` | 6–9 |
| the terrain state block | **0.10 … 20.98** |

> **Superseded by §12.1 — this attribution is wrong.** Measured with counters and a timer inside the
> block itself: nothing is rebuilt there, and the block costs **0.04 ms**. Read §12 instead.

**The terrain state block is the pan hang.** It costs a tenth of a millisecond when nothing changes
and up to 21 ms when a DEM tile lands, because that is where `invalidateTileSurfaces` /
`resetTileSurfaces` rebuild tile surfaces — on the render thread. Tangram never does this: their
surface is one static shared grid and an elevation change only replaces a texture, never geometry.
The existing `SURFACE_RESET_DELAY` debounce covers `resetTileSurfaces` but not the per-tile
`invalidateTileSurfaces`.

Note the shared static grid VBO itself is **already implemented and active** —
`surfIndices / surfDraws` comes out at exactly `24576 = 64×64×2×3`, one unit grid per draw with a
per-tile matrix, which is tangram's `RasterStyle` arrangement. It does not need porting again.

### 11.5b How tangram avoids it entirely — the target design

Their terrain geometry is built **once, at scene load**, and never again:

```cpp
// core/src/style/rasterStyle.cpp — RasterStyle::build(const Scene&)
uint32_t resolution = _scene.elevationManager() ? 64 : 1;
... one (resolution+1)^2 grid of {x, y} ...
m_rasterMesh = std::move(rasterMesh);          // vertex layout: a_position, 2 x GL_SHORT
```

So in tangram a DEM tile arriving is **a texture upload and nothing else**. There is no per-tile
terrain mesh anywhere, therefore nothing to invalidate, therefore no render-thread rebuild.

Everything that needs elevation on the CPU reads the texture instead of a mesh:

- `ElevationManager::elevationLerp(tex, pos, &grad)` bilinear-samples the texture's own CPU buffer
  (`tex.bufferData()`), with a one-entry `prevTex`/`prevTileId` memo. That is what serves label and
  marker placement, and what `contourTextStyle.cpp` marches along (§11.3).
- Occlusion and picking come from `renderTerrainDepth()` — the same shared grid drawn into an FBO —
  read back through `getDepth(screenpos)`.

**Where ours differs, precisely.** We already have the shared grid and the draw path uses it in
regular-grid mode:

```cpp
// GLTileRenderer.cpp:3528
for (const auto& tileSurface : (gridMode ? buildCompiledTerrainGridSurfaces()
                                         : buildCompiledTileSurfaces(tileId))) {
```

But we *also* build per-tile CPU surface meshes in `setVisibleTiles` → `buildTileSurfaces(tileIds)`,
and throw them away on every elevation change via `invalidateTileSurfaces`. In grid mode the draw
does not use them at all — their remaining consumers are the raycast/picking path
(`_tileSurfaceMap` at GLTileRenderer.cpp:1802, `findTileBitmapIntersections`) and the non-grid draw
path. So we are paying 21 ms on the render thread to rebuild geometry that the renderer does not
draw.

**The work, in tangram's order:**

1. In grid mode, stop building and invalidating per-tile CPU surfaces for rendering — the shared
   grid already covers the draw.
2. Serve picking the way they do: `elevationLerp` over the elevation texture, or the terrain depth
   read-back we already have in `TerrainDepthWorker`. Failing that, build a tile's surface lazily on
   the pick itself, which is a user gesture and not a frame.
3. If any surface rebuild must survive, move it off the render thread or debounce it the way
   `resetTileSurfaces` already is via `SURFACE_RESET_DELAY` — `invalidateTileSurfaces` currently has
   no debounce at all, which is why a stream of arriving DEM tiles stalls every frame.

### 11.6 What landed this session

- Per-tile background meshes retired under a shared ground (tangram has none — their map background
  is the framebuffer clear colour, `core/src/map.cpp`): 14.0/15.5 → 16.1/16.3 fps interleaved.
- Contour lines as a fragment block on the terrain paint, so a layer asking for contours no longer
  falls back to its own DEM tile set: render tiles 494 → 216, `layers` 18.4 → 16.1 ms.
- The depth budget (§10) rescaled to the stack's ordinal span, and area fills subdivided to two
  surface cells instead of one: 16.6 → 20.6 fps at the mountain camera.
- Measurement switches, all defaulting to current behaviour: `debug.massif.demtaps`,
  `debug.massif.groundpaint`, `debug.massif.tilebg`, `debug.massif.areathreshold`,
  `debug.massif.areasourcedensity`.

---

## 12. The render thread, profiled (2026-08-03)

Every previous section reasoned about where the frame goes from timers we placed by hand. This one
**sampled** it. The instrument is the device's own `simpleperf`, and it moved the top of the list
somewhere nobody had guessed.

### 12.1 First, the dead end it killed

§11.5 claimed the pan hang was `invalidateTileSurfaces` / `resetTileSurfaces` rebuilding per-tile
CPU surfaces on the render thread, and §0 had that as the largest known item. It does not happen:

```
RenderStats: ... | surfBuilt=0 surfInval=0 | ...        (every interval, whole north pan)
PROBE terrainstate: tiles=2 changed=0.03 invalSurf=0.00 invalLabel=0.01 ms
```

`_tileSurfaceMap` is filled **only** by `buildCompiledTileSurfaces` (`GLTileRenderer.cpp:5631`),
which only the NON-grid draw path calls — and every terrain configuration we ship is grid mode. So
the map is empty, `invalidateTileSurfaces` iterates nothing, and the block costs 0.04 ms, not 21.
(`surfBuilt` / `surfInval` were being collected but never printed; the `RenderStats` line carries
them now.)

### 12.2 How to profile the render thread

```sh
adb shell simpleperf record --app com.massifmaps.MassifDemo -g -f 500 --duration 12 -o /data/local/tmp/perf.data
adb pull /data/local/tmp/perf.data /tmp/perf.data
```

Symbols need the **unstripped** library, and `--symfs` matches by the dso's path on device, so the
tree has to mirror it:

```sh
D='/tmp/symfs/data/app/~~<hash>==/com.massifmaps.MassifDemo-<hash>==/lib/arm64'
mkdir -p "$D"
cp scripts/android-dev/massif/build/intermediates/cxx/Debug/*/obj/arm64-v8a/libmassif.so "$D/"
$NDK/simpleperf/bin/darwin/x86_64/simpleperf report -i /tmp/perf.data --symfs /tmp/symfs \
  --tids <gl-thread-tid> --children --sort symbol -n
```

Two traps: the report is **per process** by default and the tile decode threads are as busy as the
render thread (`Thread-7` 28%, `GLThread 32` 27%), so always pass `--tids`; and there are several
threads called `GLThread 32` — the render thread is the one whose call graph starts at
`MapRenderer::onDrawFrame`.

### 12.3 Where it goes (Crosscall, north pan, 12 s, 4308 samples on the render thread)

| | share of the render thread |
|---|---|
| `ElevationTextureCache::beginFrame` → `uploadReadyTextures` | **37.5%** |
| ├ `Bitmap::loadFromUncompressedBytes` — a 514² RGBA copy, byte by byte | 19.6% |
| ├ `EncodedTexture::~EncodedTexture` — freeing the encode buffer there | 10.8% |
| └ `Bitmap::~Bitmap` / `Texture::~Texture` | 6.6% |
| `GLTileRenderer::renderGeometry2D` | 31.0% |
| └ **`std::sort` for the near-to-far tile order** | **21.4%** |
| ⤷ its comparator: `TerrainTileTransformer::calculateTileBBox` 22.2% incl., `ElevationManager::getMinMaxDisplayHeight` 8.0% | |
| `HillshadeRasterTileLayer::onDrawFrame` | 10.5% |
| `TerrainRenderer::updateDepthBuffer` (7.7% of it destroying the previous buffer) | 8.7% |

Two thirds of the render thread was doing no GL work at all: **one memcpy-shaped copy and one
sort key computed inside a comparator.**

### 12.4 What landed

- **One distance per tile, not one per comparison.** The near-to-far comparator called
  `_transformer->calculateTileBBox` on both sides of every comparison; on the terrain transformer
  that samples the elevation manager for the tile's min/max height and transforms the box in double
  precision. The distances are now computed once per tile per frame and the sort reads them.
- **The elevation texture's bitmap is built on the encode worker**, not on the render thread. The
  worker already encodes the padded texture; it now also constructs the `Bitmap` (which is where the
  megabyte copy is) and hands that over, so the render thread does the upload and nothing else. The
  encode buffer is a worker-owned scratch vector reused by every job, so neither the allocation nor
  the free is in a frame any more.

Interleaved A/B on the device (north pan, hillshade + contours, composite base, 3 repeats each,
57 vs 61 one-second windows):

| | fps median | frame | `layers` | fps p25 |
|---|---|---|---|---|
| before | 7.3 | 118.6 ms | 49.5 ms | 3.9 |
| **after** | **8.7** | **96.4 ms** | **26.0 ms** | **8.1** |

**+19% fps, and the frame-time floor rises from 3.9 to 8.1 fps** — the stalls this pan was known for
are the elevation upload, and they are gone. Appearance at the ridge camera (45.244172/5.760595
z13.2 t20): 0.71% of pixels differ at all, 0.49% by more than 12, mean absolute difference
0.36/255 — label placement and tile arrival timing, no rendering change.

### 12.5 What the profile says to do next

- `Bitmap::loadFromUncompressedBytes` copies **one byte at a time** (`Bitmap.cpp:672`). It is off the
  render thread now, but the tile decode threads (28% of the process) run it for every tile bitmap.
  A row-wise `memcpy` is a small, contained change with a wide effect.
- `HillshadeRasterTileLayer::onDrawFrame` at 10.5% of the render thread has not been broken down.
- `TerrainRenderer::updateDepthBuffer` spends 7.7% of the render thread destroying the previous
  depth buffer's CPU meshes; that free belongs on the worker that builds them.

### 12.6 Everything above was measured on an UNOPTIMIZED build

The demo's `debug` variant builds the native SDK with `CMAKE_BUILD_TYPE=Debug`. Nothing sets an
optimization level there, so clang defaults to `-O0`:

```
massif/.cxx/Debug/*/arm64-v8a/CMakeCache.txt:  CMAKE_BUILD_TYPE:STRING=Debug
compile flags for GLTileRenderer.cpp:                    ['-g']          # no -O
```

`-DCMAKE_BUILD_TYPE=Release` is set only on the `release` buildType, which this bench never builds.
The debug variant now passes `RelWithDebInfo` **by default** — Android Studio passes no gradle
properties, so an opt-in flag would have left every run from the IDE unoptimized; `-g` stays, so
simpleperf still symbolizes, and `-PnativeOpt=false` restores `-O0` for native debugging.
Interleaved A/B, same commit, north pan, 3 repeats each, 62 vs 61 windows:

| native build | fps median | frame | prelude | layers | fps p25 |
|---|---|---|---|---|---|
| `-O0` (every number in this document before this section) | 8.6 | 97.3 ms | 17.5 | 26.2 | 7.6 |
| **`-O2` (`-PnativeOpt`)** | **12.8** | **41.6 ms** | **1.7** | **8.8** | **11.5** |

**+49% for a build flag**, and the shape of the frame changes with it: `prelude` collapses (17.5 →
1.7 ms) and `sky` — the swap wait — doubles, i.e. the CPU now hands off to the GPU and waits.

Two consequences, both of which invalidate earlier reasoning rather than earlier arithmetic:

- **§11.2's head-to-head compared their release APK to our `-O0` one.** "18.1 vs 8.3, ours draws
  less and loses 2.2×" is not a like-for-like number. Re-run it with `-PnativeOpt` before drawing
  any conclusion about how far behind tangram we are.
- **The `-O0` profile over-weights whatever the optimizer would have inlined** — `std::vector`
  helpers, cglib's `transform_point`, per-texel lambdas. The two fixes in §12.4 are algorithmic
  (a sort key computed once instead of per comparison; a copy moved to another thread) so they hold
  either way, but a hot list taken at `-O0` is not a list of things to optimize.

Re-profiled at `-O2`, the render thread has no dominant leaf left: it is draw submission
(`renderTileGeometry` 28% inclusive, ~156 draws a frame), `renderGeometry2D` 33%, `prepareFrame`
10%, `startFrame` 9%, and ~9% of JNI (`CheckJNI` is on in a debuggable APK, so that part is an
artifact of the demo, not of the SDK). Which puts the next win back where §4 left it: **fewer
draws and fewer layers**, not micro-optimization.

---

## 13. Contour labels without contour geometry (2026-08-04)

§11.3 said the contour LINES were already free (a fragment block on the terrain paint) while the
LABELS still dragged in a whole contour tile set, and that tangram generates them from the elevation
texture instead. That generator is now ported.

### 13.1 What it does

`ContourTileDataSource.LabelStubsEnabled` makes the source emit, instead of traced contours, a
**short polyline per seed** — tangram's `ContourTextStyleBuilder` (`core/src/style/contourTextStyle.cpp`),
their algorithm and their constants: a 4x4 grid of seeds per tile, each walked down the elevation
gradient onto `round(elev/interval)*interval` (≤12 iterations, interpolating straight onto the level
once it is bracketed, `maxPosErr = 0.25/256`), then along the contour tangent in steps of `2/256`
until the stub is `1.25 * 32/256` long. A stub is exactly long enough to lay the text along.

The features keep the layer name and the `ele`/`div` attributes, so **existing `#contour` text rules
style them unchanged**, and they carry `stub` (1 for a stub, 0 for traced geometry) so the same style
can keep its line rules with a `[stub=0]` filter. Both values are always present: an undefined
attribute does not compare equal to 0, so a one-sided property would silently drop the traced lines.

For the composite layer the parameters are style-driven like the rest
(`contour-label-stubs`, `contour-label-interval` in the `#contour` block), because
`CompositeVectorTileLayer` applies the source's generation parameters from the resolved style.

### 13.2 The trap: the levels have to match the shader

The stub levels must be the levels the *shader* draws (`hillshade-contour-interval`), or the labels
sit between the lines. Tangram has the same note in their source. In the demo both come from
`DemoConfig.HILLSHADE_CONTOUR_INTERVAL` / `CONTOUR_LABEL_INTERVAL`.

And in a COMPOSITE base the hillshade slot takes its contour settings **from the style**, not from
`HillshadeRasterTileLayer` setters — those only reach a stand-alone layer. `--es hsContours true`
therefore did nothing until the inline style grew a `hillshade-contour-interval`, which is why a
first pass showed stubs and labels but no lines at all.

### 13.3 Measured (emulator, structural - fps there means nothing)

Ridge camera 45.244172/5.760595 z13.2 t20, composite base, hillshade + contours, per one-second
interval:

| | traced contours | label stubs |
|---|---|---|
| geometry draws | 2035 | **1217** (−40%) |
| geometry indices | 51.6 M | **29.4 M** (−43%) |
| render tiles | 1197 | **712** (−41%) |
| style layers | 108 | **66** |
| surface draws | 828 | **506** |

Visual check at the same camera: the lines are the shader's, the labels sit on them, and no stub
fragments are painted (the `[stub=0]` filter).

**Device numbers are still to take** — the interleaved A/B on the Crosscall has not been run for
this. Do it with `bench/north.sh` and `--es contourStubs true --es hsContours true` against the
traced arm.

### 13.4 Left to do

- Device A/B, at the north pan and at a settled ridge camera.
- Label density: tangram's 4x4 grid per tile is theirs, but our tiles are not their tiles - if the
  labels come out too sparse or too dense, `gridSize` is the knob to expose, not the interval.
- The stubs do not follow the DEM's own zoom ladder: `LabelInterval` is one value for all zooms
  (0 falls back to the traced-geometry ladder). Tangram switches 100/200/500 m by zoom.

---

## 14. The contour stubs on device, and what the DEM path actually costs (2026-08-04)

### 14.1 Contour label stubs: +14% on device

Interleaved, one APK (both arms are intent extras), north pan, hillshade + contours,
3 repeats, 63 one-second windows each:

| | fps median | frame | `layers` | p25 |
|---|---|---|---|---|
| traced contour geometry | 14.5 | 42.4 ms | 8.7 ms | 12.6 |
| **label stubs + shader lines** | **16.6** | **38.8 ms** | **7.0 ms** | **14.0** |

This is §13 measured where it counts. Run it with
`--es contourStubs true --es contourStubInterval 100 --es hsContours true --es hsContourInterval 100`.

### 14.2 The elevation texture pipeline is IDLE in steady state

The re-encode churn §9.4 worried about does not exist in a warm pan. Counters over 12 s at the
north pan, warm cache of 22 textures:

```
PROBE demtex: fullEncodes=0 borderPatches=0 cache=22      (every second, for the whole run)
```

So `ElevationTextureCache::getTexture` + `resolveEntry` at ~9% of the render thread (§12) is
**lookup** cost - the per-frame resolution and the 9 locked cache lookups a new tile needs - not
encode cost. Optimising the encode cannot move a warm frame, because it does not run.

Where it DOES run is a cold load and a zoom sequence, and there the border patch (§14.3) removes
most of it.

### 14.3 Border patching: 93% less work, 0% more fps

`ElevationTileGrid::encodeTextureBorders` + `ElevationTextureCache::applyBorderPatches`: when a
neighbour lands, only the 2-texel ring is re-encoded and patched into the existing texture.

| cold load + zoom sequence | full re-encodes | border patches | fps median |
|---|---|---|---|
| re-encode whole (`debug.massif.demborderpatch 0`) | **353** | 0 | 10.6 |
| **patch the ring** | **24** | 118 | 10.5 |

And at every camera tried the frame rate is unchanged: north pan 14.6 vs 15.0, cold 10.5 vs 10.6,
full DEM detail 10.1 vs 10.7 - all inside the noise. The encode was already on a worker and the
upload already budgeted, so the render thread never saw it. Kept as a work/battery/bandwidth
reduction, recorded here as **not** an fps change so nobody re-measures it expecting one.

### 14.4 Full DEM detail is no longer catastrophic - but still not free

§9.4 measured full-detail elevation at **2.5 fps against 6.7**. With `-O2`, the stubs and the paint,
the same switch now measures:

| | fps median | frame | `layers` |
|---|---|---|---|
| mesh-capped DEM (default) | 13.9 | 38.1 ms | 7.1 ms |
| `debug.massif.paintdetail 1` | 10.4 | 67.0 ms | 21.1 ms |

−25%, not −63%. Sharp high-zoom relief is now a plausible option for a stack that wants it, but not
a default. The old numbers in §9.4 should be read as historical.

---

## 15. The session that halved the terrain frame (2026-08-05)

Four changes landed, all measured interleaved on the Crosscall (Adreno 610), north pan into
the mountains with hillshade + contour stubs unless stated. Together: **14.8 -> 23.0 fps.**

### 15.1 The lattice clamp does not belong on the surface (+34%)

`applyTerrain` snaps draped geometry to the regular grid by sampling the four surrounding grid
nodes, each a 4-tap manual bilinear: **16 texture fetches per vertex**. The surface's own
vertices ARE those nodes, so the clamp returns the node's own height for four times the cost -
and the surface is the bulk of the vertex work in a terrain frame. It now takes the plain
sample; tiles stitched to a coarser neighbour keep the clamp, because there it is what bends
the outermost cell onto the neighbour's chords.

| | fps | GPU frame |
|---|---|---|
| before | 14.8 | 31.5 ms |
| after | **19.9** | **24.9 ms** |

0.06% of pixels differ at the ridge camera. (libs-massif `ad51cb0`)

### 15.2 The elevation sampler was lowp - the device-only hillshade bug

`uniform sampler2D uElevationTexture` carried no precision qualifier, and GLSL ES 1.00 defaults
sampler2D to **lowp**. `texture2D()` therefore returned ~8 bits and threw away the low byte of
the 16-bit height. Geometry survived it; the hillshade takes a GRADIENT of that height field,
which amplifies the quantisation into flat texel-sized facets - the corduroy Martin saw on the
device and never on the emulator, where desktop GL computes lowp as fp32.

Costs ~2% (19.7 -> 19.3 fps). The vertex stage sampled at lowp too, so terrain displacement was
quantised on device as well - worth remembering for the device-only see-through this branch
chased for weeks. (libs-massif `fdbefcb`)

### 15.3 No stencil tile masks in a terrain frame (+21%)

Tangram has no stencil anywhere in `core/src`. What decides it here is what a mask COSTS, and
that differs by an order of magnitude between the paths: in a terrain frame a mask is a full
displaced grid per tile per stencil reset, in 2D it is a two-triangle quad.

| | fps | GPU layers | surface draws / interval |
|---|---|---|---|
| 3D, masks on | 19.5 | 5.5 ms | 1600 |
| 3D, masks off | **23.5** | **2.6 ms** | **650** |
| 2D, masks on | 40.2 | 2.1 | 1893 |
| 2D, masks off | 40.7 | 1.9 | 690 |

`setTileMasks` now takes three states and defaults to automatic: off in a terrain frame, kept
in 2D, and kept in both when any layer composites through a `comp-op` (the overlay buffer has
its own stencil and no depth, so nothing else clips that layer to its tile).
`debug.massif.tilemasks 1|0` forces either way. (libs-massif `be51df2`, mobile-sdk `cb702b0dc`)

### 15.4 Contour label stubs read the terrain's elevation (CPU only)

`ContourTileDataSource.setTerrainOptions` hands the source the terrain's elevation manager, and
the stubs are walked over the grid the terrain has already fetched and decoded - tangram's
arrangement (`core/src/style/contourTextStyle.cpp` reads the tile's own elevation raster). Ours
was loading and WebP-decoding a second copy of the same tile, plus up to three neighbours, to
build a full height grid for something that needs a few hundred samples.

Whole process, 14 s of the north pan: `ContourTileDataSource::loadTile` **10.3% -> 0.1%**,
`Bitmap::loadWEBP` 15.2% -> 11.8%, `FetchTaskBase::run` 27.2% -> 18.6%. **No UX gain measured**:
warm and cold-cache pans both show the same fps, p25 and hitch count, because the tile threads
are not the constraint at this camera. Keep it for battery and for heavier stacks, not for
smoothness. Traced contour geometry keeps its own decode - it needs the DEM at the source's
resolution, which the terrain's mesh-capped level cannot supply. (mobile-sdk `855f3a566`)

### 15.5 Where the frame is NOT, measured this session

- **The sky and the map background.** The GPU profiler's first section reads ~7 ms in every
  configuration, which is mostly the idle it absorbs (the caveat in `FrameProfiler.h`). Timed
  apart: the sky quad is ~1.5-1.8 ms and the background plane 2.8-4.7 ms, and **removing either
  changes fps by nothing** - 2D holds 41.3 fps with a 10.4 ms GPU frame or a 5.7 ms one.
  `debug.massif.background 0` drops the plane. Tangram has no background geometry at all and
  draws its sky only above the horizon (`core/src/util/skyManager.cpp`, mesh y in [0,1]
  translated by `u_horizon_y`); both are worth copying as simplicity, not as frame rate.
- **Labels.** `buildMs 0.0 batchMs 0.0`, pass3D labels2D 0.2 ms/interval; `--es labels false` is
  +3% in 3D and +3.4% in 2D. The culler runs on its own worker.
- **Tile stitching, fog, the GPU timer itself.** Stitching is the shader's edge coarsening (and
  now only on edge tiles); fog costs 4.5% and is off by default; `debug.massif.gputimer 0` is
  worth nothing, so the instrumentation is not paying for itself in frames.

### 15.6 The device presents at 43 Hz, not 60 - and so does tangram

A nearly-empty 2D frame (no terrain, sky, background, labels, minimal style) still runs at
**42.5 fps, with 1.5 ms of work and 17.8 ms of swap wait**. SurfaceFlinger's present-to-present
histogram for our layer is a steady **23 ms on 942 of 1000 frames**, zero dropped, zero janky -
not a multiple of the 16.7 ms the panel reports.

**Tangram's demo on the same device presents at 23 ms too** (61 of 74 frames). So ~43 Hz is the
Crosscall's ceiling for a fullscreen GL surface, not our pacing, and 2D at 41 fps is already
against it. Two consequences: cutting 2D work on this device cannot show up as frame rate, and
any future fps comparison has 43, not 60, as its ceiling.

---

## 16. The 2D city frame, profiled for the first time (2026-08-18)

Every 2D number before this round was taken at the mountain camera, where 2D runs at 41 fps against
the device's 43 Hz present ceiling — so [15.6](#156-the-device-presents-at-43-hz-not-60---and-so-does-tangram)
concluded that "cutting 2D work on this device cannot show up as frame rate". That holds **only for
that camera**. The 2D *city* has 20 fps of headroom under the ceiling and had never been measured.

Method: Crosscall HLTE556N, Grenoble 5.724/45.188 z16.22 tilt 26, `--es terrain false --es base
composite --es style assets`, scripted north pan (`--es anim pan --es animLatDelta 0.06`),
`bench/city2d.sh`, medians over 38 one-second windows per arm, arms interleaved over two rounds.

### 16.1 It is CPU-bound on draw submission, not on fragments

| | ms |
|---|---|
| CPU frame | **51.9** |
| GPU frame | **20.4** |
| fps | 17.6 |

CPU sections: swap wait 12.2 · `layers` 21.5 · `layers3D` 10.1.
GPU sections: sky 5.5 · background 3.4 · layers 9.8.

Per frame: **606 geometry draws**, 1.57 M indices, 74 label draws, 62 stencil mask draws, 23
per-tile background quads — **765 draws**. Label work on the GL thread is 9.6 ms/frame (2D pass
4.97, 3D pass 2.14, build 2.50); masks 1.07.

`simpleperf` over the GL thread (4144 of 12 802 samples) puts **60% of it outside our code**:

| shared object | % of the GL thread |
|---|---|
| `libGLESv2_adreno.so` | **36.1** |
| kernel | 24.1 |
| `libmassif.so` | 23.9 |
| `libc` | 7.1 |

Inclusive: `renderGeometry2D` 53.8 → `renderTileGeometry` **45.3** · `renderLabels` **25.0**
(`renderLabelBatch` 11.0, `Label::calculateVertexData` 8.2).

This is the 3D city's answer inverted. There the frame is GPU-bound and the lever was triangles
([10-performance.md](rendering/10-performance.md#the-undraped-line-cost-is-triangles-not-pixels-and-not-shading));
here the GPU is idle-ish and the lever is the **number of draws**.

### 16.2 The A/B that proves it

| arm | fps | CPU frame | GPU total |
|---|---|---|---|
| base | 17.6 | 51.9 | 20.4 |
| background plane off (`debug.massif.background 0`) | 18.1 | 50.9 | **16.0** |
| stencil tile masks off (`debug.massif.tilemasks 0`) | 18.3 | **49.5** | 20.9 |
| 3D buildings off (`--es bld3d false`) | 17.9 | 49.7 | 20.5 |

Two results, read together:

- Removing the background plane takes **22% off the GPU frame and buys nothing** — 4.4 ms of GPU
  slack, confirming the frame does not wait on the GPU. The sky and the plane remain what
  [15.5](#155-where-the-frame-is-not-measured-this-session) said they were: simplicity, not frame rate.
- Removing the masks removes **62 of 765 draws (8%) and buys 4%** — roughly proportional. Draws are
  the currency.

**Do not re-run `--es labels false` at this camera**: that knob only strips the *inline* style's
text rules, and this run uses `--es style assets`, so `labelDraws` was unchanged at 71/frame. The
arm looked like +5% and measured nothing. Labels are priced by the profile above (25%) instead.

### 16.3 `-PprofileRender` costs 13% — every CPU ms on this page is that much high

`PROF` cannot compare two builds, so this is SurfaceFlinger `totalFrames` over an identical 20 s
window, three interleaved pairs:

| build | totalFrames |
|---|---|
| plain | 415 · 415 · 424 |
| `-PprofileRender` | 363 · 366 · 371 |

**13%, with no overlap between the arms** — `steady_clock::now()`/`clock_gettime` is 19.4% inclusive
of the GL thread in the instrumented build. The shipped 2D city therefore sits at **~20 fps**, and
any absolute CPU figure taken with the profilers on should be discounted by ~13% before it is
compared with anything else.

### 16.4 Found while benching: the label batch drew from a freed glyph atlas

About half the startups at this camera died, as either a null `bitmap` in `renderLabelBatch` or
`Scudo ERROR: invalid chunk state when deallocating`. `GlyphMap::getBitmapPattern()` returns the
pattern **by value** and `renderLabelPass` bound a reference through that temporary's `->`, which
extends nothing; a tile thread loading a glyph resets the map's pattern, so the render thread's
temporary could be the last owner. 6/6 clean starts after the fix, 3/6 before.
(libs-massif `83da937`, mobile-sdk `064ad2254`, [06-labels.mdx](rendering/06-labels.mdx#on-the-gl-thread))

### 16.5 What this round retires, and what is left

Retired as levers for the 2D city on this device:

- **An opaque/translucent depth split** (maplibre's `renderPass` model). 2D disables the depth test
  entirely, so there is no early-Z at all — but the frame is not fill-bound, and 4.4 ms of GPU slack
  says early-Z has nothing to win here. Worth revisiting only on an immediate-mode GPU.
- **The sky quad and the background plane.** 8.9 ms of GPU, 0 fps.
- **Per-tile background meshes in 2D.** Tangram has none and dropping them is right, but they are
  23 of 765 draws — 3%, not the frame.

Left, in order: **merge geometry draws** (606 draws for 62 render tiles × 32 style layers is the
frame), **drop the masks in 2D** (62 draws, +4%, `setTileMasks` already has the switch), and the
**per-frame label batch rebuild** (25% of the GL thread, already open in
[06-labels.mdx](rendering/06-labels.mdx#a-persistent-label-batch-and-what-blocks-it)).

### 16.6 Merging the polygon-pattern draws: 584 -> 363 draws, +13% fps

[16.1](#161-it-is-cpu-bound-on-draw-submission-not-on-fragments) said the currency is draws, so the
next question was which of them are avoidable. A probe counting draws per (tile, style layer) pair:

| per frame | |
|---|---|
| geometry draws | 584 |
| pairs that drew anything | **337** (the floor a per-tile merge can reach) |
| extra geometries inside a pair | **285** (49% of the draws) |

and, by why the pair split:

| reason | per frame |
|---|---|
| **pattern differs** | **278** |
| already mergeable | 4 |
| geometry type differs | 3 |
| same atlas grown between packs | 0 |

Every sampled pair was the same shape — `type=3 params=1/1 pattern '64x64' -> 'none'` — a POLYGON
style layer alternating a `polygon-pattern-file` fill with a plain one, one style slot each. Not two
competing patterns: **a pattern against no pattern**.

That killed the atlas design this was scoped as. An atlas (mapbox's `fill-pattern` model) would have
needed `fract()` into a sub-rect, 1-texel wrap padding and the loss of `GL_REPEAT` and of the
mipmaps polygon patterns currently get. A **per-slot pattern flag** removes the same 278 splits with
none of that: `patternScales` in the style table, `uPatternTable` in the shader, packed into `vUV.z`
so it costs no extra varying vector, and `mix(vColor, texture2D(...) * vColor, vUV.z)` in the
fragment shader. Only a second, *different* pattern still splits.

Interleaved, three rounds, ~59 one-second windows per arm:

| | fps | CPU frame | `layers` CPU | GPU total | GPU layers |
|---|---|---|---|---|---|
| before | 17.9 | 51.1 | 21.9 | 20.2 | 9.7 |
| after | **20.3** | **43.2** | **15.1** | 18.4 | 7.6 |

**584 -> 363 draws a frame (-38%), +13.4% fps**, and the `layers` section — the one that submits them
— down 31%. The GPU moved too (layers 9.7 -> 7.6 ms) because a draw carries its uniform uploads and
its texture bind with it, but the GPU was never the bound here.

Verification: 0.28% of pixels differ at a landcover camera (z13.5, forest/scrub/water patterns on
screen), all of it label churn between runs — the same 0.18-0.28% shows up comparing two runs of the
*same* build. 3D terrain re-checked at the ridge camera on the INLINE style, unchanged;
assets-over-terrain was not re-shot.

Two notes for whoever goes further. The floor is **one draw per (tile, style layer)**; 363 against a
floor of 337 means the remaining splits are the type/comp-op/16-slot ones, which are not worth
chasing. Below the floor needs cross-tile batching (one VBO per style layer with per-vertex tile
offsets), which nothing in this renderer does and which the tile lifecycle makes expensive.

### 16.7 Label batches: the floor is one glyph atlas per (font, render size)

With the geometry draws down to their floor ([16.6](#166-merging-the-polygon-pattern-draws-584---363-draws-13-fps)),
the next item on the 2D city frame is labels: `renderLabels` is **25% of the GL thread** in the
simpleperf profile (`renderLabelBatch` 11%, `Label::calculateVertexData` 8.2%), and the frame issues
**65 label draws**, each re-specifying five or six VBOs with `glBufferData`.

A probe on why a batch ends:

| break reason | per frame |
|---|---|
| **glyph atlas (bitmap) changes** | **33.1** |
| **the style carries a `transform`** | **25.6** |
| parameter table full | 3.4 |
| scale / glyph render size | 0.0 |

The obvious read — labels arrive in culler order, so the atlas thrashes A→B→A — is **wrong, and the
experiment that would have exploited it is a dead end**. A stable sort of the pass by
`style->glyphMap` before batching (`debug.massif.labelsort`, reverted) moved the atlas breaks
**33.0 → 31.8** and the label draws 70.8 → 68.6. The atlases really are distinct: `FontManager`
keys `_glyphMapMap` by the **full font name including its query parameters**, so every
(font, glyph render size) pair owns its own `GlyphMap` — and with the 16/28/40 raster ladder a
handful of font families becomes ~32 atlases on screen. Sorting can only ever reach
one break per distinct atlas, which is what it did.

Do not read a frame rate off that A/B either: it measured 19.1 fps against 20.8, and the same build
had measured 20.3 an hour earlier in [16.6](#166-merging-the-polygon-pattern-draws-584---363-draws-13-fps).
With the break counts flat there is no mechanism behind the difference — it is the session drift
[Getting a trustworthy number](rendering/10-performance.md#getting-a-trustworthy-number) warns about,
caught here only because the counters disagreed with the fps.

So the label batching floor is the number of distinct (font, render size) pairs on screen, and the
only way under it is **one shared glyph atlas for every font**, which is mapbox's model and a
`FontManager` change (`_glyphMapMap` → a single map, bounded by `_maxGlyphMapWidth/Height`). Not
attempted here. The `transform` breaks are a second, independent 25/frame: a style transform is
folded into the batch's `labelMatrix`, so a transformed label cannot share a batch — applying it to
the vertices in `calculateVertexData` instead would let it.

Worth remembering before either is attempted: the 3D label batch **cache** removed the work it
targeted and bought **zero frames** ([06-labels.mdx](rendering/06-labels.mdx#a-persistent-label-batch-and-what-blocks-it)),
and was reverted for correctness. Fewer label draws may go the same way — price it against the frame,
not against the counter.

### 16.8 A depth model for the flat map: built, measured, reverted

The 2D pass disables the depth test entirely, so every fragment of every style layer is shaded and
nothing can be rejected early. maplibre splits its frame into an opaque pass (depth-writing) and a
translucent one; this round built that and threw it away.

What was built (`debug.massif.depth2d`, reverted): a `DEPTH_2D` shader flag enabling
`applyDepthBias`' painter-order term outside terrain mode, a per-style-layer ordinal handed out
across the whole stack by `MapRenderer::drawLayers` (composite children included, so the ordinals
span them), and two passes in `renderGeometry2D` — opaque fills first in **reverse** style layer
order with depth write, everything else after in painter order testing against them, `GL_LEQUAL` in
both because a layer's background and its geometry are coplanar at the same ordinal. Only a fill can
be opaque: a line and a point antialias their own edges.

It renders correctly — 0.382% of pixels differ at the city camera, the same order as the label churn
between two runs of one build.

| arm | fps | CPU frame | `layers` CPU | GPU total | GPU layers | mask draws / frame |
|---|---|---|---|---|---|---|
| baseline | 20.1 | 43.4 | 15.3 | 18.4 | 7.8 | 60 |
| **depth2d** | **19.6** | 44.8 | 14.2 | 18.0 | 7.5 | 60 |
| depth2d + no masks | 20.5 | 42.0 | **12.0** | 18.1 | 7.4 | 0 |

**Early-Z on its own is a net loss.** It did exactly what it was designed to do — GPU `layers`
7.8 → 7.5 ms — and that is 0.3 ms off a GPU which [16.2](#162-the-ab-that-proves-it) had already
shown to be idle by 4.4 ms, while the opaque classification and the second pass put 1.4 ms back on a
CPU-bound frame. The arithmetic said this before the code was written: **a fragment optimisation
cannot move a frame whose GPU is not the critical path**, and the right move would have been to stop
at that sentence.

The second motivation was better founded and still did not pay. In a terrain frame, content writing
depth is what **replaced the stencil tile masks** ([05-depth-model.md](rendering/05-depth-model.md)),
and the masks are 60 draws a frame in 2D. Removing them with the depth model underneath is a real
mechanism — `layers` CPU **15.3 → 12.0, −22%** — but it surfaces as **+2% fps**, and this bench's
cross-session spread on an unchanged baseline has been 19.1–20.3. Two percent is not distinguishable
from it.

So the flat map keeps its stencil masks and its painter order. Two things a future attempt should
know: the proxy-tile behaviour of the mask-less arm was **never checked** (only a pan was run, and
what the masks protect against is a retained tile painting through the gaps of its replacement
during a **zoom**), and the early-Z result is specific to a tiler with idle fragment capacity — on
an immediate-mode GPU it could read differently, which is not measurable from here.
### 16.9 Label draws: the style transform folded into the vertices (-43%)

[16.7](#167-label-batches-the-floor-is-one-glyph-atlas-per-font-render-size) left two reasons a
label batch ends: the glyph atlas changing (33 a frame) and the style carrying a `transform` (26).
Both were implemented; only one shipped.

**The transform fold shipped.** The translate was a factor of the batch's `labelMatrix`, so a label
carrying one was always a draw of its own. Conjugated by the tile matrix it is a *pure world
translation*, so it is now added to that label's vertices after `calculateVertexData` and the label
batches normally. Interleaved, three rounds, 58 one-second windows per arm:

| | fps | CPU frame | label draws / frame | `labels2D` |
|---|---|---|---|---|
| before | 19.9 | 45.0 | 68.4 | 4.89 ms |
| after | 20.1 | **43.2** | **38.7** | **4.24** |

The frame rate is inside the noise; the CPU frame and the draw count are not. It also fixes a latent
bug — consecutive same-style labels *did* share a batch, and its matrix was built from the **first**
label's tile, so the others were translated by the wrong tile's frame.

**The shared glyph atlas did not ship.** Keying `FontManager::_glyphMapMap` by glyph render size
instead of by full font name collapses ~32 atlases to the ladder's three and takes the draws to
**32.9** — and measured **no frame rate change** (CPU frame 43.6 → 43.7 over three rounds). It also
buys a new failure mode: a 2048² atlas is ~1764 cells at render size 40 against ~200 glyphs per
family, so shared across six families it runs ~68% full, and `GlyphMap::loadBitmapGlyph` returns 0
once it is full — the glyph silently disappears. A silent-text-loss risk for an unmeasurable gain is
the wrong trade; widen the atlas and measure its fill before retrying.

Method note worth keeping: the fold's screenshot diff read **0.536%**, well above the 0.18–0.38% this
camera usually shows, and it was **entirely label placement churn**. Running the same build twice
measured **0.533%**, and a second run of it matched the baseline to **0.032%**. At a label-dense
camera the churn floor is half a percent — establish it with a same-build control before reading a
screenshot diff as a regression.
---

## 17. Lighting the undraped 2D content (2026-08-18)

Contours (and any `NoDrapeLayerFilter` layer, and every 2D layer when the drape is off) were the
only content in a lit scene still drawn at flat style colour: the sun is applied by the surface
draw that samples the drape texture, and these are drawn live instead. `GEOMETRY_LIGHT` gives the
point/line/polygon programs the surface's own normalised Lambert, N·L from the terrain normal.
Mechanism and the "why not a cheaper normal" in
[rendering/08-lighting-sky-fog.md](rendering/08-lighting-sky-fog.md#undraped-2d-content-takes-the-same-sun).

**Method.** Crosscall HLTE556N (Adreno 610), two prebuilt APKs (no rebuild inside a pair), massif
z13.6 / tilt 55 / lat 45.2442 lon 5.7606, contours over the whole frame,
`--es terrainLight true --es ambient 0.35 --es sunHour 8`, 16 pan swipes after a 60 s settle, `PROF`
windows longer than 1600 ms discarded, two cycles per configuration.

| Configuration | GPU `layers` | GPU total | fps |
|---|---|---|---|
| shadows on (0.3), before | 4.83 / 4.87 ms | 32.4 / 36.6 ms | 15.1 / 15.7 |
| shadows on (0.3), after | 4.88 / 4.78 ms | 35.0 / 32.2 ms | 16.1 / 15.3 |
| shadows off, before | 1.29 / 1.30 ms | 18.8 / 18.8 ms | 20.8 / 22.0 |
| shadows off, after | 2.55 / 2.47 ms | 20.2 / 20.0 ms | 20.6 / 20.3 |

- **With shadows on the sun is free.** The 3×3 DEM stencil was already being run per fragment for
  the shadow's slope bias; the lighting reuses that `ndl`. Both cycles land inside the run-to-run
  spread, in opposite directions — no delta is claimed.
- **With shadows off it costs the stencil**: `layers` doubles, +1.2 ms on a 19 ms GPU frame (+6%),
  fps 21.4 → 20.5. Repeatable across both cycles, unlike the shadowed pair.
- Reading `layers` rather than fps is what makes this measurable at all: the device presents at
  43 Hz (§15.6) and the frame here is far from that ceiling, so ±1 fps of run-to-run noise swamps
  a 1.2 ms section change.

---

## 18. Phase 4 opened by measuring first, and the first two items died (2026-08-19)

[Phase 4](rendering/16-graphics-api-migration.md#phase-4--harvest-closed-nothing-shipped) lists five ES 3.0 harvests
"ordered by expected payoff". Nothing had been measured against that order. Method: Crosscall
HLTE556N (Adreno 610), `bench/city2d.sh` — Grenoble 5.724/45.188 z16.22 tilt 26, terrain off,
composite base with the bundled style project, scripted north pan, `-PprofileRender`.

Baseline, 22.1 fps, frame avg 39.9 ms:

| CPU section | ms/frame | | GPU section | ms/frame |
|---|---|---|---|---|
| `sky` | 12.2 | | sky | 5.4 |
| `layers` | 14.1 | | background | 3.6 |
| `layers3D` | 11.4 | | layers | 7.3 |
| labels 2D + 3D | 3.4 + 2.2 | | layers3D | 1.4 |
| **`billboards`** | **0.1** | | **billboards** | **0.0** |

`sky` at 12.2 matches §16's "swap wait 12.2" exactly, so it is absorbing the vsync wait rather than
costing that much itself.

### 18.1 Item 1 (instancing billboards/markers) is dead at this camera

**0.1 ms CPU, 0.0 ms GPU.** There is nothing to win. Its premise is also already satisfied:
`BillboardRenderer::BuildAndDrawBuffers` draws `drawDataIndex * 6` indices in one
`glDrawElements` per buffer-full, so billboards are *already* one draw per batch, not one per quad.

What instancing would actually change here is different from what the issue says: the renderer
builds four corners per billboard per frame and hands them over as **client-side vertex arrays**
(`coordBuf.data()`, no VBO). That is a real cost — but only in a scene that has billboards. This
camera has none, so any instancing work needs a marker-heavy bench built first, and the payoff
would be for marker-heavy apps rather than for the map.

### 18.2 Item 5 (label vertex streaming) measures as a no-op

`renderLabelBatch` re-specifies six buffers per batch with
`glBufferData(..., GL_DYNAMIC_DRAW)`. Replaced with ES 3.0 orphaning — `glBufferData(size, nullptr,
GL_STREAM_DRAW)` then `glMapBufferRange(GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
GL_MAP_UNSYNCHRONIZED_BIT)` and a `memcpy` — behind `debug.massif.labelorphan` so one APK does the
A/B. Interleaved OFF/ON over two rounds, 41 one-second windows per arm:

| arm | fps median | IQR | CPU frame | GPU total |
|---|---|---|---|---|
| off | 19.80 | 17.65–22.80 | 43.4 | 18.3 |
| orphaned | 20.00 | 17.90–21.90 | 44.0 | 18.2 |

**The same arm differs more between rounds than the arms differ from each other** — `off` measured
18.90 in round 1 and 19.80 in round 2, 4.5× the 0.2 fps between arms, against a stdev of 5.2. No
effect.

Why: there are ~25 label batches per frame carrying ~245 labels, so each of the six buffers is a
few KB. `glBufferData` of a few KB is already cheap, and orphaning solves a pipeline stall that at
that size does not happen. The change was reverted rather than shipped — a no-op behind a debug
property is complexity for nothing.

Note also that `labelBuild attribMs`, at 17.4 **per interval**, is 0.76 ms/frame, not 17.4 — and
`Label.cpp`'s `labelAttribNs` clock covers CPU filling of the `VertexArray`s, which buffer mapping
does not touch at all. Reading a per-interval stat as per-frame is the easy mistake here; every
`RenderStats` line ending `(per interval)` covers the whole ~1 s window.

### 18.3 What the numbers say to do instead

The frame is CPU-bound on draw submission (§16: 36% of the GL thread inside `libGLESv2_adreno.so`),
and the per-draw breakdown is `draw=10.1 µs` against `styleEval=3.5`, `styleUpload=2.1`,
`compile=3.3`, `bind=1.6`. At **320 geometry draws/frame** — 63 tiles × ~5 style layers with content
— the driver's own per-draw cost dominates.

Of the remaining Phase 4 items, only **UBOs (item 4)** aims at any of that, and it targets
`styleUpload`: 320 × 2.1 µs ≈ **0.67 ms/frame, ~1.7% of the frame**. Worth doing, but it will not
move fps on this device, and it should be judged on `layers` rather than fps for the reason in
§17.

The lever the data actually points at — merging geometry across tiles per style layer, to cut 320
draws — **is not in Phase 4's list**. Item 3 (packed attributes) does not deliver it either: the
draws come from the tile × style-layer structure, not from the 16-bit index cap.

### 18.4 The terrain camera says shadows are 55% of the GPU frame

The city bench has shadows off, which is why §18 found nothing to optimise in it. Re-measured at
the mountain camera (Saint-Eynard, 5.760595/45.244172 z13.2 tilt 55, `--es terrain true --es shadow
0.6 --es terrainLight true --es bld3d true`), same device and profiler build:

| | ms/frame |
|---|---|
| CPU frame | 36.0–38.8 |
| GPU frame | **27.5–28.3** |
| fps | 15.7–16.1 |

GPU sections: sky 4.5 · background 1.2 · layers 6.2 · layers3D 0.3 · **shadowCast 8.3–9.0** ·
**shadowMask 6.9**.

**The shadow pass is 15.2–15.9 ms, 55% of the GPU frame.** That is the largest single measured cost
found anywhere in this phase, and it makes Phase 4 item 2 — shadow cascades as a texture array — the
only one of the five with a measured case behind it.

What an array is expected to buy, and what it is not:

- **Not** `shadowCast`. That cost is casters × cascades; the render target's shape does not change
  how much geometry is drawn.
- **Possibly** `shadowMask`, which samples the `_size * _cascades` wide atlas with manual slice
  offsetting and clamping per cascade. An array indexes the layer directly, so the offset maths and
  the clamp both disappear.
- **Certainly** the texture-size cap: 3 × 1024 is a 3072-wide texture today, and 4 × 2048 would be
  8192, at or over `GL_MAX_TEXTURE_SIZE` on this class of device. An array removes that ceiling, so
  it is a robustness fix regardless of what it does to the frame.

Measure it build-to-build (`bench/abapk.sh`) rather than behind a property: a second shadow-map path
kept alive only for the A/B is exactly the flag-driven duplication the working agreement warns off.

### 18.5 Shadow cascades as a texture array: 28% SLOWER on the Adreno 610

Phase 4 item 2, implemented and measured. Each cascade became one layer of a
`GL_TEXTURE_2D_ARRAY` (`glTexStorage3D`, `glFramebufferTextureLayer` per cascade) instead of a page
of one `_size * _cascades` wide atlas; the receiver sampled `sampler2DArrayShadow` and the atlas
scale/offset in `shadowFactorSlope` disappeared. It rendered correctly — shadows ACTIVE, no GL
errors, no shader failures, cast shadows visually right.

Two APKs, interleaved over two rounds, mountain camera with terrain, shadows and 3D buildings,
25 one-second windows per arm:

| arm | shadowCast | shadowMask | GPU total |
|---|---|---|---|
| atlas | 7.80 | 6.70 | 20.00 |
| **array** | **8.80** | **10.30** | **25.70** |
| delta | +1.00 (+12.8%) | **+3.60 (+53.7%)** | **+5.70 (+28.5%)** |

Consistent across rounds, and the atlas arm is the stable one: mask 6.70/6.70 against the array's
8.70/11.30.

Where it goes:

- **`shadowMask` +53.7%.** The receiver does four PCF taps per fragment. On this Adreno,
  `sampler2DArrayShadow` is evidently off the fast path that `sampler2DShadow` is on — the atlas
  arithmetic that was removed (one multiply-add per tap) is far cheaper than whatever the array
  fetch costs instead.
- **`shadowCast` +12.8%.** `glFramebufferTextureLayer` per cascade re-attaches the target three
  times per pass where the atlas set a viewport; the driver appears to treat each as a real target
  change.

**Reverted, not shipped.** The change is *correct* and it does remove the size cap (`setSize` no
longer has to divide `GL_MAX_TEXTURE_SIZE` by the cascade count, so 4 x 2048 becomes possible), but
28% of the GPU frame is not a price worth paying for a cap nothing currently hits.

Worth knowing before anyone tries again: this is a per-GPU result. A desktop or Apple GPU may not
share the Adreno's array-shadow penalty, so if the cascade count ever needs to exceed what the
atlas can hold, re-measure rather than assume this verdict travels.

### 18.6 Phase 4, so far: three items measured, three negative

| Item | Verdict on the Adreno 610 |
|---|---|
| 1. Instancing | 0.1 ms CPU / 0.0 ms GPU at the city camera — nothing to win, and already batched |
| 2. Shadow cascades as a texture array | Implemented; **+28.5% GPU**. Reverted |
| 5. `glMapBufferRange` label streaming | No-op; buffers are a few KB. Reverted |

That is not a failure of the phase, it is the phase working: the list was written from what ES 3.0
*offers* rather than from what this renderer *spends*, and measuring first cost three short
experiments instead of three shipped regressions. What the numbers keep pointing at — 320 draws per
frame in the city, 55% of the terrain GPU frame in shadow rendering itself — is not on the list.

## 19. The buildings' contact shadow, on the device

Crosscall (Adreno 610), Grenoble city centre `5.724807 / 45.190814`, rotation 60.2, z17.7, tilt 53,
`--es bld3d true`. `-PprofileRender`, per-frame `PROF GPU` section averages. The `fps` on the `PROF`
line is meaningless here — the map is idle under WHEN_DIRTY and only a handful of frames are
sampled — so only the GPU section averages are usable.

### Screen-space path (`--es drape false`)

| | `groundAO` | GPU total |
|---|---|---|
| `bldAoRadius 0` | 1.4 ms | 11.1 / 10.7 |
| `bldAoRadius 3` | 2.7 / 2.8 ms | 11.8 |

Repeated in a bracket (off / on / off / on); both pairs agreed.

**1.4 ms of that was the empty pass.** `bldAoRadius 0` removes the geometry but not the work:
`isGroundAOActive` tested the style intensity only, so the mask target was bound and cleared every
frame with nothing to draw. That is the bare framebuffer round trip on a tiler, and it is the same
cost whatever resolution the mask is — measured earlier at full and quarter resolution with under
2 fps between them. `isGroundAOActive` now also requires a visible tile that actually has
`POLYGON3DGROUND`, and the off case measures **0.0 ms**. The capsules themselves are the ~1.3 ms
on top.

### Drape path (`--es drape true`)

`groundAO` is 0.0 in every run, and AO on/off do not separate: 12.3 / 11.2 / 12.5 / 10.7 across a
four-run bracket, with "on" cheaper than "off" both times. The work happens at bake time and is
cached, so there is no per-frame cost to find; what varies is how many tiles happened to be baking
in the sampled window.

This is the argument for the drape path where there is a drape, and it is not free elsewhere: with
no drape the shadow costs ~2.8 ms of a ~12 ms GPU frame at a city camera.

## 20. Raising an extrusion clear of the hill (2026-08-20)

Crosscall (Adreno 610), Grenoble city camera `--es lat 45.190814 --es lon 5.724807 --es zoom 17.7
--es tilt 53 --es rotation 60.23 --es bld3d true --es anim rotate`, `-PprofileRender`, the
`layers3D` GPU section. `anim rotate` is what makes this measurable at all: the section swings
between 1.7 and 4.4 ms with the building count on screen, so a static camera says nothing. Two runs
per variant, first four samples dropped (tile decode), n≈25 each.

| roof anchor | median | mean |
|---|---|---|
| centroid only, 1 elevation sample | 1.90 / 2.00 ms | 1.97 / 1.93 |
| + footprint reach, 5 samples | 2.30 / 2.30 ms | 2.36 / 2.43 |

**+0.35 ms**, ~15% of the extrusion pass and ~3% of an 8-12 ms GPU frame, repeatable across both
run pairs. That buys buildings that are neither buried in a hillside nor bent down it; see
[the terrain page](rendering/04-terrain.md#raising-the-prism-clear-of-the-hill) for the model and
for the two cheaper answers that do not work.

The samples are `applyTerrain`, which is 4 `demMeters` under the lattice clamp - so this is 16
extra DEM taps per above-ground vertex, not 4. The obvious optimisation is a lighter variant that
skips the lattice clamp (the anchor only picks the highest ground, it never has to line up with the
surface mesh); not done, and worth roughly three quarters of the 0.35 ms if it is.

## 21. Occluding labels with the 3D content (2026-08-20)

Crosscall (Adreno 610), Grenoble city camera `--es zoom 17.7 --es tilt 60 --es bld3d true
--es drape true --es anim rotate`, `-PprofileRender`, the new `labelOcc` GPU section. Three runs,
first four samples dropped, n=11 each.

| | `labelOcc` | GPU total |
|---|---|---|
| `debug.massif.labelocclusion 0` | 0.00 ms | 10.20 |
| `debug.massif.labelocclusion 1` | 0.90 / 0.80 ms | 10.80 / 10.70 |

**~0.85 ms**, and the frame total moves with it (+0.5-0.6). That is one half-resolution pass over
the visible extrusions with colour writes packing their depth; the per-label taps in the vertex
stage do not show against it. Zero when nothing asks: the pass is skipped, which is also why it
must stay behind a property rather than being always on.

Cheaper than the 1.5-2.5 ms estimated from the ground-AO mask (~1.4 ms just to bind and clear at
any resolution). The difference is that this target is bound once per frame rather than once per
drape tile, and the extrusions are a small part of the geometry.

The model and the two dead ends - a per-fragment depth test on the label pass, and a
`GL_DEPTH_COMPONENT24` texture sampled from the vertex stage - are in
[the labels page](rendering/06-labels.mdx#per-label-occlusion-by-3d-content).

## 22. Per-tile LOD height (2026-08-20)

Crosscall (Adreno 610), `-PprofileRender`, Grenoble looking north into the Chartreuse
`--es lat 45.218503 --es lon 5.734582 --es zoom 15.37 --es tilt 29 --es rotation -7.46
--es contour true --es hs true --es anim pan --es animLatDelta 0.03`, three interleaved pairs of
prebuilt APKs, `bench/absum.py` medians over 63-64 windows.

`TileLayer::calculateVisibleTilesRecursive` projected every tile at the elevation under the screen
centre (tangram's rule). Here the focus sits at **1389 m** and the near valley at ~250 m, so the
near field was projected a kilometre too high.

| tile height for the area test | fps | frame ms | tiles/frame |
|---|---|---|---|
| screen centre (before) | 25.7 | 29.7 | 42.0 |
| elevation band **top** | 21.8 | 33.8 | 57.0 |
| elevation band **midpoint** (shipped) | 25.8 | 29.5 | 38.8 |

The band top is the maplibre-shaped choice (the AABB point nearest the camera) and costs **15% of
the frame** for +37% tiles: projecting a whole quad at its highest corner refines the far field for
one peak. The midpoint redistributes instead — slopes refine, the near valley coarsens — and is
free. One `ElevationManager::getMinMaxDisplayHeightCached` per visited quadtree node (one mutex +
LRU read, `CACHED_ONLY`, never loads) does not show at this scale.

Gain on the visible set, static at 45.1852/5.7220 z15.71 t29: `z10=3 z11=1 z12=4 z14=3 z15=7` →
`z10=3 z12=4 z13=2 z14=4 z15=6`.

**It does not fix the grazing far field**, which is what prompted the work. At that camera the
accepted areas are 13k-83k px² against a 212k threshold — the elevation term moves the areas 2-4x
and still trips no level. The cause is the flat footprint quad, not the rule: maplibre's
`calculateTileZoom` reduces to the same `−log2(d) + ½·log2(cos θ)` as the area test at its default
fov. Method and the fix that would work (per-corner DEM heights) are in
[02-tiles.md](rendering/02-tiles.md#the-lod-height-is-per-tile-not-per-frame).

## 23. Bounding the LOD's foreshortening term (2026-08-20)

Same rig and camera as entry 22, plus a static probe logging `(zoom, area, cos θ, distance)` per
accepted tile. `Options::TileLODFactor` 0.5 (the demo's value), threshold 212 337 px².

The probe's first result is the useful one: at tilt 29 **every** accepted tile is at 79°–89°
incidence, losing 1.2–3.0 levels to foreshortening — not just the horizon band. And beyond ~15 km
the tiles are distance-limited: they would need `cos θ > 1` to refine, so no bound on the grazing
term can reach them.

`Options::TileLODForeshorteningLimit` (default 0 = off = tangram's rule), static camera at
45.1852/5.7220 z15.71 t29:

| limit | visible set | n |
|---|---|---|
| 0 | `z10=3 z12=4 z13=2 z14=4 z15=6` | 19 |
| 1.25 | `z10=2 z12=4 z13=2 z14=2 z15=9` | 19 |
| 1.0 | `z11=2 z12=3 z13=6 z14=2 z15=9` | 22 |
| 0.75 | saturated, same as 1.0 | 22 |

Cost at limit 1.0, panning north at 45.2185/5.7346 z15.37 t29, 3 interleaved pairs, 63 windows:

| | fps | frame ms | tiles/frame |
|---|---|---|---|
| off | 26.7 | 29.1 | 40.0 |
| limit 1.0 | 24.6 | 30.1 | 50.4 |

**+26% tiles for −8% fps.** Off by default, so it costs nothing until an app opts in.

**Method note — do not size an LOD change from a spreadsheet.** Applying the clamp to each accepted
tile's measured area predicted 19 → 40 tiles, **7× the real cost**. The recursion boosts a parent
and its children by the same factor, so a tile that splits produces children that stop one level
down rather than cascading; the per-tile model has no way to see that. Build it and count.

## alpimaps, eink style, zoom in/out (2026-08-24)

Crosscall device (`1cba1468`), `DEBUGMASSIF=1 ns run android … --gradleArgs=-PprofileRender`,
zooming in and out around z10–z13 over an offline mbtiles set.

**18.0 fps, frame avg 47.8 ms, max 108.5 ms.**

| section | CPU ms | GPU ms |
|---|---|---|
| sky | 18.0 | 0.9 |
| layers | 16.0 | **16.9** |
| layers3D | 6.3 | 0.7 |
| other | 7.4 | — |
| total | 47.8 | 19.9 |

**`sky` is not sky.** `FrameProfiler::skyMs` covers "frame start: state, sky, background" and
**includes the swap-buffer wait**, which is why its GPU time is 0.9 ms. Read it as frame pacing,
not as work — it was misread as an 18 ms anomaly once already. Real CPU work is ~30 ms.

Per frame: 212 draws, 505 k indices, `draw` **11.2 µs per glDrawElements** (probe overhead 0.62 µs
carried in every section — subtract it before believing any of them).

**The emulator says something completely different and must not be used for this.** Same app and
style on `emulator-5554` (ES→Metal translator): 1431 draws and 3.6 M indices per frame, `draw`
0.6 µs, `styleEval` 0.1 µs. Seven times the geometry and a twentieth of the per-draw cost — it
inverts which end is expensive.

What the device numbers actually point at, in order:

1. `layers` is GPU-bound (16.9 GPU vs 16.0 CPU). Fewer/simpler pattern-filled polygons at low zoom
   is the lever; see the eink pattern thresholds below.
2. `draw` at 11.2 µs/call is Adreno submit cost — draw *count* matters independently of geometry.
3. `tileSets` 36 and `labelMaps` 36 over 18 frames — **two full `buildLabelMaps` rebuilds per
   frame** while zooming, 1522 labels allocated per second.
4. `cullMs` 147 ms/s (8.2 ms/frame, 3 passes/frame), and `reNull` 4039 of `placeUpd` 5396: **75 %
   of placement re-anchors have no placement at all**.
5. `viewStateChanges` 210 over 18 frames — the per-frame style memo is dropped ~12×/frame, giving a
   **36 % style-function miss rate** (6 % on the emulator) and `styleEval` 5.6 µs/draw.

### The style, not the renderer, was most of the lag

`eink.json` turns pattern fills on several zoom levels below `base.json`: `rock` 13→10,
`forest` 14→11, `scrub` 14→12, `scree` 13→12, and adds `water_pattern_zoom: 0` (every zoom) and
`iceshelf_pattern_zoom: 4`. 75 `PolygonPatternSymbolizer`s against osm's 24. Raising the four
`*_pattern_zoom` **style parameters** back to the base defaults removed most of the lag —
confirmed on device. The remaining two are `constants`, so they need a style rebuild.

### Reading a spike

The once-a-second average hides the frame the user feels. `PROF SPIKE:` lines
(`FrameProfiler::checkSpike`, over 80 ms) print that frame's own section split next to the
counters that moved **during that frame** — `tileSets`, `labelMaps`, `labelsAlloc`, `snaps`,
`cullPasses`, `geomDraws`, `surfBuilt`. A spike with `tileSets 0 labelMaps 0` is not a tile-set
change, however plausible that sounded.

## 24. Bridges over a city: what the reference tiles, the labels and a fast zoom cost (2026-09-03/04)

All on emulator-5554 (arm64 image), the day-cycle-light example at Paris (48.86, 2.3376, z17.2,
tilt 20) and the Grenoble bridge camera (45.192955, 5.719502, z19.24, tilt 40, rotation -106.38),
profile APK (`-PprofileRender`). The emulator has no GPU timers, so every number here is CPU or
frame time; the device run is still owed for the settle window.

| what | before | after | mechanism |
|---|---|---|---|
| example, still camera | 2 fps, `spanUnionsMs` 1000-1300 per second, 121 reference tiles | 25-48 fps, 3.5 ms, 32 tiles | sticky reference set unbounded and named coarsest first; union meet test quadratic |
| example, pan, label anchoring | 1.5-2.8 s per 12 s run, 19-31 `prepare` spikes of 130-190 ms | 65-150 ms, 0-1 spikes | `spanHeightAt` per label vertex against every span union; now a deduplicated chord list with bounds, and a new tile set's labels sampled on the cull thread off the lock |
| still camera after a pan, reference tiles | 80 culls/s for ever, 145 tile loads per 8 s, deck flipping between two chords | 0 culls, 0 loads | reference tiles thrashed the preloading LRU; own map now |
| chained instant zooms 15-18, worst `drape` in a frame | 185-212 ms | 43-71 ms | the deck's own drape bake ran outside the frame's bake budget |

**What did not measure.** The 300 ms bake settle window (`DRAPE_BAKE_SETTLE_MS`, switch
`debug.massif.drapesettle`): bake counts swung 112-503 between identical chained-zoom runs, so no
A/B verdict on the emulator. Moving label anchoring to the cull thread UNDER the renderer's lock
(first attempt) made frames worse, not better - the lock wait moved the spike from `prepare` to
`other`/`sky`; only the lock-free sampling with a locked apply paid off, and only once the chord
list made each sample cheap. The label-side win was the chord dedup, not the thread.

**Method that found all of it.** A per-second `RenderStats` line plus counting log lines in a quiet
window (`logcat -c; sleep 10; grep -c 'Loading MapTile'; grep -c 'cullUpd='`) told a live loop from
a busy frame; a per-thread `/proc/<pid>/task/*/stat` snapshot told a hang from a load spike (every
thread sleeping, frames still coming); `PROF SPIKE` sections named the frame's cost. Screenshots
were counted by exact colour before any was looked at.
