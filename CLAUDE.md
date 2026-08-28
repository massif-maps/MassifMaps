# Massif Maps (Akylas fork)

C++ map SDK for Android / iOS / UWP (and desktop via the same native core). This is the
Akylas/farfromrefug fork of CartoDB/mobile-sdk with many custom features (hillshade,
Valhalla routing, custom label rules, PMTiles, ...).

## Repository layout

| Path | What it is |
|------|-----------|
| `all/native/` | Core SDK C++ (layers, renderers, datasources, projections, ui, vectortiles...) |
| `all/modules/` | SWIG interface files (`*.i`) — public API surface, mirrors `all/native` |
| `all/native/api/` | The **facade API** — ids, handles, JSON specs, events. A second public surface, derived from the `.i` attribute macros; [design](docs/internals/api-facade.md), [#146](https://github.com/massif-maps/MassifMaps/issues/146) |
| `tests/` | Host-native ctest suite over what links without the renderer — `cd tests && ./run.sh` |
| `libs-massif/` | **git submodule** (massif-maps/massif-maps-libs): `vt` (GL vector-tile renderer), `mapnikvt`, `cartocss`, `geocoding`, `sgre`/`osrm` routing, `nml` |
| `libs-external/` | **git submodule** (massif-maps/massif-external-libs): third-party deps (cglib, freetype, harfbuzz, `mlt` = maplibre-tile-spec, decoder only, ...). `boost` is expected as a symlink here (see BUILDING.md) |
| `android/`, `ios/`, `dotnet/`, `winphone/` | Platform glue code |
| `scripts/` | Build scripts (`build-android.py`, `build-ios.py`, `swigpp-*.py`, CMake in `scripts/build/`) |
| `tools/style-cli/` | `@massif-maps/style-tools` — the `massif-style` CLI (`css2xml`, `mvt2xml`, `mapbox2css`). C++ from `libs-massif` compiled to wasm, wrapped in TypeScript; [spec](docs/contributing/style-tools.md) |
| `docs/` | **All documentation, one tree** — also published as-is at massif-maps.github.io/MassifMaps |
| `website/` | The Docusaurus shell only (config, theme, React pages). It reads `../docs`; no content lives here |

## Where the documentation is

Read the page, do not re-derive it. `docs/` is the source of truth and the published site.

| Need | Path |
|---|---|
| how a render subsystem works | `docs/internals/rendering/` — [index](docs/internals/rendering/index.mdx) routes by subsystem |
| whole-SDK map, threads, data flow | [`docs/internals/index.mdx`](docs/internals/index.mdx) |
| what was measured and what failed | [`docs/internals/performance-log.md`](docs/internals/performance-log.md) |
| binary size, build time, ccache/ninja | [`docs/internals/build-and-size.md`](docs/internals/build-and-size.md) |
| upgrade a vendored dep, platform quirks | [`docs/maintenance/`](docs/maintenance/index.md) |
| what an app developer sees | `docs/features/`, `docs/guides/`, `docs/getting-started/` |
| the facade API — verbs, property table, specs, events | [`docs/internals/api-facade.md`](docs/internals/api-facade.md) |
| renames from the CARTO SDK | [`docs/migration.md`](docs/migration.md) |
| superseded designs — **not current** | `docs/_archive/` |

Rules for keeping them correct are in
[`.claude/CLAUDE.md`](.claude/CLAUDE.md#documentation--every-change-updates-it).

**Submodule gotcha:** changes under `libs-massif/` or `libs-external/` must be committed
inside the submodule (branch `develop`), then the submodule pointer updated in the main
repo. Commit style is conventional-commits (`fix:`, `feat:`, `chore:`).

## Conventional commits and PR workflow

All commits and PR titles **must** follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>[optional scope]: <description>
```

Common types:
- `feat:` — a new feature (triggers a minor version bump in changelogs)
- `fix:` — a bug fix (triggers a patch bump)
- `chore:` — maintenance, CI, dependency updates (no version bump)
- `docs:` — documentation only changes
- `refactor:` — code change that is neither a fix nor a feature
- `perf:` — performance improvement
- `test:` — adding or correcting tests
- `build:` — build system or external dependency changes

A breaking change must append `!` after the type (e.g. `feat!:`) and/or include a
`BREAKING CHANGE:` footer. This is used for changelog generation and versioning.

When creating PRs via `gh pr create`, always use:
```sh
gh pr create --repo massif-maps/MassifMaps --title "feat: your title here" ...
```
(Without `--repo` the command targets the archived CartoDB upstream and fails.)

### A MassifMaps PR title IS the changelog entry

MassifMaps PRs are squash-merged, so the PR title becomes the commit subject and the changelog
generator quotes it verbatim (`### BREAKING CHANGES`, `### Bug Fixes`, one line per PR). Whoever
reads the release notes sees that sentence and nothing else.

This bites hardest on a PR that only carries a **submodule pointer bump** (`libs-massif`,
`libs-external`), where the diff is one line and the real work is in the other repo. Title it by
what an SDK USER gets, never by the mechanics:

| ✗ | ✓ |
|---|---|
| `chore: bump libs-massif` | `fix(vt): lay a line label flat on the map again, as 5.x did` |
| `fix: submodule pointer` | `feat(terrain): follow DEM tile borders across zoom levels` |
| `fix(vt): various label fixes` | `fix(labels): stop a small label being drawn under its own icon` |

- **One user-visible outcome per title**, in the imperative, readable without the diff. If the PR
  fixes several unrelated things, say the one that matters and let the body carry the rest — or
  split the PR.
- **Scope by subsystem** (`vt`, `labels`, `terrain`, `renderers`, `datasources`), not by repo.
- **`!` / `BREAKING CHANGE:`** whenever a style, an option default or an `all/modules/*.i` signature
  changes — that is the section a user actually reads before upgrading.
- The submodule PR keeps its own title, scoped to its module; the two need not match, and the
  MassifMaps one is the one that ships.

v6.0.0's changelog had to be rewritten by hand because the generated one read as a list of
mechanics. The fix is the titles, not the generator.

## Working in this checkout

`scripts/android-dev` is the live test bench. It is ONE composable demo, not a set of examples:

| File (under `app/src/main/java/com/massif-maps/test/`) | Role |
|---|---|
| `demo/DemoConfig.java` | every default, one static field per knob + the intent-extra key map (`applyIntentOverrides`) |
| `demo/DemoCfg.java` | `cfgBool/cfgFloat/cfgInt/cfgStr/cfgColor` intent readers (`--es key value`) |
| `demo/DemoMap.java` | builds/updates the map: layer registry, shared sources, terrain/light/sky, camera |
| `demo/DemoStyles.java` | style decoders (dir / zip / inline CartoCSS / style project) + demo shaders |
| `demo/DemoSky.java` | day-cycle sun/sky + generated sky shader |
| `demo/DemoPanel.java` | on-screen panel — writes DemoConfig, then calls a `DemoMap.apply*()` |
| `demo/DemoTests.java` | one-shot actions (routing, search, GeoJSON) |
| `ui/main/SecondFragment.java` | Android glue only (view, permissions, map listener) |

Layers (`base`, `satellite`, `hillshade`, `hypso`, `contour`, `contourTiles`, `routes`, `elements`, `bugs`) toggle live
from the panel or with `--es <name> true|false`; the base map has `--es base plain|composite` and
`--es style dir|zip|inline|project`. `dir` reads the style from a FOLDER via `DirAssetPackage`
(`/sdcard/alpimaps_mbtiles/osm`), falling back to `osm.zip` then to inline CartoCSS.

`bugs` is the STYLE REGRESSION repro layer (`DemoStyles.bugStyle`, `DemoMap.createBugsLayer`):
synthetic GeoJSON on the start camera, one feature per reported symptom, each with its A/B knob —
`bugLabelSize` (two label attachments, text gone at <= 10), `bugBackOpacity` (a `back/` instance
punches the main line out), `bugLineColor` (a translucent line breaks at its joins),
`bugAllowOverlap`/`bugTextClip` (allow-overlap alone routes line labels onto the clipped
geometry path). `--es bugs true --es ui false` is enough to see all four.

Change defaults in `DemoConfig` only — those fields are also what the panel mutates. These files
may carry **uncommitted** local edits (camera, per-demo knobs): read before touching, keep changes
additive, never restore from a backup or an older commit.

Comparing against older SDK code (A/B-ing a regression) takes three steps, not one:

```sh
git checkout <sha> -- all/                       # 1. old sources
(cd libs-massif && git checkout <matching-sha>)   # 2. matching submodule commit
cd scripts && python3 swigpp-java.py --profile "standard+valhalla+geocoding+routing+packagemanager" \
  --swig /Volumes/dev/carto/mobile-swig/swig     # 3. regenerate wrappers, else the build fails
```

The `generated/` wrappers in the tree reference the newer API and will not compile against older
headers. Restore the same way (`git checkout HEAD -- all/`, submodule back to its branch,
regenerate). SWIG is never run by gradle — any change to `all/modules/*.i` needs step 3 too.

**`generated/` is gitignored, not tracked.** There is no `git checkout` that brings it back: a
`swigpp-java.py` run overwrites the tree's wrappers with whatever `--profile` you passed (322 files
for the full profile, 256 for `standard`, 236 for `lite`), and the only way back is to run it again
with the profile you develop against. Size per profile is in [`docs/internals/build-and-size.md`](docs/internals/build-and-size.md).

`gh pr create` needs `--repo massif-maps/MassifMaps` (or `--repo massif-maps/massif-maps-libs`).
Both repos are forks of the archived CartoDB originals, and without `--repo` gh targets the
upstream and fails with "Repository was archived so is read-only".

## The Android demo app (the main dev loop)

**The app has TWO screens, and the bench is NOT the one it opens on:**

| Activity | What it is |
|---|---|
| `.MainActivity` | the **example gallery** — one file per example, on the facade API. Not for debugging. See [`docs/contributing/examples.md`](docs/contributing/examples.md) |
| `.ExampleActivity` | runs one example: `--es example <id>`, plus `--es ui false` and `--es lon/lat/zoom/tilt/rotation` |
| `.BenchActivity` | **the composable debugging/measurement map this file documents** — every layer switch, every intent extra, `DemoLive`. Everything below is about this one |

So a debugging or benchmarking run names `.BenchActivity` explicitly; `am start` with no activity
opens the gallery instead.

`scripts/android-dev` builds the native SDK *and* the demo in one gradle run. This is the
fast loop — not the full `build-android.py`:

```sh
cd scripts/android-dev && ./gradlew :app:assembleDebug -x lint   # ~40 s incremental (native included)
adb install -r -t app/build/outputs/apk/debug/app-debug.apk      # -t: the APK is test-only
adb shell am force-stop com.massifmaps.MassifDemo
adb shell am start -n com.massifmaps.MassifDemo/.BenchActivity --es ui false --es drape false
```

- Install from `app/build/outputs/apk/debug/`. `app/build/intermediates/apk/debug/` also holds
  an `app-debug.apk` and it is **stale** — installing it silently runs old code. Verify with
  `unzip -p <apk> classes*.dex | strings | grep <new symbol>` (the demo's Java lands in classes5.dex).
- Tiles need **60-90 s** to settle before a screenshot means anything (network + persistent
  cache + label placement). `pm clear com.massifmaps.MassifDemo` resets camera/caches; without it the
  persistent tile caches stay warm, which is usually what you want.
- `--es demo terrain|project|composite` picks the configuration (default `composite`).
  Every knob in `applyTerrainConfig`/`applyCameraConfig`/`applySkyAndLightConfig` is an intent
  extra, so most experiments need no rebuild: `lon lat zoom tilt rotation`, `drape drapeLines
  drapeResolution meshResolution exaggeration`, `viewDistance viewDistanceMeters`,
  `autoFlatten autoFlattenTilt autoFlattenMs` (render flat once the terrain's on-screen parallax
  drops below N px, or past a tilt — `--es autoFlatten 0 --es autoFlattenTilt 0` is the A/B),
  `hs sat satZoom contour bld3d stitch`, `daycycle sunHour sunAzimuth sunAltitude shadow`,
  `textOcclusion` (labels behind buildings fade to this opacity, 1 = off) and
  `roadLabelOcclusion` (the same for the road-name style layer alone, a re-decode),
  `ui false` (hide the panel), `anim zoom|pan|rotate|zoomseq|approach` (`approach` = dive close,
  pan along the slope, pull back out — the terrain close-approach repro shape).
- **`--es vectorZoomBias -1` for a source authored for MapBox's 512 px tiles.** This SDK's tiles are
  256 px, so at the same view it asks for a level DEEPER than mapbox-gl does — and a level deeper
  carries a level's worth of extra POIs. Comparing at mapbox z13.67 we drew bicycle parkings its
  z13 tile does not even contain. The on-screen readout prints `z=<sdk> (mb <sdk-1>)` for this
  reason: `mbref.html` shows MapBox's zoom, and the two are a whole level apart.
- **A second DEM behind an `OrderedTileDataSource`**, for the mixed-encoding case: `demEncoding`,
  `dem2Url dem2Encoding dem2MaxZoom`. `dem_encoding` is resolved per TILE, so the two need not
  agree. **Launch-only, and the encoding is stored WITH the cached tile** — a relabelled source
  keeps rendering with the old one until `pm clear`, which is a real result, not a stale build.
- **Change a knob on the RUNNING app** (`demo/DemoLive.java`) instead of relaunching — a relaunch
  rebuilds every cache, which is exactly what hides a stale-redraw bug:
  ```sh
  adb shell am broadcast -a com.massifmaps.MassifDemo.CONFIG --es fog false
  adb shell am broadcast -a com.massifmaps.MassifDemo.CONFIG --es fogPreset dusk --es zoom 14
  ```
  Same keys as the launch extras; only the option groups whose keys arrive are re-applied, and the
  camera is left alone unless a camera key is sent. This is how the A/B-per-band diff gets run
  without the tile set changing underneath it.
  **`am start` on an already-running demo does the same thing** — the activity is `singleTop` and
  `BenchActivity.onNewIntent` feeds its extras back through `DemoLive` — so one command form works
  whether or not the app is up, and it never relaunches when it is.
- **A style knob needs a re-decode, not just an option apply.** Anything written into the CartoCSS
  (`style styleLight bld3d bldLight bldAmbient bldGradient bldGradientHeight`) is carried by the
  TILES, so `DemoLive` rebuilds the base layer for those keys. And the inline style's whole sun /
  shadow / building block is gated on `--es styleLight true`: without it those `Map` properties are
  never emitted and every knob that feeds them silently does nothing.
- **A screenshot after a state change needs the map to have drawn TWICE.** The surface is
  double-buffered and WHEN_DIRTY draws exactly what was requested, so one frame lands in the back
  buffer and the screen keeps the old state — permanently, not briefly. `requestRedraw` now owes a
  follow-up frame for this; if a change ever looks like it "sometimes does not apply", check the
  frame count before suspecting the change ([`docs/internals/rendering/01-frame.md`](docs/internals/rendering/01-frame.md)).
- Fog is `FogOptions`, independent of the terrain (it fogs a plain 2D map too). `--es fog <color>`
  or `--es fog false` is the master switch; `fogRangeStart fogRangeEnd` are in **multiples of the
  camera-to-focus distance**, not metres, so one pair holds at every zoom. `fogHigh fogSpace
  fogStars fogBlend` are the mapbox atmosphere params, and `fogVertStart fogVertEnd` (metres) are
  the altitudes the haze fades out between, so summits stand clear of it. `--es fogSource style`
  takes every value from the inline style's `Map` block instead (needs `--es style inline`) — that
  is the path that can be zoom-dependent.
  **`fogBlend` scales the GROUND as well as the sky** — one angular term for both is what makes the
  two meet at the skyline with no seam, so there is no separate horizon angle to tune any more
  ([`08-lighting-sky-fog.md`](docs/internals/rendering/08-lighting-sky-fog.md)).
- The sky is a physical atmosphere by default (Rayleigh + Mie, per fragment).
  `--es skyType gradient` gets the old two-colour ramp back, which is the A/B for both look and
  cost; `--es skyQuality low|medium|high` is the sample count, `skyAtmoSun skyAtmoLum
  skyAtmoColor skyAtmoHalo` the rest. The raymarch runs per fragment of VISIBLE SKY, so a low tilt
  is what pays for it — measure there, not at tilt 85.
- Relief / peak-finder look: `reliefSurface true` (shaded terrain surface, only visible where no
  tile layer paints - pair it with `--es map false --es hillshade false`), `peakfinder true` (the
  outline effect, `peakfinderDelay` ms), `reliefDark`, `reliefWidth reliefHorizonBoost
  reliefThreshold reliefCrease reliefShade reliefAmbient reliefHaze reliefHazeDistance`.
  A panorama needs a LOW tilt: in this SDK tilt 90 is straight down, so the peak-finder camera is
  around `--es tilt 25`, not 85.
- Runtime UI: the gear at the bottom-left opens a settings panel (checkboxes + sliders for
  drape, mesh resolution, sun, shadows, fog, max visible distance). Driving it from adb works:
  `adb shell input tap 84 2236` toggles the panel, `input swipe` scrolls it and drags sliders.
- **Camera clamp gotcha**: the terrain keeps a `cameraClearance` above the ground, so a start
  position inside a slope (e.g. lat 45.2442 / lon 5.7606 at z14.68) auto-zooms out to ~z11.6 and
  you get an empty grid screen. Zoom out a notch (z13.6) instead of assuming the render broke.

## Debugging the renderer (what actually works)

- **A/B by feature, per screen row band, before probing the plumbing.** Screenshot with a layer
  on and off (`--es hs false`, `--es sat false`, `--es drape false`), diff the two, and count
  differing pixels per horizontal band (PIL, no numpy on this machine). If a band is 0.0%
  different, that content is *not being drawn there*; if it differs, it is drawn and the problem
  is elsewhere. This is what separated "tile never loaded" from "tile drawn but depth-rejected".
- Probes go in these places, in this order — layer culling → draw data → vt render tiles → draw:
  `TileLayer::buildFetchTiles` (visible set + cache misses, `typeid(*this).name()` tells you
  which layer), `TileRenderer::refreshTiles` (per-zoom tiles/bitmaps/geometries, log `this` to
  separate the composite's children), `RasterTileLayer::FetchTask::loadTile` (why a tile is not
  stored), `ElevationTextureCache::getTexture` (render zoom → DEM grid zoom), and
  `GLTileRenderer::renderGeometry2D` for what is actually visible/blended per target zoom.
- **vt has no logger** — use `__android_log_print(4, "massif", ...)` there. When
  throttling a probe with a frame counter shared by several renderer instances, use a **prime**
  modulus: `% 120` with 4 instances always logs the same one.
- `Shader::getUniformLoc` returns **0** for a uniform the compiler dropped, and 0 is a valid
  location — writing to it clobbers whatever lives at 0. `SkyRenderer` uses `glGetUniformLocation`
  and `>= 0` guards for this reason; copy that pattern.
- `setDebugWireframe` / `setDebugSurfacePrefill` in `TileRenderer.cpp` are hardwired false and
  mostly wash the frame out; the A/B diff above is more informative.

## Terrain, fog and sky wiring

- One resolution point for both: `all/native/components/StyleEnvironment.h` — `resolveLighting()`
  and `resolveFog()` merge the app's `LightOptions`/`TerrainOptions` with the style's Map-block
  values, and `resolveFog` also *lights* the fog (dark at night, warm at a low sun). Every
  consumer (`TileRenderer` → vt, `BackgroundRenderer`, `SkyRenderer`) must go through them, or
  the ground and the sky end up with different fog.
- `SkyRenderer` draws a full-screen ray-direction quad. Apps can replace the body with
  `SkyOptions.setShaderSource` — the wrapper declares `u_sunDir/u_sunColor/u_skyColor/
  u_horizonColor/u_groundColor/u_fogColor/u_fogBlend/u_time/u_zoom/...` and a `fogAmount(rayDir)`
  helper; redeclaring any of them is a compile error and the renderer silently falls back to the
  built-in sky (watch for that when a custom sky "does nothing").
- `BackgroundRenderer` draws the flat z=0 plane that fills the view past the terrain (and past
  `TerrainOptions.ViewDistanceFactor`). It uses `Options.getBackgroundBitmap()` — **not** the
  CartoCSS `Map { background-color }`, which is why changing the style background does not tint it.
- `TerrainOptions.ViewDistanceFactor` ends the ground (tangram's rule: 2 x camera height / cos(pitch
  + fovy/2), capped at 127 tile widths; 1 = their rule verbatim). Pair a short one with fog or the
  ground ends on a hard edge.

## Building / checking

Full builds take 1+ hour (see `BUILDING.md`; requires SWIG fork + boost symlink). Before that, run
the host tests — seconds, and the only check that exercises behaviour rather than syntax:

```sh
cd tests && ./run.sh
```

New work ships its own tests; the rules are in the [test](.claude/skills/test/SKILL.md) skill.

The Android-family build scripts (`build-android.py`, `build-routing-android.py`,
`build-xamarin.py`) pick **ninja** over make and prefix the compiler with **ccache**, both
auto-detected and both opt-out (`--ninja none`, `--ccache none`). Ninja is taken from `PATH`,
otherwise from the newest `$ANDROID_HOME/cmake/*/bin/ninja`. Switching generator clears the
affected `build/<target>-<abi>` directory — CMake refuses to reconfigure a Makefiles tree as
Ninja. Measured on one arm64 Release build with a private cache: 70.9 s cold, **13.8 s warm**
(1128/1134 direct hits), what is left being the thin-LTO link. Raise the cache first —
`ccache --max-size 30G` — because one ABI writes ~1 GB of objects and the 5 GB default makes the
four ABIs evict each other; the scripts warn when it is under 20 GB. The `scripts/android-dev`
gradle build already runs ninja through AGP and now picks up ccache too (`-Pccache=false` off).

For fast iteration on the vt renderer, a syntax/type check is enough:

```sh
clang++ -fsyntax-only -std=c++20 \
  -I libs-massif/vt/src -I libs-external/cglib -I libs-external/stdext \
  -I libs-external/angle-metal/include \
  -I <dir-with-boost-or-stub> \
  libs-massif/vt/src/vt/<file>.cpp
```

boost is only used for `boost::math::constants::pi` in vt; a one-line stub header works
if `libs-external/boost` is not set up.

Useful cglib semantics (libs-external/cglib): `bbox::inside(bbox)` = *intersects* (not
containment); `frustum3::inside(bbox)` = *intersects frustum*.

## OpenGL ES 3 is a hard requirement

Both platforms create an ES3 context and nothing falls back to ES2:
`android/java/com/massifmaps/ui/ConfigChooser.java` asks for `EGL_OPENGL_ES3_BIT` in **every** EGL
config, and `ios/objc/ui/MapView.mm` uses `kEAGLRenderingAPIOpenGLES3`. `GLES3/gl3.h` is included
directly (`renderers/utils/GLContext.h`).

So an ES3-only format or entry point needs no extension check and no ES2 path — `GL_R8`,
`glInvalidateFramebuffer`, MRT, `sampler2DShadow`. What is still version-dependent is the **shader
language**: programs are built at `#version 100` unless they ask for `ESSL3_FLAG`, and a driver
that refuses the 3.00 variant falls back to 1.00 (`GLTileRenderer::hasShaderVersionFallback`), so a
shader written for ESSL 3.00 must still have a 1.00 form.

## Rendering architecture (vector tiles + labels)

**Full technical documentation lives in [`docs/internals/rendering/`](docs/internals/rendering/index.mdx)**, split by
subsystem so one page can be read without the rest: the frame and threads, tiles and LOD, the GL
draw path, 3D terrain, the depth model, labels, hillshade/contours, lighting/sky/fog, the composite
layer, performance method, and the tangram comparison. The summary below is the orientation; that
set is the detail.

Threads: GL render thread (MapRenderer/onDrawFrame), tile-loading threads, plus
background workers in `all/native/renderers/workers/` (`CullWorker` computes visible
tiles per layer, `VTLabelPlacementWorker` runs label placement).

Data flow for a `VectorTileLayer`:

1. `CullWorker` → `TileLayer::calculateDrawData` → visible tile set.
2. `VectorTileLayer` decodes tiles (mapnikvt + cartocss) → `vt::Tile` with `TileLayer`s
   containing geometry + `TileLabel`s.
3. `TileRenderer` (all/native) wraps `vt::GLTileRenderer` (libs-massif/vt) which does all
   GL work: `startFrame` → `renderGeometry` → `renderLabels` → `endFrame`.

### Label pipeline (the flicker-sensitive part)

- `GLTileRenderer::setVisibleTiles` → `buildLabelMaps`: on **every tile-set change**, all
  `vt::Label` objects are recreated from the current tiles. Labels with the same
  `globalId` from different tiles are merged (`mergeGeometries` — one geometry copy per
  tile, identified by `(tileId, localId)`), and visibility/opacity/placement are carried
  over from the previous object via `snapPlacement`.
- `VTLabelPlacementWorker` (triggered by `MapRenderer::vtLabelsChanged` whenever draw
  data changes) creates **one fresh `vt::LabelCuller` per pass** and calls
  `TileRenderer::cullLabels` for every vector layer sequentially — the culler's screen
  grid intentionally accumulates across layers so labels of different layers collide.
  `LabelCuller::process` must therefore NOT clear the grid.
- `LabelCuller::process`: captures `wasVisible`, calls `Label::updatePlacement` (only
  re-places a label when its envelope fully left the frustum; resets opacity), projects
  envelopes to screen space, sorts by priority → wasVisible → layerIndex → size →
  opacity, then greedily inserts into a 16x32 screen grid with SAT polygon-overlap tests.
  A visible label keeps its slot unless a strictly higher-priority label overlaps it.
- The GL thread fades labels via `updateLabel` (`opacity` toward `visible ? 1 : 0`);
  invisible-but-fading labels stay rendered until opacity reaches 0.
- Custom per-label rules (fork additions): `allowOverlapSameFeatureId`,
  `sameFeatureIdDependent`, group ids with `minimumGroupDistance`. These compare the
  **placement's** `localId`, so placement identity stability matters.

**Placement stability invariant** (fix for labels jumping/disappearing while panning):
`snapPlacement` / `findSnappedPointPlacement` / `findSnappedLinePlacement` prefer the
geometry copy with the same `(tileId, localId)` as the previous placement. Without this,
re-snapping picks a winner by merged-list order (which changes with the tile set), and a
placement rebuilt from a differently-clipped copy of a line can fail line fitting
(`buildLineVertexData`) → the culler hides an already-visible label. Keep this invariant
when touching `Label`/`LabelCuller`.

Known remaining cost: `buildLabelMaps` reallocates every `Label` on every tile-set
change during panning; reusing unchanged labels would be the next perf win (careful: it
relies on fresh caches / snapPlacement semantics).

## Elevation / terrain (pointers for 3D terrain work)

- Elevation tile decoders: `all/native/rastertiles/ElevationDecoder.h` +
  `MapBoxElevationDataDecoder` (RGB-encoded) and `TerrariumElevationDataDecoder`.
- `all/native/layers/HillshadeRasterTileLayer.{h,cpp}`: consumes elevation tiles,
  has `getElevation(s)` queries; shading uses `vt::NormalMapBuilder`
  (libs-massif/vt/src/vt/NormalMapBuilder.cpp).
- Tile geometry/mesh generation: `vt::TileSurfaceBuilder` builds per-tile surface
  meshes; `vt::TileTransformer` (planar + spherical implementations in
  TileTransformer.cpp) abstracts tile-local → world transforms — 3D terrain would plug
  in here (displace surface meshes by elevation) plus depth handling in
  `GLTileRenderer`.
- Rendering projection modes: `Options::setRenderProjectionMode` (PLANAR / SPHERICAL);
  spherical mode already exercises the non-trivial TileTransformer paths.
