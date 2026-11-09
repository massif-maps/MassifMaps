#!/bin/sh
# Far tile LOD at a tilt: the Massif style over the composite base, terrain on, rotating at the Grenoble camera.
# $1 = label, $2 = tilt (SDK: 90 is straight down), rest = extra extras (--es farLodVector distance ...).
# Build with -PprofileRender. Read the output with fartiltsum.py.
ANDROID_SERIAL="${ANDROID_SERIAL:?set it to the device serial}"; export ANDROID_SERIAL
LABEL="$1"; TILT="$2"; shift 2
SETTLE=${SETTLE:-40}
SPIN=${SPIN:-16}
BENCH_BASE="--es base composite --es style assetzip --es styleAssetZip massif --es vectorFile rhones_alpes/rhone-alpes.pmtiles --es buildings 2"
adb shell am force-stop com.massifmaps.MassifDemo >/dev/null 2>&1
adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
adb shell am start -n com.massifmaps.MassifDemo/.BenchActivity --es ui false \
  $BENCH_BASE --es hs true --es contour true --es terrain true --es drape true \
  --es lat 45.190814 --es lon 5.724807 --es zoom 15.5 --es tilt "$TILT" --es rotation 0 \
  --es anim rotate --es animRotation 180 --es animDuration "$SPIN" \
  --es animDelay "${SETTLE}000" \
  "$@" >/dev/null 2>&1
i=0
while [ $i -lt $SETTLE ]; do sleep 5; adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1; i=$((i+5)); done
adb logcat -c; adb logcat -d -t 1 >/dev/null
i=0
while [ $i -lt $((SPIN + 8)) ]; do sleep 2; adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1; i=$((i+2)); done
adb logcat -d -s massif | grep -E "PROF: |RenderStats: " | sed "s|^|[$LABEL] |"
