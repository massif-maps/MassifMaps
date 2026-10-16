---
title: Mac Catalyst demo loop
description: Reproduce a rendering bug in seconds with the iOS demo running as a native Mac app, captured in-process.
---

# The Mac Catalyst demo loop

The iOS demo (`scripts/ios-dev/MassifDemo`) also builds as a native **Mac Catalyst** app. It links the
`libmassif.a` that `scripts/ios-dev/build-plugin-catalyst.sh` already built (MetalANGLE merged in), takes
every knob as a launch argument, and writes screenshots from inside the process: no simulator, no
NativeScript, no Screen Recording permission. Use it to reproduce an SDK rendering bug against the same
data and style as the Alpi Maps app.

```sh
cd scripts/ios-dev
./run-catalyst-demo.sh                                     # Grenoble, 3D terrain + buildings, 1 shot after 60 s
SETTLE=40 BURST=90 STEP_DEG=0.15 SHOTDIR=/tmp/massifdemo-shots/b ./run-catalyst-demo.sh -winW 800 -winH 600
python3 -I analyze-label-flicker.py /tmp/massifdemo-shots/b --skip 12
```

## How it is built

`project-catalyst.yml` is an XcodeGen spec (the project is generated and gitignored, like `project.yml`'s).
It differs from the simulator project in three ways:

- the SDK is **not** a project dependency: `OTHER_LDFLAGS` names
  `$(MASSIF_SDK_DIR)/build/ios_metal-MACCATALYST-arm64/Release/libmassif.a`, and the headers come from
  `$(MASSIF_SDK_DIR)/generated/ios-objc/proxies` + `ios/objc` + `libs-external/angle-metal/include`.
  `MASSIF_SDK_DIR` is the Xcode build setting; the script sets it from its `SDK_DIR` variable (default: the
  MAIN checkout of the repo, so its lib is linked even when the script runs from a worktree). Set
  `SDK_DIR=<worktree>` to run against the lib that worktree built. The lib is never rebuilt here: after a
  C++ change run `build-plugin-catalyst.sh` in that checkout, then rerun the demo (Xcode relinks).
- `SUPPORTS_MACCATALYST=YES`, `TARGETED_DEVICE_FAMILY=1,2` (plain Catalyst, "scale to match iPad", not
  "Optimize for Mac"), ad-hoc signing (`CODE_SIGN_IDENTITY=-`, no team).
- no `OpenGLES.framework` (MetalANGLE provides MGLKit); `IOSurface` and `CoreVideo` are added.

No Catalyst-specific source guard was needed except the window-size one in `AppDelegate.m`.

```sh
xcodegen generate --spec project-catalyst.yml            # run-catalyst-demo.sh does it when sources changed
xcodebuild -project MassifDemoCatalyst.xcodeproj -scheme MassifDemo \
  -destination 'platform=macOS,variant=Mac Catalyst' -derivedDataPath /private/tmp/massifdemo-dd \
  MASSIF_SDK_DIR=/Volumes/dev/carto/mobile-sdk build
```

The binary is `/private/tmp/massifdemo-dd/Build/Products/Debug-maccatalyst/MassifDemo.app/Contents/MacOS/MassifDemo`;
`open -n ... --args -key value` and direct exec both deliver the arguments.

## Same data and style as the app

| What | Argument | Default of the script |
|---|---|---|
| vector tiles | `-vectorFile <x.pmtiles>` (`PMTilesTileDataSource`) | `$DATA_DIR/europe.pmtiles` |
| DEM (terrain, hillshade, contours) | `-demFile <x.pmtiles> -demEncoding terrarium` | `$DATA_DIR/europe_terrain.pmtiles` |
| style | `-style dir -styleDir <folder> -styleName alpimaps` | `assemble-app-style.sh` output |
| fallback icon fonts | `-fontFiles a.ttf:b.ttf` | the app's `osm.ttf` + `MassifIconsOsm.ttf` |
| style parameters | `-styleParams buildings=2,variant=streets`; `-bld3d true` = `buildings=2` | |

`DATA_DIR` is `/Volumes/dev/carto/cairn/alpimaps_mbtiles/europe`, `ALPIMAPS_DIR` is
`/Volumes/dev/nativescript/alpimaps`; both are read, never copied. `assemble-app-style.sh` builds the style
folder the app uses in dev (`app.webpack.config.js`): the `@massif-maps/styles` `cartocss-iconfont` package,
overlaid by `dev_assets/styles/massif` (`alpimaps.json` and its rules/ranking/colours), plus the `.glb` models
under `models/`. `OFFLINE=0` switches back to the demo's online sources.

`APPLIKE` (on unless `APPLIKE=0`) also sets what the app sets on Catalyst: `layerBlendingSpeed 3`,
`labelBlendingSpeed 3`, `labelsReverse` (`layersLabelsProcessedInReverseOrder`), 256 MB tile cache,
billboard occlusion tolerance 0.2, mesh resolution 256, camera clearance 20, drape 1024 + drape lines +
`noDrape '^contour|maneuver.*'`, terrain lighting (sun 330/60, shadow 0.3, 2048 map, 2 cascades), fog.
Without them the demo uses its own defaults (note: `baseCacheMB` had no default and meant a 0-byte tile cache).

## Arguments

Camera and layers are the demo's usual keys (`-lon -lat -zoom -tilt -rotation -terrain -hs -bld3d ...`,
`DemoConfig.m`). `-bench true -ui false` opens the bench map without chrome. A value starting with `-` needs
a leading space. Added for this loop (`DemoCapture.m`):

| Argument | Effect |
|---|---|
| `-shot <png>` | one frame of the map's own render (`MapRenderer.captureRendering`), then quit (`-quitAfterShot false` stays) |
| `-shotDelay <s>` | seconds after launch before the first frame: **tile settle time, 60-90 s** for buildings (script default 60) |
| `-shotSequence N -shotIntervalMs 80 -shotDir <dir>` | a burst: `frame_NNNN.png` + `frames.csv` (frame, ms, rotation, zoom, tilt). PNG encoding is off the render thread; real pace is ~130 ms/frame at 800x600 pt |
| `-rotateStepDeg d` | bearing += d per burst frame (deterministic) |
| `-rotateDegPerSec v -duration s` | bearing turns continuously (display-link driven) for `s` seconds; quits after unless a burst runs |
| `-shotTilts 90,75,60,45,30,15` | one still per tilt into `<shotDir>/tilt_NN.png` (SDK convention: 90 straight down, 0 horizon); `-tiltMin 0` lifts the 30 clamp |
| `-winW 800 -winH 600` | Catalyst window size in points (frames are 2x): a smaller window renders about 2.5x faster |

Parallel runs must not share state: `DD` (derived data, `/private/tmp/massifdemo-dd`), `PIDFILE` (`$DD.pid`) and
`LOG` (`$SHOTDIR/run.log`) are overridable, give each its own.

Script variables: `SDK_DIR`, `SETTLE`, `SHOT`, `SHOTDIR`, `BURST`, `INTERVAL_MS`, `STEP_DEG`, `ROTATE_DEG_PER_SEC`,
`DURATION`, `TILTS`, `ARROWS`, `OFFLINE`, `APPLIKE`, `NOBUILD`, `KEEP`, `SDK_DIR`. The script only ever
kills the pid it started (`/tmp/massifdemo.pid`). Log: `$SHOTDIR/run.log`. The SDK itself logs
nothing to stdout/stderr here, only the demo's own lines (`DemoCapture`, camera).

`screencapture -x` of the whole screen came out black here (no Screen Recording grant); that is why the
capture is in-process.

## Label blink test

Labels blinking while the camera moves is measured, not eyeballed:

```sh
SETTLE=40 BURST=90 STEP_DEG=0.15 SHOTDIR=/tmp/massifdemo-shots/b ./run-catalyst-demo.sh -winW 800 -winH 600
SETTLE=40 BURST=60 SHOTDIR=/tmp/massifdemo-shots/still ./run-catalyst-demo.sh -winW 800 -winH 600   # control
python3 -I analyze-label-flicker.py /tmp/massifdemo-shots/b --skip 12
```

`analyze-label-flicker.py` (PIL only) turns each frame into a "label ink" map (pixels standing out from a
blurred copy, minus the red paths and green trees), compares consecutive frames with a +-3 px search
window, and reports a **dip** (ink lost, back within `--window` frames at the same place) or a **flash**
(the reverse). Keep the step small (0.1-0.25 deg): a fast turn moves labels further than the window and
everything looks like a blink. The still-camera control must report 0. `blink_NN.png` shows the frames
around the biggest events, side by side. `--skip` drops the first frames, where the first camera move
re-places every label.

## Maneuver arrows

`ARROWS=1` (or `-maneuvers true -routeFile routes/grenoble-verdun.json -maneuverStyleDir
<alpimaps>/dev_assets/styles/inner -maneuverStyleName voyager`) draws a 417 m route with four turns, cut
from the local tiles by `make-grenoble-route.py` (no routing engine), as the app draws navigation: the
`navigation` line and one `maneuver` arrow per turn, styled by the app's `inner` project
(`shared/navigation.mss`), on the same GeoJSON source. Tilt stills:

```sh
ARROWS=1 SETTLE=35 TILTS=90,75,60,45,30,15 SHOTDIR=/tmp/massifdemo-shots/arrows_tilts \
  ./run-catalyst-demo.sh -winW 800 -winH 600 -lon 5.7265 -lat 45.1880 -zoom 17.6 -tilt 90
```

`-routeSeparate true` draws the route in a layer of its own under the arrows, as the app does (two tile
layers, two renderers); in one source the route covers the arrow shafts. `-itemsLine "lon,lat;lon,lat;..."`
adds a line of the app's saved-items style (`items`, with its `back/` attachment) beside it, to look at
`line-occlusion-opacity` on named attachments across 3D buildings. The arrows are sized in metres
(`-maneuverBefore/-After`): the app asks for 36 dp and about 58 dp, which is about 10 and 17 m at zoom 18.6 and
3.5 pt per metre there (halve the pt per metre per zoom level down).
