#!/bin/sh
# Build the SDK from source and drop it into the ui-massifmaps plugin's xcframework, so a
# NativeScript app links what is in all/native right now instead of a released framework. The
# NativeScript counterpart of bootstrap.sh: same edit-build-run loop, different consumer.
#
# Shares bootstrap.sh's build directory on purpose. The plugin's framework is a MetalANGLE build
# (MGLKView/MGLContext are defined in the shipped lib - angle is merged in by the libtool step),
# which is exactly what the demo builds, so one compile serves both and the demo and the app never
# disagree about the binary.
#
#   ./build-plugin-lib.sh                          # simulator arm64, Debug
#   CONFIGURATION=RelWithDebInfo ./build-plugin-lib.sh
#   ./build-plugin-lib.sh --restore                # put the released lib back
#   MASSIF_XCFRAMEWORK=/path/to/MassifMaps.xcframework ./build-plugin-lib.sh
#
# Incremental: re-running after touching one .cpp recompiles that file and relinks. Safe to call
# from a build hook on every run.
set -e

cd "$(dirname "$0")"
BASE_DIR=$(cd ../.. && pwd)
SWIG=${SWIG:-/Volumes/dev/carto/mobile-swig/swig}
# Must match the profile the plugin's framework was generated with, or the shipped headers
# describe an API this lib does not export.
PROFILE=${PROFILE:-standard+valhalla+geocoding+routing+packagemanager}
CONFIGURATION=${CONFIGURATION:-Debug}
XCFRAMEWORK=${MASSIF_XCFRAMEWORK:-/Volumes/dev/nativescript/ui-carto/packages/ui-massifmaps/platforms/ios/MassifMaps.xcframework}

ARCH=arm64
BASEARCH=arm64-simulator
SLICE=ios-arm64_x86_64-simulator
BUILD_DIR="$BASE_DIR/build/ios_metal-SIMULATOR-$ARCH"
SLICE_DIR="$XCFRAMEWORK/$SLICE"
LIB="$SLICE_DIR/MassifMaps.a"
ORIG="$SLICE_DIR/MassifMaps.a.orig"
X86_CACHE="$SLICE_DIR/.dev-x86_64.a"
STAMP="$SLICE_DIR/.dev-stamp"

if [ ! -d "$SLICE_DIR" ]; then
  echo "No simulator slice at $SLICE_DIR - set MASSIF_XCFRAMEWORK to the plugin's xcframework" >&2
  exit 1
fi

# --restore: back to the released lib, for a build that must not carry local changes.
if [ "$1" = "--restore" ]; then
  if [ -f "$ORIG" ]; then
    cp "$ORIG" "$LIB"
    rm -f "$STAMP" # or the next run would think the installed lib is already the built one
    echo "Restored the released MassifMaps.a"
  else
    echo "Nothing to restore - no $ORIG"
  fi
  exit 0
fi

# Keep the released lib once, and cache its x86_64 half. The slice is advertised as arm64+x86_64
# in the xcframework's Info.plist and we only build arm64, so the fresh arm64 is fused back onto
# the released x86_64 rather than shipping a thin lib the plist disagrees with.
if [ ! -f "$ORIG" ]; then
  echo "==> Keeping the released lib as $(basename "$ORIG")"
  cp "$LIB" "$ORIG"
fi
if [ ! -f "$X86_CACHE" ]; then
  if lipo -info "$ORIG" 2>/dev/null | grep -q x86_64; then
    lipo -thin x86_64 "$ORIG" -output "$X86_CACHE"
  else
    : > "$X86_CACHE" # thin original: nothing to fuse
  fi
fi

if [ ! -d "$BASE_DIR/generated/ios-objc/proxies" ]; then
  if [ ! -x "$SWIG" ]; then
    echo "SWIG executable not found at $SWIG - set SWIG=/path/to/mobile-swig/swig" >&2
    exit 1
  fi
  echo "==> Generating Objective-C bindings (profile: $PROFILE)"
  (cd ../ && python3 swigpp-objc.py --profile "$PROFILE" --swig "$SWIG")
fi

# Configure once, with the same flags bootstrap.sh uses so the two share this directory. The GL
# headers (GLES3/gl3.h) only exist on the include path when _MASSIF_USE_METALANGLE is set, so an
# Apple GL build of this tree does not compile at all - the define is not optional here.
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  echo "==> Configuring the SDK for SIMULATOR/$ARCH ($CONFIGURATION)"
  DEFINES=$(python3 - "$PROFILE" <<'PY'
import sys, os
sys.path.insert(0, os.path.join(os.getcwd(), '..'))
from build.sdk_build_utils import getProfile
print(' '.join('-D%s' % d for d in getProfile(sys.argv[1]).get('defines', '').split(';') if d))
PY
)
  OPTIONS=$(python3 - "$PROFILE" <<'PY'
import sys, os
sys.path.insert(0, os.path.join(os.getcwd(), '..'))
from build.sdk_build_utils import getProfile
print(' '.join('-D%s' % o for o in getProfile(sys.argv[1]).get('cmake-options', '').split(';') if o))
PY
)
  mkdir -p "$BUILD_DIR"
  # shellcheck disable=SC2086
  cmake -G Xcode $OPTIONS \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_BUILD_TYPE="$CONFIGURATION" \
    -DINCLUDE_OBJC:BOOL=ON \
    -DSINGLE_LIBRARY:BOOL=ON \
    -DSHARED_LIBRARY:BOOL=OFF \
    -DWRAPPER_DIR="$BASE_DIR/generated/ios-objc/proxies" \
    -DSDK_CPP_DEFINES="$DEFINES -D_MASSIF_USE_METALANGLE -DZSTD_STATIC_LINKING_ONLY" \
    -DSDK_VERSION=Devel \
    -DSDK_PLATFORM=iOS \
    -DSDK_IOS_ARCH="$ARCH" \
    -DSDK_IOS_BASEARCH="$BASEARCH" \
    -S "$BASE_DIR/scripts/build" -B "$BUILD_DIR"
fi

echo "==> Building libmassif.a ($CONFIGURATION)"
# Quiet on success, but keep the log: a build hook that swallows the compiler's output leaves you
# staring at a stale lib with no idea why.
BUILD_LOG="$BUILD_DIR/build.log"
if ! (cd "$BUILD_DIR" && xcodebuild -project massif.xcodeproj -arch "$ARCH" \
  -configuration "$CONFIGURATION" -sdk iphonesimulator build \
  ENABLE_BITCODE=NO \
  GCC_PREPROCESSOR_DEFINITIONS=_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION) >"$BUILD_LOG" 2>&1; then
  echo "Build failed - errors from $BUILD_LOG:" >&2
  grep -E "error:|BUILD FAILED" "$BUILD_LOG" | tail -20 >&2
  exit 1
fi

BUILT="$BUILD_DIR/$CONFIGURATION-iphonesimulator/libmassif.a"
if [ ! -f "$BUILT" ]; then
  echo "Build produced no $BUILT" >&2
  exit 1
fi

# Guard against linking a lib the shipped headers do not describe: MGLKView is defined only when
# angle was merged in, which is what the plugin's headers assume.
if ! nm -g "$BUILT" 2>/dev/null | grep -q 'OBJC_CLASS_\$_MGLKView'; then
  echo "Built lib has no MetalANGLE symbols - it will not satisfy the plugin's headers" >&2
  exit 1
fi

# Install only when the built lib actually changed. Rewriting the 88 MB fat lib on every run is
# not just the lipo: NativeScript re-copies the framework and Xcode relinks the app because the
# mtime moved, which is most of the time a no-op 'ns run ios' used to spend. Xcode does not relink
# libmassif.a when nothing changed, so its mtime+size is a sound fingerprint.
FINGERPRINT=$(stat -f "%m %z" "$BUILT")
if [ -f "$STAMP" ] && [ -f "$LIB" ] && [ "$FINGERPRINT" = "$(cat "$STAMP")" ]; then
  echo "Up to date - $SLICE/MassifMaps.a already holds this build."
  exit 0
fi

echo "==> Installing into $SLICE/MassifMaps.a"
if [ -s "$X86_CACHE" ]; then
  lipo -create "$BUILT" "$X86_CACHE" -output "$LIB"
else
  cp "$BUILT" "$LIB"
fi
echo "$FINGERPRINT" > "$STAMP"

echo "Done. $(lipo -info "$LIB")"
echo "Released lib kept at $(basename "$ORIG") - './build-plugin-lib.sh --restore' puts it back."
