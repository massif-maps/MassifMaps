#!/bin/sh
# Pan NORTH into the mountains from the demo camera - Martin's slow case.
# $1 = label, rest = extra extras.
ANDROID_SERIAL="${ANDROID_SERIAL:?set it to the device serial}"; export ANDROID_SERIAL
LABEL="$1"; shift
adb shell am force-stop com.massifmaps.MassifDemo >/dev/null 2>&1
adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
adb shell am start -n com.massifmaps.MassifDemo/.BenchActivity --es ui false \
  --es contour true --es hs true \
  --es anim pan --es animDelay 40000 --es animDuration 25 --es animLonDelta 0 --es animLatDelta 0.06 \
  "$@" >/dev/null 2>&1
i=0
while [ $i -lt 8 ]; do sleep 5; adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1; i=$((i+1)); done
adb logcat -c
sleep 22
adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
adb logcat -d -s massif | grep "PROF: " | sed "s/^/[$LABEL] /"
echo "--- elevation texture churn:"
adb logcat -d -s massif | grep "PROBE elevation" | tail -4
echo "--- tiles:"
adb logcat -d -s massif | grep "geomDraws=" | tail -2
