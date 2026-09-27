#!/bin/sh
# Launch -> first drawn tile geometry, for a given style source. $1 = style
ANDROID_SERIAL="${ANDROID_SERIAL:?set it to the device serial}"; export ANDROID_SERIAL
S="$1"
adb shell am force-stop com.massifmaps.MassifDemo >/dev/null 2>&1
adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
adb logcat -c
adb shell am start -n com.massifmaps.MassifDemo/.BenchActivity --es ui false \
  --es style "$S" --es contour false --es hs false --es elements false >/dev/null 2>&1
sleep 70
START=$(adb logcat -d -v time -s massif | grep "AttachJVM" | head -1 | awk '{print $2}')
FIRSTTILE=$(adb logcat -d -v time -s massif | grep "loadTile" | head -1 | awk '{print $2}')
FIRSTDRAW=$(adb logcat -d -v time -s massif | grep -E "geomDraws=[1-9]" | head -1 | awk '{print $2}')
echo "style=$S start=$START firstTileRequest=$FIRSTTILE firstDraw=$FIRSTDRAW"
