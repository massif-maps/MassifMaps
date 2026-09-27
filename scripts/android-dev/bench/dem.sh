#!/bin/sh
# What the DEM pipeline costs to bring a camera's elevation in, per TILE rather than per second.
# The RenderStats 'dem' lines are per-interval sums, so they say nothing on their own - divide them
# by the encodes in the same interval, which is what bench/demsum.py does.
#
# The whole session is captured, load included: the encodes happen while the cover fills, and a
# window that starts after the map has settled contains almost none of them.
# $1 = apk, $2 = label, rest = extra extras.
ANDROID_SERIAL="${ANDROID_SERIAL:?set it to the device serial}"; export ANDROID_SERIAL
APK="$1"; LABEL="$2"; shift 2
RUN=${RUN:-40}
adb install -r -t "$APK" >/dev/null 2>&1
adb shell am force-stop com.massifmaps.MassifDemo >/dev/null 2>&1
adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
adb logcat -c
adb shell am start -n com.massifmaps.MassifDemo/.BenchActivity --es ui false \
  --es base composite --es hs true --es contour true \
  --es terrain true --es drape true \
  --es lat 45.244172 --es lon 5.760595 --es zoom 14 --es tilt 20 --es rotation 0 \
  --es anim rotate --es animRotation 180 --es animDuration 6 --es animDelay 25000 \
  "$@" >/dev/null 2>&1
i=0
while [ $i -lt $RUN ]; do sleep 5; adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1; i=$((i+5)); done
adb logcat -d -s massif | grep -E "RenderStats: dem encodes|RenderStats: demEncode texture|RenderStats: demNode edge" | sed "s|^|[$LABEL] |"
