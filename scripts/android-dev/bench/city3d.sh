#!/bin/sh
# Buildings + cast shadows over 3D terrain, at the Grenoble city camera, rotating.
# '--es anim rotate' is what makes layers3D/shadowCast measurable: the sections swing with the
# building count on screen, so a static camera says nothing (performance-log section 20).
# '--es appSun true' is NOT optional: without it the style states its own sun, the app's azimuth and
# altitude are ignored, and at the style's sun there is no visible cast shadow to measure at all.
# The camera is backed off to z16.3 on purpose - at 17.7 it sits between two facades, so the frame is
# two walls of fill and says nothing about a city of buildings.
# The ambient/sun balance is part of the configuration, not decoration: resolveLighting multiplies
# the requested shadow strength by the DIRECT light's share of the total (StyleEnvironment.cpp), so
# against the demo's default ambient a requested 1.0 arrives as 0.30 and there is nothing to see.
# At ambient 0.35 / sunIntensity 1.0 it arrives as 0.62. Check the 'shadows ACTIVE (strength ...)'
# line before believing a shadow measurement.
# $1 = label, rest = extra extras (e.g. --es shadow 0 --es meshResolution 64).
ANDROID_SERIAL="${ANDROID_SERIAL:?set it to the device serial}"; export ANDROID_SERIAL
LABEL="$1"; shift
SETTLE=${SETTLE:-30}
SPIN=${SPIN:-8}
adb shell am force-stop com.massifmaps.MassifDemo >/dev/null 2>&1
adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
adb shell am start -n com.massifmaps.MassifDemo/.BenchActivity --es ui false \
  --es base composite --es hs true --es contour true \
  --es terrain true --es drape true --es bld3d true --es terrainLight true \
  --es appSun true --es sunAzimuth 315 --es sunAltitude 35 \
  --es ambient 0.35 --es sunIntensity 1.0 \
  --es lat 45.190814 --es lon 5.724807 --es zoom 16.3 --es tilt 50 --es rotation 0 \
  --es anim rotate --es animRotation 180 --es animDuration "$SPIN" \
  --es animDelay "${SETTLE}000" \
  "$@" >/dev/null 2>&1
i=0
while [ $i -lt $SETTLE ]; do sleep 5; adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1; i=$((i+5)); done
adb logcat -c
sleep $((SPIN + 1))
adb logcat -d -s massif | grep -E "PROF: |PROF GPU: " | sed "s|^|[$LABEL] |"
