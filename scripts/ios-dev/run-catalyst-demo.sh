#!/bin/bash
# Build (incrementally) and run the MassifDemo as a native Mac Catalyst app, wait for the tiles to
# settle, and capture the map's own render in-process (no Screen Recording permission needed).
# See docs/internals/catalyst-demo-loop.md.
#
#   ./run-catalyst-demo.sh                          # Grenoble, 3D terrain + buildings, one shot after 60 s
#   SETTLE=90 SHOT=/tmp/a.png ./run-catalyst-demo.sh
#   BURST=60 STEP_DEG=1 ./run-catalyst-demo.sh      # 60 frames, bearing +1 deg per frame -> $SHOTDIR
#   ./run-catalyst-demo.sh -tilt 45 -zoom 18        # anything after the script is a launch argument
#
# Env: SETTLE (s, 60) SHOT SHOTDIR BURST (frames) INTERVAL_MS (80) STEP_DEG ROTATE_DEG_PER_SEC DURATION
#      TILTS=90,75,60,45,30,15 (one still per tilt into $SHOTDIR/tilt_NN.png, SDK convention: 90 = straight down)
#      SDK_DIR (checkout holding build/ and generated/, default: the main checkout of this repo)
#      ALPIMAPS_DIR (style + fonts of the app), DATA_DIR (europe.pmtiles + europe_terrain.pmtiles)
#      ARROWS=1 (draw the maneuver arrows of routes/grenoble-verdun.json with the app's inner style)
#      OFFLINE=0 (use the online sources instead of the pmtiles), APPLIKE=0 (the demo's own defaults
#      instead of the app's terrain/light/blending settings), NOBUILD=1, KEEP=1 (do not quit after)
set -e
cd "$(dirname "$0")"

SDK_DIR=${SDK_DIR:-$(cd "$(git rev-parse --git-common-dir)/.." && pwd)}
ALPIMAPS_DIR=${ALPIMAPS_DIR:-/Volumes/dev/nativescript/alpimaps}
DATA_DIR=${DATA_DIR:-/Volumes/dev/carto/cairn/alpimaps_mbtiles/europe}
DD=${DD:-/private/tmp/massifdemo-dd}
STYLE_DIR=${STYLE_DIR:-/private/tmp/massifdemo-style/massif}
SETTLE=${SETTLE:-60}
SHOTDIR=${SHOTDIR:-/tmp/massifdemo-shots/seq}
APP="$DD/Build/Products/Debug-maccatalyst/MassifDemo.app"
PIDFILE=/tmp/massifdemo.pid

if [ ! -f "$SDK_DIR/build/ios_metal-MACCATALYST-arm64/Release/libmassif.a" ]; then
  echo "no Catalyst lib in $SDK_DIR - run build-plugin-catalyst.sh there first (or set SDK_DIR)" >&2
  exit 1
fi

if [ -z "$NOBUILD" ]; then
  # sources are globbed at generation time, so regenerate when one was added
  if [ ! -d MassifDemoCatalyst.xcodeproj ] || [ -n "$(find MassifDemo project-catalyst.yml -newer MassifDemoCatalyst.xcodeproj/project.pbxproj \( -name '*.m' -o -name '*.h' -o -name 'project-catalyst.yml' \) | head -1)" ]; then
    xcodegen generate --spec project-catalyst.yml >/dev/null
  fi
  xcodebuild -project MassifDemoCatalyst.xcodeproj -scheme MassifDemo \
    -destination 'platform=macOS,variant=Mac Catalyst' -derivedDataPath "$DD" \
    MASSIF_SDK_DIR="$SDK_DIR" build >"$DD.log" 2>&1 || { grep 'error:' "$DD.log" | sort -u; echo "build failed, see $DD.log" >&2; exit 1; }
fi

[ -d "$STYLE_DIR" ] || ALPIMAPS_DIR="$ALPIMAPS_DIR" ./assemble-app-style.sh "$STYLE_DIR"

# only the instance this script started
if [ -f "$PIDFILE" ]; then kill "$(cat "$PIDFILE")" 2>/dev/null || true; rm -f "$PIDFILE"; fi

ARGS=(-bench true -ui false
  -lon 5.7245 -lat 45.1885 -zoom 17.5 -tilt 60 -rotation 0
  -terrain true -bld3d true -labels true -hs true
  -style dir -styleDir "$STYLE_DIR" -styleName alpimaps
  -fontFiles "$ALPIMAPS_DIR/app/fonts/osm.ttf:$ALPIMAPS_DIR/app/fonts/MassifIconsOsm.ttf")
if [ "$OFFLINE" != 0 ]; then
  ARGS+=(-vectorFile "$DATA_DIR/europe.pmtiles" -demFile "$DATA_DIR/europe_terrain.pmtiles" -demEncoding terrarium)
fi
if [ "$APPLIKE" != 0 ]; then
  # what the Alpi Maps app sets on Catalyst (terrain3d.ts, terrainStore.ts, CustomLayersModule.ts)
  ARGS+=(-layerBlendingSpeed 3 -labelBlendingSpeed 3 -labelsReverse true -baseCacheMB 256
    -occlusionTolerance 0.2 -meshResolution 256 -clearance 20 -drapeResolution 1024 -drapeLines true -noDrape '^contour|maneuver.*'
    -terrainLight true -sunAzimuth 330 -sunAltitude 60 -shadow 0.3 -shadowMapSize 2048 -shadowCascades 2
    -fog true)
fi
if [ -n "$ARROWS" ]; then
  # the app's navigation arrows (inner style) along a route cut from the local tiles
  ARGS+=(-maneuvers true -routeFile "${ROUTE_FILE:-$PWD/routes/grenoble-verdun.json}"
    -maneuverStyleDir "$ALPIMAPS_DIR/dev_assets/styles/inner" -maneuverStyleName voyager)
fi
ARGS+=(-shotDelay "$SETTLE" -shotDir "$SHOTDIR")
if [ -n "$BURST" ]; then
  ARGS+=(-shotSequence "$BURST" -shotIntervalMs "${INTERVAL_MS:-80}")
  [ -n "$STEP_DEG" ] && ARGS+=(-rotateStepDeg "$STEP_DEG")
else
  ARGS+=(-shot "${SHOT:-/tmp/massifdemo-shots/shot.png}")
fi
[ -n "$TILTS" ] && ARGS+=(-shotTilts "$TILTS" -tiltMin 0)
[ -n "$ROTATE_DEG_PER_SEC" ] && ARGS+=(-rotateDegPerSec "$ROTATE_DEG_PER_SEC")
[ -n "$DURATION" ] && ARGS+=(-duration "$DURATION")
[ -n "$KEEP" ] && ARGS+=(-quitAfterShot false)

mkdir -p /tmp/massifdemo-shots
LOG=/tmp/massifdemo-shots/run.log
"$APP/Contents/MacOS/MassifDemo" "${ARGS[@]}" "$@" >"$LOG" 2>&1 &
PID=$!
echo "$PID" >"$PIDFILE"
echo "MassifDemo pid $PID, settling ${SETTLE}s, log $LOG"

[ -n "$KEEP" ] && exit 0
LIMIT=$((SETTLE + ${BURST:-1} * 3 + ${DURATION:-0} + 120))
for _ in $(seq "$LIMIT"); do
  kill -0 "$PID" 2>/dev/null || break
  sleep 1
done
if kill -0 "$PID" 2>/dev/null; then echo "timed out, killing $PID" >&2; kill "$PID"; fi
rm -f "$PIDFILE"
grep 'DemoCapture' "$LOG" | tail -3
