#!/bin/sh
# Assemble the Massif style folder the Alpi Maps app runs in dev (app.webpack.config.js, non
# production): the @massif-maps/styles cartocss-iconfont package, overlaid by the app's own
# dev_assets/styles/massif project (alpimaps.json, rules, ranking, colours), plus the 3D models
# beside it. Read-only on the app; the result is a plain folder for `-style dir -styleDir <out>`.
#
#   ./assemble-app-style.sh [out-dir]        # default /private/tmp/massifdemo-style/massif
#   ALPIMAPS_DIR=/path/to/alpimaps ./assemble-app-style.sh
set -e
ALPIMAPS_DIR=${ALPIMAPS_DIR:-/Volumes/dev/nativescript/alpimaps}
OUT=${1:-/private/tmp/massifdemo-style/massif}
PKG="$ALPIMAPS_DIR/node_modules/@massif-maps/styles/cartocss-iconfont"

[ -d "$PKG" ] || { echo "no $PKG - run yarn in the app first" >&2; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT/models"
rsync -a --exclude 'osm*' --exclude 'custom*' --exclude 'fonts' --exclude 'iconfont' "$PKG/" "$OUT/"
rsync -a "$ALPIMAPS_DIR/dev_assets/styles/massif/" "$OUT/"
cp "$ALPIMAPS_DIR"/dev_assets/styles/massif-models/*.glb "$OUT/models/" 2>/dev/null || true
echo "style assembled in $OUT ($(ls "$OUT" | wc -l | tr -d ' ') entries)"
