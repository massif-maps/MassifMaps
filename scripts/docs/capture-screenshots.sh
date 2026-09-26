#!/usr/bin/env bash
# Capture a feature still (RECORD=1: plus a ~14s video) from the demo bench into img/features/.
# Usage: capture-screenshots.sh <name> --es lon <lon> --es lat <lat> --es zoom <z> --es tilt <t>
# The camera is only what the extras say (docs/contributing/demo-app.md); SETTLE=<s> sets the wait.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEV="$ROOT/scripts/android-dev"
OUT="$ROOT/website/static/img/features"
APP_ID="${APP_ID:-com.massifmaps.MassifDemo}"
NAME="${1:-feature}"
shift $(( $# > 0 ? 1 : 0 ))
mkdir -p "$OUT"

command -v adb >/dev/null || { echo "adb not found (install Android platform-tools)"; exit 1; }
adb get-state >/dev/null 2>&1 || { echo "No device/emulator. Boot one: emulator -avd <name>"; exit 1; }

echo "==> Building demo (assembleDebug, prebuilt native, offline)"
( cd "$DEV" && ./gradlew :app:assembleDebug -x lint --offline )
APK="$(find "$DEV/app/build/outputs/apk/debug" -name '*.apk' | head -1)"
[ -n "$APK" ] || { echo "APK not found"; exit 1; }

echo "==> Installing + launching"
adb install -r -g "$APK" >/dev/null
adb shell am force-stop "$APP_ID"
adb shell am start -n "$APP_ID/.BenchActivity" --es ui false "$@" >/dev/null
until [ -n "$(adb shell pidof "$APP_ID" 2>/dev/null)" ]; do sleep 1; done
echo "   waiting ${SETTLE:-75}s for the scene to settle…"; sleep "${SETTLE:-75}"

echo "==> Screenshot -> $OUT/$NAME.png"
adb exec-out screencap -p > "$OUT/$NAME.png"

if [ "${RECORD:-0}" = "1" ]; then
  echo "==> Recording ~14s video"
  adb shell screenrecord --bit-rate 8000000 --time-limit 14 /sdcard/${NAME}.mp4 &
  REC=$!; sleep 1
  # pan / drag gestures for some motion
  adb shell input swipe 540 1400 540 700 1600
  adb shell input swipe 300 1000 800 1000 1600
  adb shell input swipe 800 1100 300 900 1600
  adb shell input swipe 540 700 540 1400 1600
  wait $REC 2>/dev/null
  adb pull /sdcard/${NAME}.mp4 "$OUT/${NAME}.mp4"
fi

# Crop the Android status/app bars + nav bar and encode for web (needs ffmpeg).
# Android phone screenshots here are 1080x2400; adjust the crop for other devices.
if command -v ffmpeg >/dev/null; then
  echo "==> Cropping chrome + encoding (ffmpeg)"
  ffmpeg -y -loglevel error -i "$OUT/$NAME.png" \
    -vf "crop=1080:2055:0:195" -q:v 3 "$OUT/$NAME.jpg" && rm -f "$OUT/$NAME.png"
  if [ -f "$OUT/${NAME}.mp4" ]; then
    ffmpeg -y -loglevel error -i "$OUT/${NAME}.mp4" \
      -vf "crop=1080:2055:0:195,scale=640:-2" -c:v libx264 -pix_fmt yuv420p \
      -movflags +faststart -crf 27 -an "$OUT/${NAME}-web.mp4"
  fi
else
  echo "   (install ffmpeg to auto-crop the status/nav bars and encode the video)"
fi

echo "done. Review $OUT/ and point the feature doc's image/video at it."
