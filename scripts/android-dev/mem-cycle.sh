#!/bin/bash
# Opens a demo screen, waits, presses BACK, and prints `dumpsys meminfo` after each step, N times.
#   ./mem-cycle.sh 5 25 .BenchActivity --es ui false
#   ./mem-cycle.sh 5 25 .ExampleActivity --es example terrain-3d
# Columns are kB: nativeAlloc is the live malloc bytes (the number that must come back down after
# close); activities/views are the retained instances. Set ANDROID_SERIAL when several devices are attached.
ADB=${ADB:-$HOME/Library/Android/sdk/platform-tools/adb}
PKG=com.massifmaps.MassifDemo
CYCLES=$1
SETTLE=$2
shift 2

sample() {
  local dump
  dump=$($ADB shell dumpsys meminfo $PKG)
  echo "$1 javaPSS=$(echo "$dump" | awk '/^ *Java Heap:/{print $3; exit}')" \
    "nativeAlloc=$(echo "$dump" | awk '/^ *Native Heap /{print $9; exit}')" \
    "totalPSS=$(echo "$dump" | awk '/^ *TOTAL PSS:/{print $3; exit}')" \
    "activities=$(echo "$dump" | awk '/Activities:/{print $NF; exit}')" \
    "views=$(echo "$dump" | awk '/ Views:/{print $2; exit}')" \
    "viewRoots=$(echo "$dump" | awk '/ Views:/{print $4; exit}')"
}

for i in $(seq 1 "$CYCLES"); do
  $ADB shell am start -n $PKG/"$@" > /dev/null
  sleep "$SETTLE"
  sample "open$i "
  $ADB shell input keyevent KEYCODE_BACK
  sleep 4
  $ADB shell am send-trim-memory $PKG RUNNING_CRITICAL > /dev/null 2>&1
  sleep 2
  sample "close$i"
done
