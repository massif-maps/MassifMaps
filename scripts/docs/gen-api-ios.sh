#!/usr/bin/env bash
# Generate the iOS (Objective-C) API reference (Jazzy) into website/static/api/ios.
#
# Steps: run the SWIG Objective-C proxy generator to emit the public MSF* headers,
# then run `jazzy --objc` over the umbrella header. macOS host only.
#
# Prerequisites (macOS):
#   - The SWIG fork executable. Point to it with $SWIG (default: `swig` on PATH).
#   - jazzy:   gem install jazzy   (or `bundle`)
#   - sourcekitten (pulled in by jazzy)
#   - Submodules checked out (libs-massif, libs-external).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCRIPTS="$ROOT/scripts"
SWIG="${SWIG:-swig}"
PROFILE="${SWIG_PROFILE:-standard+valhalla+geocoding+routing+packagemanager+gdal+nmlmodellodtree}"
BUILD="$ROOT/build/docs-api/ios"
PROXY="$BUILD/proxies"
OUT="$ROOT/website/static/api/ios"

command -v "$SWIG" >/dev/null || { echo "SWIG not found ($SWIG). Set \$SWIG to the SWIG fork."; exit 1; }
command -v jazzy >/dev/null || { echo "jazzy not found. Run: gem install jazzy"; exit 1; }

rm -rf "$BUILD" && mkdir -p "$PROXY"
echo "==> Generating Objective-C proxies (profile: $PROFILE)"
# swigpp-objc.py resolves paths relative to scripts/; run it from there.
( cd "$SCRIPTS" && python3 swigpp-objc.py \
    --profile "$PROFILE" \
    --swig "$SWIG" \
    --proxydir "$PROXY" \
    --wrapperdir "$BUILD/wrappers" \
    --moduledir "$BUILD/modules" )

# Build an umbrella header that imports every generated public header.
UMBRELLA="$BUILD/MassifMaps.h"
: > "$UMBRELLA"
# MSFPolymorphicClasses.h is generated plumbing (imports + a static init), not API.
find "$PROXY" -name 'MSF*.h' ! -name 'MSFPolymorphicClasses.h' | sort | while read -r h; do
  echo "#import \"$(basename "$h")\"" >> "$UMBRELLA"
done
HCOUNT=$(grep -c '#import' "$UMBRELLA" || true)
echo "   $HCOUNT public headers"
[ "$HCOUNT" -gt 0 ] || { echo "No MSF*.h generated — check the SWIG run above."; exit 1; }

echo "==> Running jazzy"
rm -rf "$OUT" && mkdir -p "$OUT"
jazzy \
  --objc \
  --umbrella-header "$UMBRELLA" \
  --framework-root "$PROXY" \
  --module MassifMaps \
  --module-version "${DOCS_VERSION:-master}" \
  --title "Massif Maps — iOS API" \
  --output "$OUT" \
  || echo "   (jazzy reported warnings — output still generated)"

echo "==> iOS API reference at $OUT"
