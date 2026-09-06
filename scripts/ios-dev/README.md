# iOS development environment

`scripts/ios-dev` is the iOS counterpart of [`scripts/android-dev`](../android-dev): one demo app
that exercises the SDK, with the SDK itself built as a dependency of the app rather than consumed
as a prebuilt framework. Touch a `.cpp`, hit build, and only that file recompiles — the same loop
gradle gives on Android.

## Setup

```sh
./bootstrap.sh                  # arm64 simulator (the usual dev target)
./bootstrap.sh device           # arm64 device
PROFILE=lite ./bootstrap.sh     # a different feature profile
PROFILE_RENDER=1 ./bootstrap.sh # per-frame timings, android-dev's -PprofileRender
```

`PROFILE_RENDER=1` compiles in `MASSIF_FRAME_PROFILER` and `MASSIF_VT_RENDER_STATS` — the per-frame
section timings and the vt draw/label/tile counters, printed as `PROF` and `RenderStats` lines.
Neither exists in the binary otherwise, so switching it is a re-bootstrap, not a launch argument.
Read them with `xcrun simctl spawn <udid> log stream` on the simulator, or the device console.
Method for an A/B is [`scripts/android-dev/bench/README.md`](../android-dev/bench/README.md): the
numbers drift, so only interleaved runs with medians over many windows count.

That regenerates the Objective-C bindings, configures the SDK with CMake's Xcode generator, and
writes `MassifDemo.xcodeproj` with [XcodeGen](https://github.com/yonaskolb/XcodeGen) (`brew install
xcodegen`). Re-run it only when the profile or the platform changes; day to day just build:

```sh
xcodebuild -project MassifDemo.xcodeproj -scheme MassifDemo -sdk iphonesimulator build
```

`MassifDemo.xcodeproj`, `Info.plist`, `.sdkproj` and `.angle` are all generated and gitignored.
`project.yml` is the source of truth.

## Feeding a NativeScript app from source

`build-plugin-lib.sh` is the same loop for an app that consumes the SDK through
`@nativescript-community/ui-massifmaps`: it builds the simulator slice and drops it into the
plugin's `MassifMaps.xcframework`, so `ns run ios` links what is in `all/native` right now.

```sh
./build-plugin-lib.sh            # build + install, ~5 s when one .cpp changed
./build-plugin-lib.sh --restore  # put the released lib back
```

It shares this directory's build tree (`build/ios_metal-SIMULATOR-arm64`), because the plugin's
framework is a **MetalANGLE** build - `MGLKView`/`MGLContext` are *defined* in the shipped lib,
angle having been merged in by the libtool step. Checking that with `nm -u` says the opposite and
is wrong: the merged symbols are defined, not undefined. There is no Apple GL alternative to build
instead - `vt/GLExtensions.h` includes `<GLES3/gl3.h>` unconditionally and that header only exists
on the include path when `_MASSIF_USE_METALANGLE` is set.

Only the `.a` is replaced. The shipped headers stay, which is correct as long as `all/modules/*.i`
is untouched; change a `.i` and the headers must be regenerated and reinstalled too.

The released lib is kept beside it as `MassifMaps.a.orig` on the first run, and its x86_64 half is
cached as `.dev-x86_64.a`. Only arm64 is built, so the fresh arm64 is fused back onto the released
x86_64 - the slice is advertised as `arm64 x86_64` in the xcframework's `Info.plist` and a thin lib
would contradict it. Everything lives inside the xcframework, which is gitignored in `ui-carto`.

An app can run this automatically from a `before-prepareNativeApp` hook in
`nativescript.config.js`, so a clean build sets itself up:

```js
hooks: [{ type: 'before-prepareNativeApp', script: 'tools/scripts/before-prepareNativeApp-massif.js' }]
```

`MASSIF_DEV_SDK=0` or `CI` skips it; `MASSIF_SDK_DIR` points it at a different checkout. The hook
fails the build when the SDK build fails, rather than letting the app link the previous binary.

## Configuring a run

Android takes its knobs as intent extras; iOS takes them as launch arguments, which UIKit folds
into `NSUserDefaults`. The key names are deliberately identical, so a camera or a layer set reads
the same for both demos:

```sh
adb shell am start -n com.massifmaps.MassifDemo/.MainActivity --es zoom 14 --es hillshade true
xcrun simctl launch <device> com.massifmaps.MassifDemo -zoom 14 -hillshade true
```

Supported today: `base`, `satellite`, `hillshade`, `terrain`, `lon`, `lat`, `zoom`, `tilt`,
`rotation`, `exaggeration`, `meshResolution`, `rasterUrl`, `demUrl`, `demEncoding`.

## Structure

Mirrors the Android demo file for file, so the two can be compared knob by knob:

| iOS | Android | Role |
|---|---|---|
| `DemoConfig.h/.m` | `demo/DemoConfig.java` | every default + the launch-argument key map |
| `DemoCfg.h/.m` | `demo/DemoCfg.java` | typed readers for the overrides |
| `DemoMap.h/.m` | `demo/DemoMap.java` | layer registry, tile sources, terrain, sky/light, camera |
| `DemoStyles.h/.m` | `demo/DemoStyles.java` | generated CartoCSS + the style decoders |
| `DemoPanel.h/.m` | `demo/DemoPanel.java` | the settings bottom sheet |
| `DemoSky.h/.m` | `demo/DemoSky.java` | day cycle: sun position + sky colours |
| `DemoCelestial.h/.m` | `demo/DemoCelestial.java` + `DemoStars.java` | sun, moon, arcs, star field |
| `DemoOrientation.h/.m` | `demo/DemoOrientation.java` | follow the device heading (CoreMotion) |
| `DemoTests.h/.m` | `demo/DemoTests.java` | one-shot actions (route, search, clear) |
| `DemoViewController.m` | `ui/main/SecondFragment.java` | platform glue only |

One deliberate difference: `DemoConfig.java` is ~230 static fields, each hand-mapped to an intent
extra. Here the values live in a dictionary keyed by the **same names**, so the override pass is
automatic and adding a knob is one line instead of four. `DemoPanel` is likewise table-driven
rather than a hand-built layout - the Java panel is 1200 lines mostly because every control is
written out.

## Adding a source file

`./regen.sh` — it strips the legacy elements CMake re-emits and re-runs XcodeGen. Needed because
`xcodegen generate` on its own fails after any CMake reconfigure, and fails *silently* in the
sense that the project keeps its old file list.

**A new file under `all/native` needs `./bootstrap.sh`, not `regen.sh`.** The SDK target's file
list comes from CMake's glob, so it only changes when CMake reconfigures; `regen.sh` regenerates
the Xcode project around the *existing* list. The symptom is a link error naming symbols from the
new file, after a build that looked like it should have compiled it. Android does not show this —
its glob is `CONFIGURE_DEPENDS` and Gradle reconfigures on its own.

## The settings sheet

A bottom sheet rather than a full-screen modal: the point of a knob is watching the map change as
you drag it, so the sheet opens at a medium, **undimmed** detent with the map live above it, and
expands to full height for the longer sections.

- **Search** filters on the label *and* the config key, so typing `zoom` finds "Zoom", "Satellite
  min zoom" and "Min zoom". A search flattens the accordion — a hit inside a closed section would
  otherwise look like no hit at all.
- **Collapsible sections**, all closed but the layer list.
- A control writes `DemoConfig` and calls the cheapest `DemoMap` apply that shows the change
  (layers / terrain / light / camera / options), declared per row.

## Status

Covered: vector and raster base maps (composite or plain), the generated inline CartoCSS with its
label / road-width / 3D-building / landcover knobs, the composite hillshade, satellite **and
contour** slots, the stand-alone hillshade layer, contours both on-the-fly and pre-baked, 3D
terrain, fog and view distance, sun/sky plus the day cycle, the camera, the settings sheet, and
the route / maneuver-arrow / GeoJSON-benchmark / vector-tile-search / clear actions.

Also covered: the hypsometric tint (a `CustomRasterTileLayer` filter shader over the raw DEM),
slope-angle bands, vector elements (marker / text / line / polygon), summit callout labels, the
peak-finder mode with its `flyTo` entry, the relief surface **and** the outline post-process
effect, AR over the live camera, free roam with a negative tilt range, device-heading following,
star sky, the scripted `anim` modes, the in-memory `param::` project style, the maneuver-head SVG
gallery, and the sun / moon / daily arcs / star catalogue on a `CelestialLayer`.

Every demo is now a port rather than an approximation: `DemoAstro` is the same low-precision
ephemeris the Android demo uses (Astronomical Almanac sun, abbreviated lunar series, JPL's
Keplerian elements for the planets), `DemoSky` places the sun with
`LightOptions.setSunPositionFromTime` and generates the same sky shader, and `DemoStarCatalogue`
carries the same ~190 stars and 30 constellation figures.

A **camera readout** sits along the bottom (`z=… tilt=… lat, lon`), the counterpart of the Android
layout's `zoomText`, and every move logs the same line the Android demo logs so a scripted run can
read the camera back out. Tapping the map reports the position and the terrain elevation under it.

### One structure, two platforms

`DemoMap` mirrors `DemoMap.java` method for method: a `DemoFeature` registry with a fixed
bottom-to-top `LAYER_ORDER`, lazily built layers cached across toggles, `isEnabled` /
`setEnabled` / `invalidate` / `rebuildLayers`, lazily created shared tile sources, and the same
`apply*` names the panel calls. The panel's sections and rows are the Java panel's, in the same
order. Both demos read the same key names, so a camera or a layer set can be pasted from one
command line to the other.

The one structural difference: `DemoConfig` is a dictionary rather than ~230 static fields (see
above), so `DemoConfig.boolFor:@"hillshade"` stands in for `DemoConfig.LAYER_HILLSHADE`.

## Composite slots are not layers

`contour`, `hs` and `sat` are **composite slots**, not layers: the source is woven into the master
style at the position of its `#name` rule. `contour` in particular is merged with
`addVectorDataSource` so the base style's `#contour` block draws it — `addExternalDataSource`
would give it its own pass and the style's contour rules would still get no data. The stand-alone
equivalents are the separate `contourLayer` / `hillshade` / `satellite` keys. Same split, same
defaults and same key names as the Android demo, where `contour` is on by default.

## Gotchas

- **A launch value starting with `-` is silently dropped.** UIKit folds `-key value` pairs into
  `NSUserDefaults`, and it reads `-tilt -45` as two keys, so the knob keeps its default and the
  demo runs at a camera you did not ask for. Quote it with a leading space — `-tilt " -45"` — or
  set it from the panel. The startup log line `MassifDemo: camera lon … tilt …` reports what the
  camera actually ended up at, which is the fastest way to catch this (and the terrain's zoom
  clamp).
- **Tilt 90 is straight down**, 0 is the horizon and negative looks above it. A panorama is around
  tilt 25, not 85; `-tilt 0` shows nothing but sky.
- **Screen sizes in the celestial API are pixels, not points.** The Android demo multiplies them by
  the display density and this one multiplies by `UIScreen.mainScreen.scale`; without it every star
  and figure line comes out a third of its intended size on a 3x phone.
- **Nothing repaints unless the renderer is asked.** Android's demo ends every apply with
  `mapView.requestRender()`; the iOS `MapView` has no such method, so `DemoMap.requestRender` calls
  `[[mapView getMapRenderer] requestRedraw]` in the same places. Adding a layer used to appear at
  once and removing one only on the next pan, which was `Layers::setAll` never requesting a redraw
  of its own (fixed in the SDK - it notified the surviving layers only).
- **Positions must go through the map's own projection.** `[[mapView getOptions] getBaseProjection]`
  is EPSG3857; converting with `MSFEPSG4326` instead compiles, looks right, and silently feeds
  lon/lat to the map as metres, which puts the camera in the ocean off 0,0.
- `bootstrap.sh` strips `PBXBuildStyle` from the CMake-generated project. CMake still emits those
  Xcode 2 vestiges and XcodeGen's parser refuses to read a project containing them.
- The app links `libc++` and MetalANGLE explicitly: its own sources are all Objective-C, so nothing
  would otherwise pull in the C++ runtime, and a project reference does not carry CMake's
  `target_link_libraries` through to the app.
- One architecture at a time — `bootstrap.sh` configures the SDK for a single arch and the app is
  pinned to match.
