#!/bin/sh
# Cross-app instrument for the 2D city camera: SurfaceFlinger averageFPS of our SurfaceView layer.
# The only way to compare two BUILDS (PROF does not exist in a plain one) - see 11-tangram-diff.md.
# $1 = apk, $2 = label, rest = extra extras.
ANDROID_SERIAL="${ANDROID_SERIAL:?set it to the device serial}"; export ANDROID_SERIAL
APK="$1"; LABEL="$2"; shift 2
adb install -r -t "$APK" >/dev/null 2>&1
adb shell am force-stop com.massifmaps.MassifDemo >/dev/null 2>&1
adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
adb shell am start -n com.massifmaps.MassifDemo/.BenchActivity --es ui false \
  --es terrain false --es base composite --es style assets \
  --es lat 45.188 --es lon 5.724 --es zoom 16.22 --es tilt 26 \
  --es anim pan --es animDelay 40000 --es animDuration 25 --es animLatDelta 0.06 \
  "$@" >/dev/null 2>&1
i=0
while [ $i -lt 8 ]; do sleep 5; adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1; i=$((i+1)); done
adb shell dumpsys SurfaceFlinger --timestats -disable >/dev/null 2>&1
adb shell dumpsys SurfaceFlinger --timestats -clear >/dev/null 2>&1
adb shell dumpsys SurfaceFlinger --timestats -enable >/dev/null 2>&1
i=0
while [ $i -lt 5 ]; do sleep 4; adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1; i=$((i+1)); done
adb shell dumpsys SurfaceFlinger --timestats -dump --maxlayers 8 \
  | grep -A 6 "SurfaceView\[com.massifmaps.MassifDemo" | grep -E "layerName|totalFrames|averageFPS" \
  | sed "s/^/[$LABEL] /"
adb shell dumpsys SurfaceFlinger --timestats -disable >/dev/null 2>&1
