#!/bin/sh
# iOS counterpart of android's DEBUGMASSIF=1: builds the SDK from source and assembles
# build/spm-local/MassifMaps.xcframework, which the ui-massifmaps plugin links when DEBUGMASSIF is set.
#
#   ./build-plugin-spm.sh [simulator|device|catalyst]     # default: simulator
#   CONFIGURATION=Release PROFILE=standard ./build-plugin-spm.sh device
#
# Each platform keeps its own slice in build/spm-local-slices; the xcframework holds every slice built so far.
# The first run per platform builds the whole SDK (long), later runs are an incremental xcodebuild.
set -e

cd "$(dirname "$0")"
BASE_DIR=$(cd ../.. && pwd)
PLATFORM=${1:-simulator}
SWIG=${SWIG:-/Volumes/dev/massif/mobile-swig/swig}
PROFILE=${PROFILE:-full}
SPM_DIR=${MASSIF_SPM_LOCAL:-$BASE_DIR/build/spm-local}
SLICES_DIR="$SPM_DIR-slices"

case "$PLATFORM" in
  simulator) BASE_ARCH=arm64-simulator; BUILD_DIR="$BASE_DIR/build/ios_metal-SIMULATOR-arm64"; PRODUCT_DIR=-iphonesimulator; CONFIGURATION=${CONFIGURATION:-Debug} ;;
  device) BASE_ARCH=arm64; BUILD_DIR="$BASE_DIR/build/ios_metal-OS-arm64"; PRODUCT_DIR=-iphoneos; CONFIGURATION=${CONFIGURATION:-Debug} ;;
  catalyst) BASE_ARCH=arm64-maccatalyst; BUILD_DIR="$BASE_DIR/build/ios_metal-MACCATALYST-arm64"; PRODUCT_DIR=; CONFIGURATION=${CONFIGURATION:-Release} ;;
  *) echo "Unknown platform '$PLATFORM': simulator, device or catalyst" >&2; exit 1 ;;
esac
PBXPROJ="$BUILD_DIR/massif.xcodeproj/project.pbxproj"
# The cmake project asks for iOS 13, which Xcode 27 no longer supports (15.0 minimum).
DEPLOYMENT_TARGET_SETTING=
[ "$PLATFORM" != catalyst ] && DEPLOYMENT_TARGET_SETTING="IPHONEOS_DEPLOYMENT_TARGET=${MASSIF_IOS_DEPLOYMENT_TARGET:-15.0}"

if [ ! -d "$BASE_DIR/generated/ios-objc/proxies" ]; then
  if [ ! -x "$SWIG" ]; then
    echo "SWIG executable not found at $SWIG - set SWIG=/path/to/mobile-swig/swig" >&2
    exit 1
  fi
  echo "==> Generating Objective-C bindings (profile: $PROFILE)"
  (cd .. && python3 swigpp-objc.py --profile "$PROFILE" --swig "$SWIG")
fi

# A build dir copied or moved from another checkout makes CMake refuse the cache. Set it aside, not deleted.
CACHE_DIR=$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null || true)
if [ -n "$CACHE_DIR" ] && [ "$CACHE_DIR" != "$BUILD_DIR" ]; then
  echo "==> $BUILD_DIR was configured in $CACHE_DIR: moving it aside and configuring afresh"
  mv "$BUILD_DIR" "$BUILD_DIR.stale-$(date +%Y%m%d%H%M%S)"
fi

# The profile sets the C++ defines (_MASSIF_WKBT_SUPPORT...) the wrappers in generated/ios-objc were generated for: a
# build dir configured under another one fails on "no member named 'WKTGeometryWriter' in namespace 'massif'".
PROFILE_STAMP="$BUILD_DIR/.plugin-spm-profile"
if [ ! -f "$PBXPROJ" ] || [ "$(cat "$PROFILE_STAMP" 2>/dev/null)" != "$PROFILE/$CONFIGURATION" ]; then
  echo "==> Configuring and building the whole SDK for $PLATFORM ($PROFILE, $CONFIGURATION), this takes a while"
  (cd .. && MASSIF_IOS_DEPLOYMENT_TARGET="${MASSIF_IOS_DEPLOYMENT_TARGET:-15.0}" python3 build-ios.py --profile "$PROFILE" --ios-arch "$BASE_ARCH" \
    --configuration "$CONFIGURATION" --use-metalangle) || [ -f "$PBXPROJ" ]
  echo "$PROFILE/$CONFIGURATION" > "$PROFILE_STAMP"
fi

# find_program caches the interpreter: a build dir configured under another python (a moved mise install) dies in the
# api-tables phase with "python3: No such file or directory". Drop the stale entry so CMake looks it up again.
CACHED_PYTHON=$(sed -n 's/^SDK_PYTHON:FILEPATH=//p' "$BUILD_DIR/CMakeCache.txt" || true)
if [ -n "$CACHED_PYTHON" ] && [ ! -x "$CACHED_PYTHON" ]; then
  echo "==> Reconfiguring: cached python $CACHED_PYTHON is gone"
  cmake -USDK_PYTHON "$BUILD_DIR" >/dev/null
fi

# A CMake reconfigure regenerates the pbxproj and loses the fix build-ios.py applies to it.
if grep -q -e '-apple-ios-13.0-macabi' "$PBXPROJ"; then
  sed -i '' 's/-apple-ios-13.0-macabi/-apple-ios13.1-macabi/g' "$PBXPROJ"
fi

echo "==> Building libmassif.a ($PLATFORM, $CONFIGURATION)"
BUILD_LOG="$BUILD_DIR/build.log"
if ! (cd "$BUILD_DIR" && xcodebuild -project massif.xcodeproj -arch arm64 \
  -configuration "$CONFIGURATION" build \
  ENABLE_BITCODE=NO $DEPLOYMENT_TARGET_SETTING \
  GCC_PREPROCESSOR_DEFINITIONS='$(inherited) _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION') >"$BUILD_LOG" 2>&1; then
  echo "Build failed - errors from $BUILD_LOG:" >&2
  grep -E "error:|BUILD FAILED" "$BUILD_LOG" | tail -20 >&2
  exit 1
fi

BUILT="$BUILD_DIR/$CONFIGURATION$PRODUCT_DIR/libmassif.a"
if [ ! -f "$BUILT" ]; then
  echo "Build produced no $BUILT" >&2
  exit 1
fi

# MetalANGLE (MGLKView) is merged by the Release prelink only; a Debug lib has it undefined, so merge libangle here.
# The plugin's headers assume it, and `nm -g | grep MGLKView` alone also matches the undefined entry.
defines_mglkview() { nm -g "$1" 2>/dev/null | grep -Eq '^[0-9a-f]+ [A-Za-z] _OBJC_CLASS_\$_MGLKView'; }
if ! defines_mglkview "$BUILT"; then
  ANGLE="$BASE_DIR/libs-external/angle-metal/$BASE_ARCH/libangle.a"
  if [ ! -f "$ANGLE" ]; then
    echo "No $ANGLE to merge into the built lib" >&2
    exit 1
  fi
  MERGED="$BUILD_DIR/libmassif-angle.a"
  if [ ! -f "$MERGED" ] || [ "$BUILT" -nt "$MERGED" ]; then
    echo "==> Merging libangle into libmassif.a"
    libtool -static -o "$MERGED" "$BUILT" "$ANGLE"
  fi
  BUILT="$MERGED"
  if ! defines_mglkview "$BUILT"; then
    echo "Merged lib still has no MetalANGLE symbols" >&2
    exit 1
  fi
fi

mkdir -p "$SLICES_DIR/$PLATFORM" "$SLICES_DIR/Headers"
HEADERS_TMP=$(mktemp -d)
(cd .. && python3 build-ios.py --profile "$PROFILE" --use-metalangle --headers-only "$HEADERS_TMP")
CHANGED=$(rsync -rci --delete "$HEADERS_TMP/" "$SLICES_DIR/Headers/")
rm -rf "$HEADERS_TMP"

# Rewriting the lib or the xcframework on every run makes NativeScript re-copy it and Xcode relink the app.
FINGERPRINT=$(stat -f "%m %z" "$BUILT")
STAMP="$SLICES_DIR/$PLATFORM/.stamp"
if [ -n "$CHANGED" ] || [ ! -f "$STAMP" ] || [ "$FINGERPRINT" != "$(cat "$STAMP")" ] || [ ! -d "$SPM_DIR/MassifMaps.xcframework" ]; then
  cp "$BUILT" "$SLICES_DIR/$PLATFORM/MassifMaps.a"
  echo "$FINGERPRINT" > "$STAMP"

  set --
  for SLICE in simulator device catalyst; do
    if [ -f "$SLICES_DIR/$SLICE/MassifMaps.a" ]; then
      set -- "$@" -library "$SLICES_DIR/$SLICE/MassifMaps.a" -headers "$SLICES_DIR/Headers"
    fi
  done
  rm -rf "$SPM_DIR/MassifMaps.xcframework"
  mkdir -p "$SPM_DIR"
  xcodebuild -create-xcframework "$@" -output "$SPM_DIR/MassifMaps.xcframework"
  cat > "$SPM_DIR/Package.swift" <<'EOF'
// swift-tools-version:5.5
import PackageDescription
let package = Package(name: "MassifMaps", platforms: [.iOS(.v13)], products: [.library(name: "MassifMaps", targets: ["MassifMaps"])], targets: [.binaryTarget(name: "MassifMaps", path: "MassifMaps.xcframework")])
EOF
  echo "Done. $PLATFORM $CONFIGURATION installed into $SPM_DIR"
else
  echo "Up to date - $SPM_DIR already holds this $PLATFORM build."
fi
