---
title: Performance
description: How to measure the renderer, what currently costs what, and where the next win is.
sidebar_position: 10
---

# Measuring and improving performance

Scope: how to get a number you can trust, what the frame currently costs, and what is known **not**
to be worth optimising. The dated history and the raw numbers live in
[../performance-log.md](../performance-log.md); this page is the method and the current state.

## Build

```sh
cd scripts/android-dev && ./gradlew :app:assembleDebug -x lint -PprofileRender
adb install -r -t app/build/outputs/apk/debug/app-debug.apk
```

- The native SDK is compiled **optimised by default** (`RelWithDebInfo`), from the command line and
  from Android Studio alike. `-PnativeOpt=false` goes back to `-O0` for stepping through native code.
  This matters more than any single optimisation so far: the same code at `-O0` measured **8.6 fps**
  against **12.8** at `-O2` on the north pan.
- `-PprofileRender` compiles in `FrameProfiler` (`PROF` lines) and `vt/RenderStats.h` (`RenderStats`
  lines). Neither exists in the binary otherwise.
- Install from `app/build/outputs/apk/debug/`. `app/build/intermediates/apk/debug/` also holds an
  `app-debug.apk` and it is **stale** — *except* when you pass
  `-Pandroid.injected.build.abi=<abi>` to build a single ABI, which inverts it: AGP then writes the
  fresh APK to `intermediates/` and leaves `outputs/` untouched. Check the mtimes, not the path.
- **`RelWithDebInfo` is not what ships.** It is a plain `-O2 -g` build: no LTO, and none of the
  per-subproject `-O2`/`-Oz` split, which is gated on `CMAKE_BUILD_TYPE MATCHES Release`. The
  shipped Release build compiles the SDK at `-Oz -flto=thin`. Bench the shipped configuration with
  `-PnativeConfig=Release` (Release strips, so simpleperf loses its symbols — use it for `PROF`
  numbers, not for profiles).

## The three instruments

| instrument | what it gives | gotchas |
|---|---|---|
| `PROF` | CPU ms per frame section: `sky prelude prepare cover drape layers layers3D billboards` | `sky` is mostly the swap wait, not work. Not comparable across apps. |
| `PROF GPU` | the same sections on the GPU (`GL_EXT_disjoint_timer_query`) | Android only; off with `setprop debug.massif.gputimer 0` |
| `RenderStats` | draws, indices, render tiles, style layers, surfaces, label and prep timings, tile-surface builds | per one-second interval, deltas — **divide by the `PROF` frame count** of that interval, a faster build prints bigger counters |
| `simpleperf` | an actual CPU profile of the render thread | see below — this is what finds things the timers cannot |

### Profiling the render thread

```sh
adb shell simpleperf record --app com.massifmaps.MassifDemo -g -f 500 --duration 12 -o /data/local/tmp/perf.data
adb pull /data/local/tmp/perf.data /tmp/perf.data
# symbols: the UNSTRIPPED .so, in a tree mirroring the device path
D='/tmp/symfs/data/app/~~<hash>==/com.massifmaps.MassifDemo-<hash>==/lib/arm64'; mkdir -p "$D"
cp scripts/android-dev/massif/build/intermediates/cxx/*/*/obj/arm64-v8a/libmassif.so "$D/"
$NDK/simpleperf/bin/darwin/x86_64/simpleperf report -i /tmp/perf.data --symfs /tmp/symfs \
  --tids <gl-thread-tid> --children --sort symbol -n
```

Two traps: the report is per **process** by default and the tile decode threads are as busy as the
render thread, so always pass `--tids`; and several threads are called `GLThread` — the render thread
is the one whose call graph starts at `MapRenderer::onDrawFrame`.

## Getting a trustworthy number

- **Device numbers drift.** The same build has measured 8.4, 11.2 and 16.1 fps in one evening.
  Only **interleaved** A/B means anything: alternate two APKs, ≥3 repeats, report the median and the
  spread over one-second windows. A comparison against a number taken earlier is worthless.
- **Emulator fps is meaningless.** Emulator runs are for *counters* (draws, indices, render tiles) and
  for functional checks.
- **`RenderStats` counters are sums over the one-second interval, not per-frame values.** A build
  that renders the same scene FASTER therefore prints MORE draws and MORE indices, because it got
  through more frames. Always divide by the frame count of the same interval — the `PROF` line
  right next to it starts with `%d frames in %.0f ms`. Comparing two arms on the raw
  `geomIndices=` cost a wrong conclusion in August 2026 (the faster arm looked like it was
  submitting more geometry).
- **A static camera never settles here.** With a rich style, a parked camera swings 9–24 fps for
  minutes (tile arrival, drape bakes, label placement, elevation fetches), so "leave it still and
  read the number" is not a measurement. Drive a scripted move — `--es anim pan` — for anything you
  intend to believe; it also makes the two arms traverse the same tiles.
- **The camera decides what you measure.** The slow case is panning **north into the mountains**
  (`--es animLatDelta 0.06`) with contours and hillshade. Panning east over the valley is cheap.
  Tilt matters as much: at tilt 85 most of the screen is sky. Tilt 90 is straight down, so a
  "tilted" camera for occlusion work is t=20, not t=55.
- **`bench/ab2.sh` passes `--es base plain`, and the `#hillshade` slot only exists in the COMPOSITE
  base** — a hillshade run started from it silently measures a frame with no hillshade in it. Add
  `--es base composite`.
- Helper scripts: `scripts/android-dev/bench/` (`ab.sh`, `ab2.sh`, `north.sh`, `abapk.sh`,
  `abprop.sh`, `absum.py`).

## Reset the debug props before measuring

`debug.massif.*` properties survive until the device reboots, and a session that leaves them set
measures a crippled build for weeks. A run in August 2026 found `drapebudget 0` and `drapemip 0`
(the drape memory budget and its mipmaps, i.e. the whole win of the round that added them) still
set from the session that introduced them, along with `paintdetail 0` and `skyclip 0`. Clear every
one of them before a baseline:

```sh
for p in areasourcedensity areathreshold asyncdepth asyncdepthms background demtaps depthshift \
         drapebudget drapemip groundpaint linesourcedensity paintdetail skyclip terrainpaint \
         tilebg tilemasks; do adb shell setprop debug.massif.$p '""'; done
```

## Where the frame goes today

At `-O2`, on the north pan with content, the render thread has **no dominant leaf**: draw submission
(`renderTileGeometry` ~28% inclusive, ~156 draws/frame), `renderGeometry2D` ~33%,
`TileRenderer::prepareFrameUnsafe` ~10%, `GLTileRenderer::startFrame` ~9%,
`ElevationTextureCache::getTexture` + `resolveEntry` ~9%, hillshade layer ~6%.

The GPU is not the limit: `PROF GPU` with content puts the layers at 29–43 ms and the total at
38–53 ms against a CPU frame of 120–175 ms at `-O0`. **We are CPU-bound, on draw submission.**

So the lever is **fewer draws and fewer layers**, which is [07-hillshade-contours.md](07-hillshade-contours.md)
and [09-composite-layer.md](09-composite-layer.md), not micro-optimisation.

### A city pans slower than a mountain, and it is fragments

Same build, same camera settings (z16.22, tilt 26), panning north — Grenoble against the
Saint-Eynard ridge, on a Crosscall:

| | fps | CPU frame | GPU total | GPU layers | draws/frame | indices/frame |
|---|---|---|---|---|---|---|
| city | 12.0–13.7 | 34 ms | 33 ms | **21 ms** | 48 | 1.65M |
| mountain | 18.2–19.6 | 31 ms | 18 ms | **9 ms** | 35–48 | 1.3–1.6M |

The CPU frame is the same and the draw/index counts match in the closest pair, so the 2.4× is
**per-fragment**: dense city content covers the whole screen where the mountain view is mostly
terrain surface. Of it, **contours are 45%** — `--es contour false` takes the city from 12.1 to
17.6 fps (repeated, interleaved), GPU layers 21.3 → 13.6 ms, render tiles 805 → 380 and draws
602 → 430 per interval, because the `#contour` slot is a second tile set drawn over the first.

That was measured before the line tesselation was fixed, and the fix moved the city more than
anything on this page: cutting a draped line by its **sag** instead of by the tile's cell count
([04-terrain.md](04-terrain.md#cutting-a-line-by-its-sag-instead-of-by-the-tiles-cell-count)) took
the city pan from 7.5 to 13.8 fps and the mountain pan from ~11 to ~17.8, with the draw count
unchanged and 3.4× fewer geometry indices per frame. It is the shipped path now
(`debug.massif.linesag 0` restores the old split), so any city number taken before 2026-08-13 is
measuring a frame that no longer exists — retake rather than compare.

### What the city frame is bound by, and the five things that were not it

After the sag fix the city pan sits at 13.4–15.2 fps: a 66–71 ms frame against a 41–48 ms CPU frame
and a 33 ms GPU frame. `sky` (21.5 ms of the CPU frame) is the swap wait, so real CPU work is
~20 ms — the frame is **GPU-bound**, and shrinking the surface proves it is per-fragment:
at 0.76× the pixels (`adb shell wm size 720x1440`, reset with `wm size reset`) GPU `layers` scales
0.79×, i.e. linear in pixel count.

Everything knocked out one at a time, on the same pan, changed **nothing** (GPU `layers`, ms):
baseline 20.8–24.1 · contours off 21.6–23.6 · 3D buildings off 22.0–23.8 · hillshade off 21.3–22.9 ·
fog + shadows off 22.5–23.8 · the per-fragment tile-clip `discard` compiled out 20.6–23.0 · blending
off with content depth writes on 22.0–24.3. Two of those deserve a note, because each killed a
plausible theory: the shader comment claiming `GL_STENCIL_BITS = 0` is stale (this device reports
**stencil bits 8**), and blending is free on this tile-based GPU, so "opaque without blending" buys
nothing while the draw order gives the depth test nothing to reject.

The answer was the base map layer as a whole — with it off, `layers` is 0.0 ms and the GPU frame is
9.4 ms at 43 fps — and inside it, the **undraped lines**. Draping them takes the city to 26.8 fps
with `layers` at 0.3 ms; the numbers and the resolution trade are in
[04-terrain.md](04-terrain.md#draping-the-lines-and-keeping-contours-out-of-it).

Two method points from that hunt. Toggling one slot of a **composite** layer (`--es contour false`)
moves a sliver of one layer, not a layer — it is not a way to price a subsystem. And the old
"contours are 45% of the city frame" figure was taken at z16.22 panning north into the ridge, where
contour lines exist; on the valley floor at z15 they cost nothing measurable. A camera is part of a
measurement, not a detail of it.

### The undraped line cost is TRIANGLES, not pixels and not shading

The natural reading of the resolution test above is "fragment-bound". It is wrong, and one more
experiment says so: **halving every line width changes nothing** (`layers` 20.3–21.7 ms). Neither
does anything else that makes a fragment cheaper — see the table above, plus round-join fans cut
from 5 triangles to 1 (13.6–15.5 fps, same indices/frame) and the DEM vertex taps cut from 4 to a
single hardware-filtered fetch (`debug.massif.demtaps 1`, `layers` 22.3–23.3 ms).

What moves it, every time, is the triangle count. Shrinking the framebuffer also shrinks **binning**
work, which is per-primitive on a tiler, so that test could not separate fill from binning. Long
thin road quads crossing many screen bins are the worst shape for this GPU, which is why the sag
split (3.4× fewer indices) and draping (no per-frame triangles at all) are the only two things that
have ever moved this camera.

The relationship is sub-linear, which bounds what geometry work can buy. Douglas-Peucker over the
source vertices before tesselation (the `simplify` mapnik property is parsed and never applied —
`TileReader.cpp:170`) measures:

| line simplification | indices / frame | fps |
|---|---|---|
| none | 0.50–0.70M | 13.4–15.2 |
| ~¼ pixel | 0.44–0.47M | 14.9–16.0 |
| ~2.5 pixels (visibly lossy) | 0.28M | 16.8–18.3 |

Halving the triangles buys ~25%; removing them (drape) buys ~90%. At a tolerance anyone would ship,
simplification is worth half a frame per second — not a lever. **Draping is.**

### The 2D city is bound by the OPPOSITE thing: draw submission

Terrain off, same city camera (`--es terrain false --es base composite --es style assets`,
`bench/city2d.sh`): **CPU frame 51.9 ms against a GPU frame of 20.4** — the inverse of the 3D city
above. The frame issues **765 draws** (606 geometry, 74 label, 62 stencil mask, 23 per-tile
background), and `simpleperf` puts **60% of the GL thread in the driver and the kernel**
(`libGLESv2_adreno.so` 36.1%, kernel 24.1%, our own code 23.9%).

The two experiments that settle it: dropping the background plane takes **22% off the GPU frame and
buys no fps**, while dropping the stencil masks removes **8% of the draws and buys 4%**. So on this
camera fragments, triangles and shading are all free, and the only currency is the draw count.

Acting on that: a style layer alternating patterned and plain polygon fills used to split into a
draw per alternation — 48% of all geometry draws. Each style slot now carries a **pattern flag**
instead, so both live in one draw ([03-vt-renderer.md](03-vt-renderer.md#what-splits-a-tiles-style-layer-into-several-draws)):
**584 → 363 draws a frame, 17.9 → 20.3 fps**, `layers` CPU 21.9 → 15.1 ms. The floor is one draw per
(tile, style layer) — 337 — so what is left needs cross-tile batching, which nothing here does.

**An opaque/translucent depth split (maplibre's model) was then built and reverted.** 2D disables the
depth test entirely, so there is no early-Z at all — but a fragment optimisation cannot move a frame
whose GPU is idle by 4.4 ms: it measured **20.1 → 19.6 fps**, the GPU saving 0.3 ms and the extra
pass costing 1.4 ms of CPU. It also unlocks dropping the stencil tile masks (60 draws a frame,
`layers` CPU −22%), and even that surfaces as +2% — inside this bench's drift.
[performance-log.md 16.8](../performance-log.md).

2D at the *mountain* camera measures 41 fps and is pinned against the device's 43 Hz present ceiling
([performance-log.md 15.6](../performance-log.md)), which is why this was never visible before: the
conclusion "cutting 2D work cannot show up as frame rate" is true of that camera only.

**`-PprofileRender` itself costs 13%** (SurfaceFlinger `totalFrames`, three interleaved pairs:
415/415/424 plain against 363/366/371 instrumented) — `clock_gettime` is 19.4% inclusive of the GL
thread. Discount any absolute CPU ms from a profiling build before comparing it with anything else.
Numbers and the full A/B in [performance-log.md 16](../performance-log.md).

## Against tangram-ng, on the same device and camera

Run back to back on the Crosscall at Grenoble 5.724/45.188 z15 tilt 45, their demo patched to that
camera with 3D terrain and contours enabled (`BENCH_MODE` in their `MainActivity`). Measured with
the **cross-app** instrument this page insists on — SurfaceFlinger `averageFPS` of the
`SurfaceView[...](BLAST)` layer — under an identical scripted 20-swipe pan:

| | totalFrames | averageFPS |
|---|---|---|
| tangram-ng, 3D terrain + contours | 241 | **13.6** |
| this fork, undraped lines | 224 | **12.7** |
| this fork, `drapeLines true` | 271 | **15.3** |

**Tangram is not faster here.** It is within a frame per second of our undraped path and behind our
draped one, and it does not reach 30 fps on this device either. Its cost sits somewhere else: their
own `FrameInfo` puts `renderTerrainDepth` at 37–43 ms of a 66–70 ms frame — a terrain depth pre-pass
is most of their frame, where ours is line geometry.

Read the two instruments separately, and never mix them: their in-app `_Frame` (14–15 fps) and our
`PROF` (13.7–15.2 undraped, 26.8–27.7 draped) each measure their own render loop, while
`averageFPS` counts frames actually presented over a window that includes the gaps between scripted
swipes — both apps render on demand, so it compresses everything toward the gesture rate. The
ranking is the same under both; the magnitudes are not comparable across them.

Not a controlled A/B either way: their scene is OSM Bright + AscendMaps against our packaged style,
so content density differs, and their run carries their debug overlay (which calls `GL::finish`).
It is enough to retire "tangram is smooth, we are not" as a premise for city work; where they still
lead is a question to settle per mechanism, not per frame rate.

Two bugs found in their tree while setting this up, both still open there: `MapController.DebugFlag`
lists a `TILE_INFOS` that C++ dropped (`map.h:533`), so every Java flag from index 3 on is off by
one; and on Android `ElevationManager::offscreenWorker` is never created, so their terrain depth
pass logs an error every frame and takes a fallback path.

## Starting up in terrain mode

Measured on a Crosscall at the demo's default camera (Grenoble, z16.22, terrain + contours, warm
caches), with temporary probes in `TileLayer::FetchTaskBase::run`, `PersistentCacheTileDataSource::
loadTile`, `ElevationManager::loadTileGrid` and `VectorTileLayer::FetchTask::loadTile`. The vector
tiles were never the cost: 66 decodes, 5–6 s of thread time. Elevation was, three times over.

| per startup | before | after |
|---|---|---|
| DEM HTTP requests | 79–94, of which **15 could only fail** | **0–1** |
| DEM grid decodes | 1525 loads of 167 distinct tiles (32 s) | 157 of 157 (3.1 s) |
| 90% of the content on screen | 6.4–7.6 s | **4.3–4.4 s** |

1. **The elevation grid LRU held 85 tiles and the view needed 167** — a byte budget behaving as a
   tile budget the DEM raster size decides. Fixed in [04-terrain.md](04-terrain.md#elevation-data).
2. **The on-disk tile cache defaults to 50 MB and one terrain view's DEM pyramid does not fit.**
   Two consecutive starts at the *same* camera missed on **different** tiles (their miss sets did
   not intersect): the cache was evicting exactly what the next start needed, so ~65 tiles were
   re-downloaded every time at 400–800 ms each. This is an app-side setting —
   `PersistentCacheTileDataSource::setCapacity`; the demo now asks for 600 MB for the DEM and
   200 MB per other source (`DemoConfig.DEM_PERSISTENT_CACHE_MB`, `--es demCacheMb`).
3. **The element elevation warm-up sampled the view envelope**, whose corners are off the DEM in
   terrain mode: 15 guaranteed 404s per startup. See
   [12-vector-elements.md](12-vector-elements.md#terrain-interaction).

What is left, in order: **contour tile generation** (44 child fetches, 6–22 s of thread time — the
`#contour` slot is a whole second tile set, which is what [07-hillshade-contours.md](07-hillshade-contours.md)
would remove by computing contours in the terrain fragment shader) and the network itself.

**The decode pool changes almost nothing here.** `Options::setTileThreadPoolSize` retaken
2026-08-13 now that the network no longer dominates — interleaved pairs, `--es tilePool 1|2`, warm
caches, city camera:

| | 3D | 2D (`--es terrain false`) |
|---|---|---|
| tiles decoded | 25 either way | 22 either way |
| first→last decode, pool 1 | 2.98 / 3.20 s | 2.68 / 2.59 s |
| first→last decode, pool 2 | 3.26 / 2.93 s | **2.22 / 2.17 s** |
| pan fps, pool 1 | 26.7 median | 20.1–22.9 |
| pan fps, pool 2 | 26.8 median | 19.8–22.8 |

The pan is identical in both modes. Decode finishes at the same moment in 3D — 25 tiles land within
~3 s of the first, paced by fetch and cache I/O rather than CPU, so a second thread has nothing to
win — while in 2D, where the frame leaves more CPU over, it finishes ~18% sooner (2 of 2 pairs).
Either setting is defensible; **1 remains the default** because the gain is confined to the load
phase in 2D.

Two metrics to avoid here, both of which produced a wrong answer first time round: fps measured
*while tiles stream* (it conflates "did less work" with "ran faster" — the arm that gets more
content on screen scores worse), and fps on a **static camera** after loading, which never settles
([Getting a trustworthy number](#getting-a-trustworthy-number)). An earlier version of this section
claimed pool 2 costs ~15% of the frame rate on that basis; time-to-content and a scripted pan do
not support it.

Unrelated but on the same path: the first `loadTile` on a persistent cache runs `loadTileInfo`, a
full-table scan of the cache DB, on the tile thread — **1.4 s** on the first (cold page cache) run
of a 52 MB DB.

## Style load and tile decode (off the render thread, but in front of the user)

Measured on a Crosscall HLTE556N with the demo's bundled style project (`--es style assets`:
`osm.json`, 23 layers, 67 styles, 461 styleparameters, 9 `.less`/`.mss` files, 74 KB), with temporary
timers in `CartoCSSMapLoader`, `TileReader` and `MBVectorTileDecoder`. Device clocks move the
absolute numbers by up to 40% between runs — compare a change against a run whose *style load* time
matches, or pair the runs.

**Loading a style: ~0.5–0.7 s**, split roughly 7 / 75 / 20 between parse, compile and everything else:

| section | ms |
|---|---|
| `CartoCSSParser::parse`, all 9 files (boost::spirit) | 34 |
| `CartoCSSMapLoader::buildMap` | 362 |
| — `CartoCSSCompiler::compileLayer`, all layers | 271 |
| — translate to mapnik rules | 71 |
| — `Style::optimizeRules` | 9 |
| fonts, asset scan, symbolizer context | ~110 |

The compile is three layers: `transportation` **115 ms** (2230 rules, 23 attachments), `route` 61 ms,
`poi` 61 ms; the other 20 together ≈ 35 ms.

Inside `compileLayer` it was three near-equal thirds, and each answered to a constant factor rather
than to the algorithm (per-section ms, `transportation`, cold runs on the Crosscall):

| section | before | after |
|---|---|---|
| `buildPropertyLists` | 37.8 | 22.8 |
| per-zoom filter evaluation | 33.4 | 12.1 |
| `buildLayerAttachment` | 39.6 | 31.4 |
| list comparison | 2.2 | 2.0 |

What the three fixes were, in the order they pay: **evaluate each distinct predicate once per zoom**
(a layer has ~77 predicates and ~640 properties, and every property re-evaluated its own filters —
this is a memo, not a semantic change); **bucket `insertProperty` by field** (two properties can only
be equal if they set the same field, and comparing them is a deep expression comparison, so the scan
went over every property inserted so far); **intern the field strings and hand out references** in
`buildLayerAttachment` (its innermost operation was a string compare, and each property visit copied
a `shared_ptr`). Summed over all 23 layers: **264 → 157 ms**, `buildMap` 362 → 261 ms.

A second round took the same three sections further, for **157 → 129 ms** (`buildMap` 220 ms):

- **A zoom whose predicates evaluate exactly as the previous one is skipped whole.** The optimized
  property lists are a pure function of (property lists, predicate results), so equal results mean
  equal lists and equal attachments. Comparing ~80 bytes replaces rebuilding every property list and
  deep-comparing it. A layer resolves to 1–14 distinct ranges out of the 25 zooms evaluated.
  `boost::tribool` cannot be compared as a block — two indeterminate values do not test equal — so
  the results are kept as a three-state byte.
- `buildLayerAttachment` built a fresh property set (two vectors) per (property, property set) pair
  considered and dropped it on the common path; one reused object keeps the buffers.

Cumulative: **compile 264 → 129 ms**, `buildMap` 362 → 220 ms on the same style and device.

A third round attacked what was then the biggest item, `buildLayerAttachment`, for a paired
**175.4 → 117.4 ms** on the same device and style project (three cold runs each, spread under
1.5 ms; the absolute numbers are higher than the round above because the bundled style has grown
since — only the pair means anything). Its inner loop runs **115,756** times for `transportation`
alone, and the counters said where:

| what the iteration did | share | what replaces it |
|---|---|---|
| "does this set already set this field?" | 71% | one bit test on a per-set field bitmask |
| filter intersect rejects the merge | 24% | one AND against a per-predicate "disjoint" mask |
| reaches the redundancy (cover) test | 5% | one AND against a per-predicate "contains" mask |

The field test is the interesting one: it used to scan the set's whole property list and compare
specificities. `compileLayer` sorts the properties by **decreasing specificity**, so a set that
already has the field got it from a property that outranks the current one — the answer is always
"skip", and one bit answers it. That order is now load-bearing; the comment at the sort says so.
Two smaller items: a merge that cannot succeed is rejected *before* the trial set is copied, and
`buildPropertyLists` evaluated each property expression once per attachment it appears in rather
than once per property.

Both masks are 256 bits with a fallback to the scans they replace (the biggest layer here has ~50
fields and ~80 predicates).

**How this was measured, and how to redo it:** `libs-massif/cartocss/test/CompileBench.cpp` builds
on the host (the command is in its header), loads a style project the way `CartoCSSMapLoader` does
and times `compileLayer` per layer. With `CSSBENCH_DUMP=<file>` it dumps every compiled property
set, so `diff` proves a compiler change kept the output identical — all five bundled projects
(osm/streets/outdoors/eink/ign) dump identically across this round. The host is ~9× faster than the
device but splits the same way per layer, so it is a valid *guide*; the ms in this page are always
device numbers.

Left on the table, per the host section timers after this round (all 23 layers, one run):
`buildLayerAttachment` **4.8 ms**, the stylesheet walk in `buildPropertyLists` **2.8 ms**, the
expression evaluation **0.7 ms** (2.6 before the memo), the per-zoom filter evaluation **0.9 ms**.
The walk is the structural one: it goes over the **whole** stylesheet once per layer (23 × 407
elements here) and only then discards what the layer predicate rules out. Sharing one
`FilteredPropertyState` across a project's layers would fix it, and that is an API change to the
compiler rather than a local one.

### Flattening the cascade is exponential in INDEPENDENT filter fields

Every round above tuned the constant factor. The shape underneath is worse than linear, and it is
worth knowing exactly what triggers it before designing anything on top.

CartoCSS cascades: declarations from different selectors land on one feature and are resolved by
specificity. `buildLayerAttachment` flattens that by enumerating every combination of filter
conditions that yields a distinct declaration set — so a layer whose declarations are driven by
**independent** fields produces their **cross product**. Measured on the host with
`libs-massif/cartocss/test/CompileBench.cpp`, one layer, one property per field dimension, N values
per field:

| dimensions | N | declarations | compiled rules | compile |
|---|---|---|---|---|
| 2 | 8 | 17 | 81 | 0.1 ms |
| 3 | 8 | 25 | 729 | 0.9 ms |
| 4 | 8 | 33 | 6,561 | 29 ms |
| 5 | 8 | 41 | 59,049 | 3.1 s |
| 6 | 6 | 37 | 117,649 | 13.3 s |
| 6 | 8 | 49 | — | **> 180 s** |

Rules are exactly `(N+1)^dimensions`, and compile time tracks `declarations × rules`. **49
declarations do not compile.** So the OpenMapTiles style that had not finished after 25 minutes is
not a layer-count problem, and no constant-factor round reaches it.

What decides it is whether the fields are independent, not how many predicates there are. The same
declaration count, arranged the two ways:

| 65 declarations, 4 properties | compiled rules | compile |
|---|---|---|
| all driven by **one** field (`[class=…]`, values mutually exclusive) | 17 | 0.1 ms |
| each driven by **its own** field | 83,521 | 11.9 s |

4,900× the rules from the same style. `PredicateIntersectsChecker` is what saves the first case: two
predicates on the same field with different values cannot both hold, so the combination is pruned.

**Real styles sit on the safe side, but not by much.** The bundled 23-layer `osm` project compiles
835 source declarations to 3,731 rules over 296 (layer, attachment, zoom-range) groups. Its worst
group, `transportation::casing` at z17, tests **14 distinct fields** and still yields only 100 rules
— because those fields are dominated by one mutually-exclusive `class`. 103 of the 296 groups reach
4 or more dimensions. Median rules per group is 4, mean 12.6, max 100; that count is also what
`findFeatureSymbolizers` walks per distinct feature-data at decode, so the blowup would cost twice.

A MapBox style is exactly the adversarial shape: each layer's paint properties are driven by
independent `["get", …]` expressions. That is why `mapbox2css`'s `split.ts` caps its branch
expansion at 8 and adds 24 attachments on topo-v4 — it is holding the dimensions apart by hand.

**Reproduce:** build `CompileBench` per the command in its header, generate a layer with `d` fields ×
`N` values, and read the `CSSBENCH_DUMP` line count as the rule count.

#### And resolving it per feature instead would be CHEAPER at decode

The alternative to flattening is to keep the selectors and resolve the cascade per feature at
decode, memoised on the feature-data key `TileReader` already uses. Measured on the Crosscall with
a temporary probe in `TileReader::processLayer` that, per distinct feature-data group, times the
real rule walk and then a **shadow** pass evaluating every distinct filter of the style's
zoom-prefiltered rule set once. `--es style assets` (23-layer bundled project), default city camera:

| run | tiles | decode | groups/tile | rules/group | today | shadow | ratio |
|---|---|---|---|---|---|---|---|
| warm cache | 64 | 169.5 ms | 1360 | 13.9 | 15.08 ms (8.9%) | 11.95 ms (7.1%) | **0.79×** |
| warm cache | 128 | 150.1 ms | 1366 | 13.9 | 14.21 ms (9.5%) | 11.44 ms (7.6%) | **0.80×** |
| cold + zoomseq | 64 | 144.9 ms | 1481 | 13.9 | 14.86 ms (10.2%) | 11.96 ms (8.3%) | **0.81×** |
| cold + zoomseq | 128 | 107.7 ms | 774 | 13.5 | 7.82 ms (7.3%) | 6.14 ms (5.7%) | **0.79×** |

Evaluating **every** distinct filter once per group is *cheaper* than the walk that exists — even
though the walk evaluates **fewer** predicates, because `FilterMode::FIRST` stops evaluating after
the first match. So `findFeatureSymbolizers` is not paying for predicate evaluation; it is paying
for the walk's bookkeeping — the filter-mode state machine, the `symbolizers.insert()` appends and
a `shared_ptr` copy per matching rule.

The absolutes carry probe overhead (two clock reads per group, ~1,400 groups a tile) and the decode
column includes the shadow pass; the **ratio** is what the runs agree on, and both halves are timed
the same way.

So the per-feature model's predicate term costs **≤ 7.1% of decode**, replacing a term that costs
8.9% — and it is an upper bound, because flattening turns the source predicates into conjunctions,
so the source set is smaller than the 13.9 filters measured here.

#### The second term, prototyped: the declaration walk costs 1.4%

The model's other half is the declaration scan — walk the layer's declarations in decreasing
specificity, take the first writer of each field, hash the winners to intern a symbolizer set.
Prototyped in the probe as the real scan (one masked test per declaration that all its filters hold,
a field-shadowing test, an FNV hash of the winners), over an array sized from the style's own
numbers: `CartoCSSCompiler::measureDeclarations` on the bundled `osm` project gives declarations,
distinct fields and filter refs per style, so scan length, mask density and field cardinality are the
style's. Only which bits are set is synthetic, and the scan cost does not depend on that.

How long that scan is, from the compiler:

| | groups | declarations | per group |
|---|---|---|---|
| all zooms | 67 | 4,954 | median 18, mean 74, max 945 |
| pruned at z16 | 57 | 2,954 | mean 137 |

**Zoom pruning is what makes it cheap.** Zoom is fixed for a tile, so a declaration whose zoom
filter is false is dead for the whole tile and is dropped once per (style, tile) — exactly what
`preFilterStyleRules` already does for rules. That removes 32–42% of declarations (39% at z16), and
because it hits the biggest styles hardest the mean scan falls 465 → 137.

Crosscall, `--es style assets`, default city camera, same probe as above:

| declarations | tiles | decode | decls/group | today | preds | walk | per-feature | ratio |
|---|---|---|---|---|---|---|---|---|
| all zooms | 64 | 171.3 ms | 465 | 15.88 (9.3%) | 12.84 (7.5%) | 4.55 (2.7%) | 17.38 (10.1%) | 1.10× |
| all zooms | 128 | 146.9 ms | 466 | 14.76 (10.0%) | 11.89 (8.1%) | 4.24 (2.9%) | 16.13 (11.0%) | 1.09× |
| **z16-pruned** | 64 | 181.0 ms | 137 | 18.46 (10.2%) | 14.82 (8.2%) | **2.52 (1.4%)** | 17.34 (9.6%) | **0.94×** |
| **z16-pruned** | 128 | 146.9 ms | 136 | 15.76 (10.7%) | 12.85 (8.7%) | **2.20 (1.5%)** | 15.05 (10.2%) | **0.96×** |

So the full per-feature model — predicates plus declaration walk plus intern hash — costs **0.94–0.96×
the flattened rule walk it would replace**, once the zoom prefilter it needs anyway is applied.
Without that prefilter it is 1.09–1.10×, so the prefilter is not an optimisation to add later; it is
part of the design.

Both terms are near-parity with today, which is the point: the per-feature model is not chosen for
decode speed, it is chosen because it removes the `(N+1)^dimensions` load-side wall above, and it
costs nothing at decode to do so.

**Caveats.** The absolutes carry probe overhead (three clock reads per group, ~1,400 groups a tile)
and `decode` includes both shadow terms; the ratio is what four runs agree on. The declaration
array's *bits* are synthetic, so the branch-prediction pattern is not the real one — a real
implementation could differ on the walk term, which is 1.4% of decode. The table is pruned at z16
while a tilted view also decodes parent tiles at other zooms.

**`setPixelScale` used to reload the style.** It rebuilt the symbolizer context by calling
`updateCurrentStyleSet`, i.e. a full parse + compile, and `VectorTileLayer` calls it when the layer
joins a map — so every startup paid the ~0.5 s twice. Split into `updateSymbolizerContext()`
(fonts, bitmap/stroke/glyph maps, settings), the second pass is **~100–130 ms**. `addFallbackFont`
took the same path. `setCartoCSSLayerNamesIgnored` genuinely changes compilation and still reloads.

**…and that second pass then cost 133 ms for nothing.** Two things were being redone that a pixel
scale cannot change:

- `setPixelScale` cleared the whole `_assetPackageSymbolizerContexts` entry, so the style's fonts
  were decoded again — **75 ms for the 15 fonts** of the bundled project (1.1 MB of woff2) plus
  27 ms for the system fallback. Only the **stroke and glyph maps** hold anything rasterized at a
  pixel scale; the font and bitmap managers do not. `resetSymbolizerContextRasterMaps` now replaces
  those two and keeps the managers.
- `mvt::resolveLiveStyleParameters` ran on every `updateSymbolizerContext` — **38 ms** on the
  23-layer style — although it is a property of the compiled map alone. It moved next to
  `_map`'s assignment, with `getSelectionParameter`.

Measured on the Crosscall, three interleaved pairs, the whole `setPixelScale` call: **133.5–133.9 →
0.5 ms**, spread under 0.5 ms. It does *not* show up end to end — surface-created → first tile
request varies by ±150 ms on this device, which swamps it — so this is CPU removed from the startup
path, not a startup number.

Where the rest of a cold style load goes today (same device, bundled `assets` project, 23 layers):
parse + compile + translate **251 ms**, first symbolizer context **143 ms** (of which 75 ms is
fonts).

**The style's fonts are decoded when they are asked for, not when it loads.** Registering a font
means reading its name table, and a **woff2 has to be decompressed to be read at all** — measured
per font on the Crosscall, and the cost tracks the compression, not the typeface: `osm.ttf`
(69 KB, plain TTF) **0.1 ms**, a 26 KB woff2 **1.1 ms**, a 180 KB woff2 **11.4 ms**. The bundled
project packs 15 fonts and asks for **4** (DIN Pro Medium / Medium Italic / Bold, and `osm` for the
icon glyphs; `Arial` resolves to the system Roboto), so 10 were decompressed for nothing — and the
four NotoSans among them are 43 ms of the 62.

`FontManager::addPendingFontData` takes a font plus a **hint name** — the file name — and decodes it
only when a request normalizes to that hint (`DINPro-MediumItalic.woff2` ↔ `DIN Pro Medium Italic`,
the same normalization `SystemFontUtils` uses for `/system/fonts`). A request matching no hint falls
to the `FontDataLoader`, and only then sweeps every font still pending and registers the names they
really carry — so a package whose file names disagree with its font names resolves exactly as eager
loading did, once. **Symbolizer context 98 → 27 ms**, three interleaved pairs.

The order matters and cost a round: with the sweep *before* the loader, resolving the default
fallback font — `Arial`, which no style package carries — swept all 15 on every context build and
the change measured exactly nothing. The one behaviour difference left: a name the `FontDataLoader`
also answers now resolves from there rather than from a package font whose *file* name did not
match. Eagerly loaded fonts always won that; hinted ones still do.

**Decoding a tile: 120–150 ms mean, ~0.5 s worst** at a z16 city camera. Section split (probe
overhead ~30%, so read the shares, not the absolutes):

| section | share |
|---|---|
| symbolizer → geometry/label build (tesselation) | 35% |
| loop glue: `shared_ptr`-keyed caches, 67 layer builders per tile, batching | 22% |
| `TileLayerBuilder::buildTileLayer` | 13% |
| filter predicate evaluation | 12% |
| feature tag decode | 7% |
| protobuf geometry decode + clip | 5% |
| symbolizer property evaluation | 4% |
| rule prefilter + field gathering | 2% |

So the CartoCSS/expression machinery is **~18% of a decode** — geometry building is the cost.

### Three cuts to the loop glue — measured, and worth nothing

Three output-identical cuts to the per-style setup in `mapnikvt` were built and measured together.
**They do not move tile decode.** One was kept for tidiness, two were reverted; the measurement is
worth more than any of them, because it says the per-style setup is not where the glue cost is.

**Result.** Crosscall, default city camera (Grenoble z16.22 tilt 26), `--es style assets`, warm
persistent tile cache so every tile decodes from disk, `RelWithDebInfo` arm64, temporary probe
around `reader.readTile` in `MBVectorTileDecoder::decodeTile`, mean over the first 64 decodes of a
run. **Five interleaved pairs from two prebuilt APKs, no rebuild inside a pair:**

| pair | before | after | Δ |
|---|---|---|---|
| 1 | 154.06 | 151.26 | −2.80 |
| 2 | 151.18 | 153.25 | +2.07 |
| 3 | 155.49 | 153.11 | −2.38 |
| 4 | 150.23 | 156.00 | +5.77 |
| 5 | 152.42 | 138.83 | −13.59 |
| mean | 152.68 | 150.49 | **−2.19 ms (−1.4%)** |

Paired-delta sd is 7.3 ms and two pairs of five favour the old code, so −1.4% is noise. Decode count
was 64 in every run, before and after — the changes alter no output.

**Two sequential series said −3.2% and that was drift.** Three runs of one build then three of the
other gave 152.94 → 148.00 with no overlap between the two ranges, which reads as a clean win and is
not one; it did not survive interleaving. This is the trap the top of this page describes, and it
cost a wrong conclusion here. Never A/B this device in series.

**Why nothing moved — the useful part.** A counter over the same run: **67.1 styles per tile, of
which 32.5 skip on the absent layer, 10.3 on the zoom prefilter, and 24.4 actually build.** So the
skip fires on nearly half of all styles and still buys under half a millisecond of a 150 ms decode:
building a `TileLayerBuilder`, gathering field sets and calling `buildTileLayer()` for a layer with
no features costs on the order of **10 µs**. The "67 layer builders per tile" in the section split
above is therefore misleading as a cost attribution — the builders that cost anything are the **24
that have features**, and the glue is inside `processLayer`'s per-feature loop, not in the per-style
setup around it. Anything aimed at the 22% has to go there.

Nothing downstream should be planned as though the 22% had shrunk.

**Kept: a layer the tile does not carry is skipped whole.** `TileReader::readTile` built a
`vt::TileLayerBuilder` and called `buildTileLayer()` for every style that survived the zoom
prefilter, then discovered the layer was absent when `createFeatureIterator` returned null — the
style's field sets were gathered first, for nothing. The bundled style has 67 styles over 23 layers
and a z16 city tile carries far fewer, so most of that work produced an empty layer that was then
dropped. `TileReader::hasLayer` (a `_layerMap` lookup in `MBVTFeatureDecoder`, virtual so
`TorqueTileReader` keeps answering yes) is now asked once per layer, before anything is built.

The one case that must still be built is a style with a **comp-op**: `GLTileRenderer` renders an
empty layer when `isEmptyBlendRequired(compOp)` ([GLTileRenderer.cpp:2587](https://github.com/massif-maps/massif-maps-libs/blob/develop/vt/src/vt/GLTileRenderer.cpp)),
so dropping it would change the frame. The skip is therefore `!layerPresent && !style->getCompOp()`.

**Reverted: one feature-data cache per field set, for the current layer.** `createLayerFeatureIterator`
holds its two caches in one slot each, keyed by a string: the layer name for geometry, the layer name
plus every field name for feature data. A key that does not match throws the cache away.

The geometry one was **already fine, and the first read of it was wrong**: `readTile` iterates
`for layer { for style }` and `CartoCSSMapLoader` builds exactly one `mvt::Layer` per layer name
with the attachments as its consecutive styles ([CartoCSSMapLoader.cpp:365](https://github.com/massif-maps/massif-maps-libs/blob/develop/cartocss/src/cartocss/CartoCSSMapLoader.cpp)),
so a layer's styles never interleave with another layer's and the single slot is discarded exactly
when the loop leaves the layer. Nothing to win there.

The feature-data one does thrash, but only *within* a layer: `#road`, `#road::casing` and
`#road::labels` are consecutive styles asking for different field sets, so each attachment drops the
layer's `FeatureData` and the next one rebuilds all of it. Holding one cache per field set against
the current layer fixes that and measured nothing, so it was reverted rather than carried — it also
keeps several caches alive per layer instead of one, which raises peak decode memory for no return.

**Reverted: the per-feature field-set probes as a byte lookup.** `MBVTFeatureIterator::getFeatureData`
filters tags with `fields->find(key)` — a `std::set<std::string>` probe per tag per feature, run
twice per feature because the reader asks once with the filter fields and once with the symbolizer
fields. Note for anyone tempted to delete those probes as redundant: the iterator's `_keyFieldMap`
does **not** imply membership, it is built from the *union* of the two sets and each call narrows it
further. Resolving each set once into a `char` mask over the key indices works and measured nothing,
so the complexity was not kept.

### The compiled map is cached

A compiled `mvt::Map` is read-only, and a decoder's parameter values live in its own store, so the
same map can serve several decoders. `MBVectorTileDecoder` keeps a small process-wide cache keyed by
**(asset package, style asset name, `cartoCSSLayerNamesIgnored`)** — weak references plus the last
two held strongly, so a day/night pair stays warm without pinning every style ever loaded. Measured
on the device, loading the same style a second time: **411 ms → 0.00 ms** (the symbolizer context is
already cached by asset package too, so the second load is free end to end).

The key is the asset **package object**, not its contents: two styles of one package (the day/night
case, `CompiledStyleSet(pack, "day")` / `(pack, "night")`) hit the cache, but re-creating the package
around the same files does not. Hashing the assets to do better would cost more than it saves for
the single-load case.

### Live style parameters

`setStyleParameter` used to invalidate every tile ([TileLayer.cpp](https://github.com/massif-maps/MassifMaps/blob/master/all/native/layers/TileLayer.cpp)
`updateTiles`), so changing one `param::` colour cost *visible tiles × ~130 ms* of decode CPU. A
parameter that **only** feeds properties the renderer evaluates per frame does not need any of that:

- the values live in a `mvt::StyleParameterStore` that decoded tiles hold a pointer to, so replacing
  them is visible to already-decoded tiles;
- a colour/width property whose expression reads parameters (and at most the view state) becomes a
  `vt::ColorFunction`/`FloatFunction` instead of being folded at decode — `Property::isLiveCapable`;
- `mvt::resolveLiveStyleParameters` classifies each parameter at load, and `MBVectorTileDecoder`
  takes the cheap path only when **every** parameter in the call is live: swap the values, ask for a
  redraw (`onDecoderRefreshed`), decode nothing.

Conservative by construction. A parameter is **not** live when it appears in a rule filter (it
decides what the tile contains), when it feeds a property that is also read at decode time — glyph
raster size, generated marker bitmap, stroke pattern (`Property::isBakedAtDecode`) — when the
expression also reads a feature field or the zoom, or when it drives `_geometryscale`, `_fontscale`
or `_zoomlevelbias`. Anything unclassified stays on the re-decode path.

Measured on the device with the demo's in-memory style project (`--es style project`): flipping a colour
parameter every 3 s produced **zero `decodeTile` calls** and the water polygons changed between the
two colours in the next frame; flipping the boolean the style uses in a filter still re-decodes, as
it must. Worth knowing: the bundled 23-layer style has **no** live parameter — its 461 parameters all
sit in filters, text or marker sizes — so this pays only for styles written with colour parameters,
which is the point of the feature.

Classification costs ~37 ms once per style load on that style (a walk over every rule and property).

### Selection: the appearance half, without a decode

A selection is a parameter compared with a feature field — `[param::selected_id] = [osmid] + ''` —
which the classification above rejects, because the comparison can only be answered per feature. The
**appearance** half of it no longer needs a decode either, for a style that asks:

```json
"styleparameters": { "selected_id": { "default": "", "selects": true } }
```

Opt-in on purpose. It only works for a style written a particular way, so inferring it would make
every other style pay a walk over its rules to be told no, and would leave an author whose style
just misses the conditions with no way to find out. `resolveSelectionParameter` returns before
touching anything when no parameter declares itself, and warns with the reason when a declared one
does not qualify.

A geometry already carries up to 16 style slots, whose colour and width are uniform arrays refilled
every frame, and every vertex names its slot in one byte of `aVertexAttribs`. So the decoder folds
the comparison BOTH ways: it builds the symbolizer twice, once with the parameter forced to the
feature's own value and once to a value it cannot equal, and both answers land as two slots of the
same geometry. Nothing else about the feature changes, so it is tesselated once.

- `mvt::resolveSelectionParameter` verifies the declared parameter at style load and marks the
  properties it may fold (`Property::setSelectionFoldable`). A folded property reads no parameter, so
  it collapses to a constant — which is what makes it a slot, and what lets every unselected feature
  share one.
- `ExpressionContext::setStyleParameterOverride` is how the fold is forced; `TileReader::createSelectionFeatureProcessor`
  runs the branch that is not drawn over an EMPTY feature collection, so it registers its slot without
  laying down vertices.
- Each feature keeps a 64-bit `hashValue` of what it is compared with, next to the vertex run
  (`vt::TileGeometry::FeatureStyleRange`). `MBVectorTileDecoder` publishes the hash of the parameter
  in a shared atomic; `GLTileRenderer` compares the two in `buildCompiledTileGeometry`, rewrites the
  style byte of the runs that changed and re-uploads exactly those bytes with `glBufferSubData`.

Deciding it on the render thread rather than walking the tile cache is what keeps it free of locks:
the vertex data is only ever touched where it is uploaded, and a tile decoded later picks the state
up on its own.

Conservative, because a fold that got the tesselation wrong could not be undone by a repaint. The
parameter has to be read only by the `stroke`, `stroke-opacity` and `stroke-width` of line
symbolizers — the three that end up as slots and touch no vertex — always inside an `=` against the
same field expression, never in a rule filter, never beside another parameter in one property. A
dashed line whose width is selected is refused as well: the dash raster is sized by the width, so the
two branches would not share their vertices.

Measured with the demo's selection bench (`--es routeSelect true --es routeSelectCycle 2500`,
12 routes, z12.5, `--es tilt 90`), with a temporary `decodeTile` probe. `value` mode goes from 6
`decodeTile` calls per selection to **zero**, on the emulator and on the Crosscall alike, and
`setStyleParameter` from 2.2-3.6 ms to **0.32-0.81 ms** on the device (0.08-0.39 ms on the emulator).
`filter` mode still decodes its 6 tiles per selection, as it must, and logs `it is read by a rule
filter, which decides whether the geometry exists at all`. The selected route changes colour and
width in the next frame in both, and the 23-layer base-map style is unaffected - it declares no
selecting parameter, so its rules are never walked.

**What is still a decode: the structural half.** `when ([param::selected_id] = [osmid] + '')::selected`
decides whether the casing geometry exists at all, and no repaint can build geometry. A style that
wants a free selection has to express the casing as appearance — a width and a colour that fold —
rather than as a rule. The durable answer for the general case is maplibre's `feature-state` model:
the selected id becomes a **uniform** compared per vertex against a feature-id attribute, which needs
a feature-id vertex attribute in `vt` and shader support — not done.

## Measured NOT to matter — do not re-run these

| hypothesis | result |
|---|---|
| geometry volume (area subdivision off) | indices 37.3M → 7.5M, **+6.5%, inside the noise** |
| per-vertex DEM taps 16 → 1 | 5.69 vs 5.86 fps with content — nothing (worth ~20% terrain-only) |
| tile LOD granularity (`--es maxTileZoomOffset -1`) | 11.46 → 11.16 fps |
| paint-as-ground (`debug.massif.groundpaint 1`) | nothing, twice |
| the far plane (tangram's formula) | never binds at the cameras tested |
| tile decode pool size 1 → 4 | no change warm or cold |
| shadows off / sky shader off | ~0 |
| half display resolution (¼ the pixels) | +13% — not fill-bound |
| lattice clamp on surfaces (16 taps → 4) | correct but unmeasurable |
| the per-tile CPU surface rebuild ("the pan hang") | **does not happen at all** in grid mode: `surfBuilt=0 surfInval=0`, the block costs 0.04 ms |
| DEM border patching instead of full re-encode | 93% fewer re-encodes, **no fps change** at any camera — the encode was never on the render thread |
| the DEM encode path in a warm pan | **zero encodes** — there is nothing there to optimise |
| skipping the layer builder for styles the prefilter empties (67 per tile) | 3 paired cold runs each: decode mean 148 vs 147 ms — inside the noise |

## Things that did pay

| change | effect |
|---|---|
| native `-O2` instead of `-O0` | 8.6 → 12.8 fps |
| sort key computed once per tile instead of per comparison | part of 7.3 → 8.7 fps |
| elevation texture bitmap built on the encode worker | same pair; `layers` 49.5 → 26.0 ms, fps p25 3.9 → 8.1 |
| shared ground (no per-layer pre-pass, no stencil masks) | mask draws 4209 → 0, surface draws −39% |
| hillshade as a terrain paint | +29% fps in a background-only style |
| mesh resolution 128 → 64 (tangram's value) | 8.5 → 15.2 fps |
| occlusion depth read-back on its own thread | 13.7 → 14.9 fps |
| contour lines as a shader block | render tiles 494 → 216 |
| contour label stubs + shader lines (device) | 14.5 → 16.6 fps, `layers` 8.7 → 7.0 ms |
| label mutex taken per 32 labels instead of per label | culler pass 19.4 → 15.4 ms (device, ~1960 labels) |
| label terrain re-anchor: DEM tile loads no longer read as a scale-only change, plus a grid and a latitude-scale memo | full stack over terrain, interleaved ×3: **1.00 → 1.55 fps**, `prepare` 658 → 219 ms |
| an off-screen, already-anchored label defers its re-anchor | 1.60 → 1.70 fps — small, most dirty labels do hold a placement |
| label lines tesselated for reading, not for painting (no lattice split, surface-cell step) | **1.75 → 2.10 fps**, `prepare` 157 → 72 ms |
| `setPixelScale` rebuilds only the symbolizer context, not the compiled map | startup style cost 2 × 0.5–0.7 s → one load plus a ~0.1 s context rebuild |
| `setPixelScale` keeps the fonts and bitmaps, drops only the rasterized maps; live-parameter classification moved to where the map changes | the whole call **133.5–133.9 → 0.5 ms**, interleaved ×3 |
| a style's fonts are decoded on first use, matched by file name (the bundled project packs 15 and asks for 4) | symbolizer context **98 → 27 ms**, interleaved ×3 |
| CartoCSS compile: per-zoom predicate memo, field-bucketed property insert, interned field ids | compile 264 → 157 ms, `buildMap` 362 → 261 ms (23-layer style, device) |
| CartoCSS compile: skip a zoom whose predicate results repeat, reuse the trial property set | compile 157 → 129 ms, `buildMap` → 220 ms |
| CartoCSS compile: field and predicate bitmasks on the property set, evaluate each property once | compile 175.4 → 117.4 ms paired (same style, later and bigger than the rows above) |
| live style parameters (a colour-only parameter swaps values and redraws) | a parameter change went from *visible tiles × ~130 ms* of decode to **zero decodes** |
| compiled-map cache keyed by asset package + style name | loading the same style again: 411 ms → **0.00 ms** |
| render and tile paths at `-O2` in Release instead of `-Oz` | device 39.09 → 37.82 ms/frame (3.2%), CPU work minus the swap wait 14.39 → 13.51 ms (6.1%), `prepare` 2.65 → 2.22, `prelude` 0.91 → 0.70; +614 KB on arm64 |

The `-Oz` → `-O2` A/B is a warning about the emulator as much as a result. Three interleaved cycles
on the emulator put the mean 4.8% apart but reversed the sign on one cycle out of three — no
conclusion. The same six runs on the device (Adreno 610) favoured `-O2` in **3 of 3 paired runs**.
Anything worth a few percent needs the device and needs pairing; a single emulator run will happily
report 10%.

### The label culler, measured on the device

A pass is `cullMs / cullPasses` from the `RenderStats:` line. At a POI-dense camera with ~1960 live
labels it was **19.4 ms**, and splitting it showed **44% of that was waiting on `labelMutex`** — the
loop took and released it once per label, ~1900 times, against a GL thread that holds it to build
label vertices. `BatchLock` (32 labels per acquisition, handed back around the sort) is most of the
win. The rest: two screen-aligned envelopes whose bounds intersect *do* overlap, so the
separating-axis test is skipped for them (`CullRecord::axisAligned` — exact, not an approximation,
and it covers every billboard label), and the two per-label heap allocations in the collection loop
became reused buffers.

Ruled out on the way: SAT was **not** the bottleneck — the fast path alone barely moved a style with
no anchors. And the batch size does not trade against frame time the way it looks like it should:
batch 8 and batch 32 measure the same worst frame (~46 ms against ~40 before). The worst-frame cost
is the culler doing the same work in a denser burst, not the mutex being held longer.

## Runtime switches (no rebuild)

`adb shell setprop debug.massif.<name> <value>` — `demtaps`, `groundpaint`, `tilebg`,
`areathreshold`, `areasourcedensity`, `linesourcedensity`, `depthshift`, `terrainpaint`,
`paintdetail`, `asyncdepthms`, `gputimer`. They are read **once per process**, so restart the app
after setting one, and **reset them when you are done** — they survive until reboot.
