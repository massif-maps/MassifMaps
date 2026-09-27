#!/bin/sh
# Fast ROTATION at a 3D terrain camera - the case where the drape cover is renamed fastest.
# $1 = label, rest = extra extras (e.g. --es drapeResolution 1024 --es drapeCacheSize 192).
#
# The rotation itself is '--es anim rotate': setMapRotation(animRotation, animDuration), so the
# useful window is animDuration seconds long. Everything before it is tile load and is discarded by
# clearing logcat after the settle.
ANDROID_SERIAL="${ANDROID_SERIAL:?set it to the device serial}"; export ANDROID_SERIAL
LABEL="$1"; shift
SETTLE=${SETTLE:-30}
SPIN=${SPIN:-4}
adb shell am force-stop com.massifmaps.MassifDemo >/dev/null 2>&1
adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
adb shell am start -n com.massifmaps.MassifDemo/.BenchActivity --es ui false \
  --es base composite --es hs true --es contour true \
  --es terrain true --es drape true \
  --es lat 45.244172 --es lon 5.760595 --es zoom 14 --es tilt 20 --es rotation 0 \
  --es anim rotate --es animRotation 180 --es animDuration "$SPIN" \
  --es animDelay "${SETTLE}000" \
  "$@" >/dev/null 2>&1
i=0
while [ $i -lt $SETTLE ]; do sleep 5; adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1; i=$((i+5)); done
adb logcat -c
sleep $((SPIN + 1))
adb logcat -d -s massif | grep "PROF: " | sed "s/^/[$LABEL] /"
echo "--- [$LABEL] drape cache:"
adb logcat -d -s massif | grep "drapeCache" | tail -2
