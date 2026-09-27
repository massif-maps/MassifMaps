#!/bin/sh
# The 2D city frame: Grenoble against the Saint-Eynard ridge, terrain OFF, composite base with the
# bundled style project, panning north. The one 2D camera with headroom over the device's 43 Hz
# present ceiling (docs/internals/performance-log.md, 15.6).
# $1 = label, rest = extra extras.
ANDROID_SERIAL="${ANDROID_SERIAL:?set it to the device serial}"; export ANDROID_SERIAL
LABEL="$1"; shift
adb shell am force-stop com.massifmaps.MassifDemo >/dev/null 2>&1
adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
adb shell am start -n com.massifmaps.MassifDemo/.BenchActivity --es ui false \
  --es terrain false --es base composite --es style assets \
  --es lat 45.188 --es lon 5.724 --es zoom 16.22 --es tilt 26 \
  --es anim pan --es animDelay 40000 --es animDuration 25 --es animLatDelta 0.06 \
  "$@" >/dev/null 2>&1
i=0
while [ $i -lt 8 ]; do sleep 5; adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1; i=$((i+1)); done
adb logcat -c
i=0
while [ $i -lt 5 ]; do sleep 4; adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1; i=$((i+1)); done
adb logcat -d -s massif | grep -E "PROF|RenderStats" | sed "s/^/[$LABEL] /"
