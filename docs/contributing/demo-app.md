---
title: The demo app and the debugging loop
---

# The demo app, and how to debug the renderer

Everything about `scripts/android-dev`: the two screens, the intent extras, the probes that work,
and the measurement discipline. [`CLAUDE.md`](https://github.com/massif-maps/MassifMaps/blob/master/CLAUDE.md)
routes here rather than carrying it, because none of it is needed to answer an ordinary question.

## The app has TWO screens, and the bench is NOT the one it opens on

| Activity | What it is |
|---|---|
| `.MainActivity` | the **example gallery** — one file per example, on the facade API. Not for debugging. See [examples.md](examples.md) |
| `.ExampleActivity` | runs one example: `--es example <id>`, plus `--es ui false` and `--es lon/lat/zoom/tilt/rotation` |
| `.BenchActivity` | the **composable debugging/measurement map** — every layer switch, every intent extra, `DemoLive` |

A debugging run names `.BenchActivity` explicitly; `am start` with no activity opens the gallery.

## The fast loop

```sh
cd scripts/android-dev && ./gradlew :app:assembleDebug -x lint   # ~40 s incremental (native included)
adb install -r -t app/build/outputs/apk/debug/app-debug.apk      # -t: the APK is test-only
adb shell am force-stop com.massifmaps.MassifDemo
adb shell am start -n com.massifmaps.MassifDemo/.BenchActivity --es ui false --es drape false
```

`-PabiFilters=arm64-v8a` narrows the native build to one ABI — enough for a device or an arm64
emulator, and much faster than the default four.

- Install from `app/build/outputs/apk/debug/`. `app/build/intermediates/apk/debug/` also holds an
  `app-debug.apk` and it is **stale**. Verify with
  `unzip -p <apk> classes*.dex | strings | grep <new symbol>` (the demo's Java lands in classes5.dex).
- `pm clear com.massifmaps.MassifDemo` resets camera/caches. Without it the persistent tile caches
  stay warm, which is usually what you want — but two runs that differ only in cache state are **not
  a valid A/B**.

### Settle time

Tiles need **60–90 s** to settle before a screenshot means anything (network + persistent cache +
label placement). For anything involving 3D extrusions — shadows above all — this is not optional:
buildings arrive asynchronously, and **before they cast there are no shadows to be wrong**. A frame
taken too early shows a clean scene that looks exactly like a fix.

## The demo files

| File (under `app/src/main/java/com/massif-maps/test/`) | Role |
|---|---|
| `demo/DemoConfig.java` | every default, one static field per knob + the intent-extra key map |
| `demo/DemoCfg.java` | `cfgBool/cfgFloat/cfgInt/cfgStr/cfgColor` intent readers (`--es key value`) |
| `demo/DemoMap.java` | builds/updates the map: layer registry, shared sources, terrain/light/sky, camera |
| `demo/DemoStyles.java` | style decoders (dir / zip / inline CartoCSS / style project) + demo shaders |
| `demo/DemoSky.java` | day-cycle sun/sky + generated sky shader |
| `demo/DemoPanel.java` | on-screen panel — writes DemoConfig, then calls a `DemoMap.apply*()` |
| `demo/DemoTests.java` | one-shot actions (routing, search, GeoJSON) |
| `ui/main/SecondFragment.java` | Android glue only (view, permissions, map listener) |

Change defaults in `DemoConfig` only. These files may carry **uncommitted** local edits (camera,
per-demo knobs): read before touching, keep changes additive, never restore from a backup or an
older commit.

## Intent extras

Layers (`base`, `satellite`, `hillshade`, `hypso`, `contour`, `contourTiles`, `routes`, `elements`,
`bugs`) toggle with `--es <name> true|false`; the base map has `--es base plain|composite` and
`--es style dir|zip|inline|project`. `dir` reads the style from a FOLDER via `DirAssetPackage`
(`/sdcard/alpimaps_mbtiles/osm`), falling back to `osm.zip` then to inline CartoCSS.

`bugs` is the STYLE REGRESSION repro layer (`DemoStyles.bugStyle`, `DemoMap.createBugsLayer`):
synthetic GeoJSON on the start camera, one feature per reported symptom, each with its A/B knob —
`bugLabelSize`, `bugBackOpacity`, `bugLineColor`, `bugLineBorder` (pair with `--es bugBackOpacity -1`),
`bugAllowOverlap`/`bugTextClip`. `--es bugs true --es ui false` shows all four.

It also carries the **bridge / tunnel span** (`bugbridge`, `bugtunnel`): one chord across the
Bastille ridge, 207 m at its south end and 641 m at its north, with the ground bulging ~60 m ABOVE
the chord in between — so "goes straight" is unmistakable, and a tunnel on the same chord is
genuinely inside the hill at mid-span. They are their own vt layers so the drape bake can be turned
off by name:

```sh
--es bugs true --es terrain true --es noDrape '^contour|bug(bridge|tunnel)'
```

Quote the filter for the DEVICE shell (the `|` is re-parsed there), and note `noDrape` needs a
RELAUNCH — an already-baked drape texture is not invalidated when the filter changes. Real bridges
are a poor substitute: Millau's DEM sits 220 m above the actual deck and the viaduct is longer than
a z14 tile. The span keeps INTERMEDIATE vertices on purpose (`bugSpanVertices`) — a two-point line
is already straight once it leaves the bake and would show a fix that is not there.

`--es demo terrain|project|composite` picks the configuration (default `composite`). Every knob in
`applyTerrainConfig`/`applyCameraConfig`/`applySkyAndLightConfig` is an intent extra, so most
experiments need no rebuild:

- camera: `lon lat zoom tilt rotation`
- terrain: `drape drapeLines drapeResolution meshResolution exaggeration`, `viewDistance
  viewDistanceMeters`, `autoFlatten autoFlattenTilt autoFlattenMs`, `stitch`
- layers: `hs sat satZoom contour bld3d`
- light/shadow: `daycycle sunHour sunAzimuth sunAltitude shadow`
- labels: `textOcclusion`, `roadLabelOcclusion` (a re-decode)
- `ui false` (hide the panel), `anim zoom|pan|rotate|zoomseq|approach`

**`--es buildings 0|1|2`** drives a COMPILED style's own `buildings` parameter (none / footprints /
extrusions), which is what a converted Mapbox Standard gates its 3D on. `--es bld3d` sets it too.

**`--es vectorZoomBias -1` for a source authored for MapBox's 512 px tiles.** This SDK's tiles are
256 px, so at the same view it asks for a level DEEPER than mapbox-gl does. The on-screen readout
prints `z=<sdk> (mb <sdk-1>)` for this reason.

**A second DEM behind an `OrderedTileDataSource`**: `demEncoding`, `dem2Url dem2Encoding
dem2MaxZoom`. `dem_encoding` is resolved per TILE. **Launch-only, and the encoding is stored WITH
the cached tile** — a relabelled source keeps rendering with the old one until `pm clear`.

### Changing a knob on the RUNNING app

```bash
adb shell am broadcast -a com.massifmaps.MassifDemo.CONFIG --es fog false
adb shell am broadcast -a com.massifmaps.MassifDemo.CONFIG --es fogPreset dusk --es zoom 14
```

Same keys as the launch extras; only the option groups whose keys arrive are re-applied, and the
camera is left alone unless a camera key is sent. This is how an A/B gets run without the tile set
changing underneath it. **`am start` on an already-running bench does the same thing** — the
activity is `singleTop` and `BenchActivity.onNewIntent` feeds its extras back through `DemoLive`.

The gallery has the same channel through `ExampleLive` (same short keys). Two traps:

- `ExampleLive.onReceive` writes the properties **inline on the main thread**, so a broadcast sent
  while the render thread is busy can ANR the app. Send config early, before the tiles load.
- The receiver must be registered `RECEIVER_EXPORTED` — `adb shell am broadcast` runs as the shell
  uid, and a `NOT_EXPORTED` receiver drops every one silently (`result=0`, no log).

`ExampleActivity` logs the camera on every move, in the form the launch extras take:

```bash
adb logcat -s ExampleActivity | grep camera
```

### Things that look like bugs and are not

- **A style knob needs a re-decode, not just an option apply.** Anything written into the CartoCSS
  (`style styleLight bld3d bldLight bldAmbient bldGradient bldGradientHeight`) is carried by the
  TILES, so `DemoLive` rebuilds the base layer for those keys. The inline style's whole sun/shadow/
  building block is gated on `--es styleLight true`.
- **A screenshot after a state change needs the map to have drawn TWICE.** The surface is
  double-buffered and WHEN_DIRTY draws exactly what was requested, so one frame lands in the back
  buffer and the screen keeps the old state — permanently, not briefly. If a change "sometimes does
  not apply", check the frame count before suspecting the change
  ([01-frame.md](../internals/rendering/01-frame.md)).
- **Camera clamp**: the terrain keeps a `cameraClearance` above the ground, so a start position
  inside a slope auto-zooms out and you get an empty grid screen. Zoom out a notch instead of
  assuming the render broke.
- **`--es shadowDistance` with a large value hangs the app** — the caster ring is sized by the
  shadow throw, so a big cutout explodes the tile count.

## Debugging the renderer — what actually works

**Measure first, look second.** Reading a screenshot is by far the most expensive thing in a
debugging loop, both in wall-clock and in context. Compute a number, and only open the image when
the number says something changed:

```bash
# Did anything change at all?
python3 -c "
from PIL import Image, ImageChops, ImageStat
a=Image.open('before.png').convert('L'); b=Image.open('after.png').convert('L')
d=ImageChops.difference(a,b)
print('mean %.2f  max %d' % (ImageStat.Stat(d).mean[0], d.getextrema()[1]))"
```

`mean 0.00 max 0` means the two frames are identical — no need to look at either, and it is also
how you catch a knob that never applied. Then, when you do look, crop and downscale to the region
in question rather than reading a full 1080×2400 frame:

```bash
python3 -c "
from PIL import Image
Image.open('shot.png').crop((0,700,1080,1500)).save('crop.png')"
```

Other measurements that beat looking:

```bash
# Brightness/contrast of one region, e.g. is a shadow there at all?
python3 -c "
from PIL import Image, ImageStat
im=Image.open('shot.png').convert('L').crop((560,950,1050,1230)); s=ImageStat.Stat(im)
print('mean %.1f stddev %.1f' % (s.mean[0], s.stddev[0]))"
```

- **A/B by feature, per screen row band, before probing the plumbing.** Screenshot with a layer on
  and off (`--es hs false`, `--es sat false`, `--es drape false`), diff the two, and count differing
  pixels per horizontal band (PIL, no numpy on this machine). If a band is 0.0% different, that
  content is *not being drawn there*; if it differs, it is drawn and the problem is elsewhere. This
  is what separated "tile never loaded" from "tile drawn but depth-rejected".
- Probes go in these places, in this order — layer culling → draw data → vt render tiles → draw:
  `TileLayer::buildFetchTiles` (visible set + cache misses, `typeid(*this).name()` tells you which
  layer), `TileRenderer::refreshTiles` (per-zoom tiles/bitmaps/geometries, log `this` to separate a
  composite's children), `RasterTileLayer::FetchTask::loadTile`, `ElevationTextureCache::getTexture`,
  and `GLTileRenderer::renderGeometry2D`.
- **vt has no logger** — use `__android_log_print(4, "massif", ...)`. When throttling a probe with a
  frame counter shared by several renderer instances, use a **prime** modulus: `% 120` with 4
  instances always logs the same one.
- `Shader::getUniformLoc` returns **0** for a uniform the compiler dropped, and 0 is a valid
  location — writing to it clobbers whatever lives at 0. `SkyRenderer` uses `glGetUniformLocation`
  and `>= 0` guards; copy that pattern.
- `setDebugWireframe` / `setDebugSurfacePrefill` in `TileRenderer.cpp` are hardwired false and mostly
  wash the frame out; the A/B diff above is more informative.
- Strip every probe before committing.

## OpenGL ES 3 is a hard requirement

Both platforms create an ES3 context and nothing falls back to ES2:
`android/java/com/massifmaps/ui/ConfigChooser.java` asks for `EGL_OPENGL_ES3_BIT` in **every** EGL
config, and `ios/objc/ui/MapView.mm` uses `kEAGLRenderingAPIOpenGLES3`. `GLES3/gl3.h` is included
directly (`renderers/utils/GLContext.h`).

So an ES3-only format or entry point needs no extension check and no ES2 path — `GL_R8`,
`glInvalidateFramebuffer`, MRT, `sampler2DShadow`. What is still version-dependent is the **shader
language**: programs are built at `#version 100` unless they ask for `ESSL3_FLAG`, and a driver that
refuses the 3.00 variant falls back to 1.00 (`GLTileRenderer::hasShaderVersionFallback`), so a
shader written for ESSL 3.00 must still have a 1.00 form. Note also that `precision mediump float`
blocks overflow on functions like `tan()` near π/2 — write the numerically safe form.

## Terrain, fog and sky wiring

Detail: [08-lighting-sky-fog.md](../internals/rendering/08-lighting-sky-fog.md). The pointers worth
knowing before reading it:

- One resolution point for both: `all/native/components/StyleEnvironment.h` — `resolveLighting()`
  and `resolveFog()` merge the app's `LightOptions`/`TerrainOptions` with the style's Map-block
  values, and `resolveFog` also *lights* the fog. Every consumer (`TileRenderer` → vt,
  `BackgroundRenderer`, `SkyRenderer`) must go through them, or the ground and the sky end up with
  different fog.
- `SkyRenderer` draws a full-screen ray-direction quad. Apps can replace the body with
  `SkyOptions.setShaderSource`; the wrapper declares `u_sunDir/u_sunColor/...` and a
  `fogAmount(rayDir)` helper — redeclaring any of them is a compile error and the renderer silently
  falls back to the built-in sky.
- `BackgroundRenderer` draws the flat z=0 plane past the terrain. It uses
  `Options.getBackgroundBitmap()` — **not** the CartoCSS `Map { background-color }`.
- `TerrainOptions.ViewDistanceFactor` ends the ground (tangram's rule). Pair a short one with fog or
  the ground ends on a hard edge.
- Fog is `FogOptions`, independent of the terrain. `--es fog <color>` or `--es fog false` is the
  master switch; `fogRangeStart fogRangeEnd` are in **multiples of the camera-to-focus distance**.
  `fogBlend` scales the GROUND as well as the sky — one angular term for both is what makes the two
  meet at the skyline with no seam.
- The sky is a physical atmosphere by default. `--es skyType gradient` gets the two-colour ramp back;
  the raymarch runs per fragment of VISIBLE SKY, so a low tilt is what pays for it.

## Comparing against older SDK code

A/B-ing a regression takes three steps, not one:

```sh
git checkout <sha> -- all/                        # 1. old sources
(cd libs-massif && git checkout <matching-sha>)    # 2. matching submodule commit
cd scripts && python3 swigpp-java.py --profile "standard+valhalla+geocoding+routing+packagemanager" \
  --swig /Volumes/dev/carto/mobile-swig/swig       # 3. regenerate wrappers, else the build fails
```

`generated/` is gitignored, not tracked: there is no `git checkout` that brings it back, and a
`swigpp-java.py` run overwrites the tree's wrappers with whatever `--profile` you passed. Restore
the same way (`git checkout HEAD -- all/`, submodule back to its branch, regenerate).

A **temporary WIP commit** is the safe way to set work aside for a comparison — the git stash stack
is shared with the main checkout and every other worktree.
