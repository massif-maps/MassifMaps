#!/bin/sh
# Mac Catalyst counterpart of android's `DEBUGMASSIF=1`: build the SDK from source and put it in a
# local MassifMaps.xcframework (build/spm-local) that the ui-massifmaps plugin links when it is
# pointed at it. Touch a .cpp/.mm, run this (or let the app's before-prepareNativeApp hook run it),
# and only that file recompiles.
#
#   ./build-plugin-catalyst.sh                      # arm64, Release (no LTO on Catalyst, -O2)
#   CONFIGURATION=RelWithDebInfo ./build-plugin-catalyst.sh
#   PROFILE=standard ./build-plugin-catalyst.sh
#
# The first run configures and builds the whole SDK through build-ios.py (long). Every run after
# that is an xcodebuild of the same project, ~5-15 s when one file changed.
#
# Headers are copied from dist/ios_metal once. Change an all/modules/*.i (or add a public
# header) and the generated bindings and headers are stale: rebuild dist with build-ios.py and
# delete build/spm-local/MassifMaps.xcframework so it is copied again.
set -e

cd "$(dirname "$0")"
BASE_DIR=$(cd ../.. && pwd)
SWIG=${SWIG:-/Volumes/dev/carto/mobile-swig/swig}
# Must match the profile of the headers in dist/ios_metal, or they describe an API this lib does not export.
PROFILE=${PROFILE:-standard+valhalla+geocoding+routing+packagemanager}
CONFIGURATION=${CONFIGURATION:-Release}
ARCH=arm64
BUILD_DIR="$BASE_DIR/build/ios_metal-MACCATALYST-$ARCH"
DIST_XCFRAMEWORK="$BASE_DIR/dist/ios_metal/MassifMaps.xcframework"
LOCAL_XCFRAMEWORK="$BASE_DIR/build/spm-local/MassifMaps.xcframework"

if [ ! -d "$BASE_DIR/generated/ios-objc/proxies" ]; then
  if [ ! -x "$SWIG" ]; then
    echo "SWIG executable not found at $SWIG - set SWIG=/path/to/mobile-swig/swig" >&2
    exit 1
  fi
  echo "==> Generating Objective-C bindings (profile: $PROFILE)"
  (cd .. && python3 swigpp-objc.py --profile "$PROFILE" --swig "$SWIG")
fi

# Configure (and build everything once) with the release script, so the project carries exactly the
# flags a shipped Catalyst slice has - including the macabi target fix-up it applies to the pbxproj.
if [ ! -f "$BUILD_DIR/massif.xcodeproj/project.pbxproj" ]; then
  echo "==> First run: configuring and building the whole SDK ($CONFIGURATION), this takes a while"
  (cd .. && python3 build-ios.py --profile "$PROFILE" --ios-arch arm64-maccatalyst \
    --configuration "$CONFIGURATION" --use-metalangle)
fi

# A CMake reconfigure regenerates the pbxproj and loses the fix build-ios.py applies to it.
PBXPROJ="$BUILD_DIR/massif.xcodeproj/project.pbxproj"
if grep -q -e '-apple-ios-13.0-macabi' "$PBXPROJ"; then
  sed -i '' 's/-apple-ios-13.0-macabi/-apple-ios13.1-macabi/g' "$PBXPROJ"
fi

echo "==> Building libmassif.a ($CONFIGURATION)"
# Quiet on success, but keep the log: a hook that swallows the compiler's output leaves you
# staring at a stale lib with no idea why.
BUILD_LOG="$BUILD_DIR/build.log"
if ! (cd "$BUILD_DIR" && xcodebuild -project massif.xcodeproj -arch "$ARCH" \
  -configuration "$CONFIGURATION" build \
  ENABLE_BITCODE=NO \
  GCC_PREPROCESSOR_DEFINITIONS='$(inherited) _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION') >"$BUILD_LOG" 2>&1; then
  echo "Build failed - errors from $BUILD_LOG:" >&2
  grep -E "error:|BUILD FAILED" "$BUILD_LOG" | tail -20 >&2
  exit 1
fi

BUILT="$BUILD_DIR/$CONFIGURATION/libmassif.a"
if [ ! -f "$BUILT" ]; then
  echo "Build produced no $BUILT" >&2
  exit 1
fi

# The plugin's headers assume MetalANGLE was merged into the lib.
if ! nm -g "$BUILT" 2>/dev/null | grep -q 'OBJC_CLASS_\$_MGLKView'; then
  echo "Built lib has no MetalANGLE symbols - it will not satisfy the plugin's headers" >&2
  exit 1
fi

# A real copy, not dist's symlink target: this lib is a dev build, dist stays the last release build.
if [ -L "$LOCAL_XCFRAMEWORK" ] || [ ! -d "$LOCAL_XCFRAMEWORK" ]; then
  if [ ! -d "$DIST_XCFRAMEWORK" ]; then
    echo "No $DIST_XCFRAMEWORK to take the headers from - run build-ios.py --build-xcframework once" >&2
    exit 1
  fi
  rm -f "$LOCAL_XCFRAMEWORK"
  mkdir -p "$(dirname "$LOCAL_XCFRAMEWORK")"
  cp -R "$DIST_XCFRAMEWORK" "$LOCAL_XCFRAMEWORK"
fi

SLICE_DIR=$(find "$LOCAL_XCFRAMEWORK" -maxdepth 1 -type d -name '*maccatalyst*' | head -1)
if [ -z "$SLICE_DIR" ]; then
  echo "No Mac Catalyst slice in $LOCAL_XCFRAMEWORK" >&2
  exit 1
fi
LIB="$SLICE_DIR/MassifMaps.a"
STAMP="$SLICE_DIR/.dev-stamp"

# Rewriting a 100 MB lib on every run makes NativeScript re-copy it and Xcode relink the app for
# nothing; xcodebuild leaves libmassif.a alone when nothing changed, so mtime+size is a sound key.
FINGERPRINT=$(stat -f "%m %z" "$BUILT")
if [ -f "$STAMP" ] && [ -f "$LIB" ] && [ "$FINGERPRINT" = "$(cat "$STAMP")" ]; then
  echo "Up to date - $(basename "$SLICE_DIR")/MassifMaps.a already holds this build."
  exit 0
fi

cp "$BUILT" "$LIB"
echo "$FINGERPRINT" > "$STAMP"
echo "Done. Installed $CONFIGURATION build into $LOCAL_XCFRAMEWORK"
