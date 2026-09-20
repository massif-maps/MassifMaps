#!/bin/sh
# Back-to-back A/B runner. $1 = label, rest = extra intent extras.
# Prints raw PROF lines prefixed with the label so a summarizer can group them.
ANDROID_SERIAL="${ANDROID_SERIAL:?set it to the device serial}"; export ANDROID_SERIAL
LABEL="$1"; shift
adb shell am force-stop com.massifmaps.MassifDemo >/dev/null 2>&1
adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
adb shell am start -n com.massifmaps.MassifDemo/.BenchActivity --es ui false \
  --es base plain --es style inline --es hs false --es contour false --es elements false --es labels false \
  --es anim pan --es animDelay 40000 --es animDuration 25 --es animLonDelta 0.05 \
  "$@" >/dev/null 2>&1
i=0
while [ $i -lt 8 ]; do sleep 5; adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1; i=$((i+1)); done
adb logcat -c
sleep 22
adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
adb logcat -d -s massif | grep "PROF: " | sed "s/^/[$LABEL] /"
