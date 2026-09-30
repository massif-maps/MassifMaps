#!/bin/sh
# Generate the iOS demo project. Mirrors what scripts/android-dev gets from gradle: the SDK is
# built as a dependency of the app, not consumed as a prebuilt framework.
#
#   ./bootstrap.sh                  # arm64 simulator (the usual dev target)
#   ./bootstrap.sh device           # arm64 device
#   PROFILE=lite ./bootstrap.sh     # a different feature profile
#   PROFILE_RENDER=1 ./bootstrap.sh # per-frame timings, the counterpart of -PprofileRender
#   NATIVE_OPT=0 ./bootstrap.sh     # an -O0 SDK in Debug, to step through native code
#
# Re-run it after changing the profile or the platform. Day to day you do not: once the project
# exists, build from Xcode or with 'xcodebuild -project MassifDemo.xcodeproj'.
set -e

cd "$(dirname "$0")"
BASE_DIR=$(cd ../.. && pwd)
SWIG=${SWIG:-/Volumes/dev/carto/mobile-swig/swig}
# Matches scripts/android-dev/massif/build.gradle, so the two demos expose the same API.
PROFILE=${PROFILE:-standard+valhalla+geocoding+routing+packagemanager}

case "${1:-simulator}" in
  simulator) PLATFORM=SIMULATOR; SYSROOT=iphonesimulator; BASEARCH=arm64-simulator ;;
  device)    PLATFORM=OS;        SYSROOT=iphoneos;       BASEARCH=arm64 ;;
  *) echo "usage: $0 [simulator|device]" >&2; exit 1 ;;
esac
ARCH=arm64
BUILD_DIR="$BASE_DIR/build/ios_metal-$PLATFORM-$ARCH"

# Render profiling, off unless asked for - the counterpart of android-dev's '-PprofileRender'.
# Turns on the per-frame section timings (utils/FrameProfiler.h) and the vt draw/label/tile
# counters (vt/RenderStats.h); neither exists in the binary otherwise. Read the 'PROF' and
# 'RenderStats' lines with 'xcrun simctl spawn <udid> log stream' or the device console.
# Changing it needs this re-run: it is a compile-time flag, not a launch argument.
PROFILER_DEFINES=""
if [ -n "$PROFILE_RENDER" ]; then
  PROFILER_DEFINES="-DMASSIF_FRAME_PROFILER=1 -DMASSIF_VT_RENDER_STATS=1"
  echo "==> Render profiling ON"
fi

# The SDK's Debug is -O2 -g, as android-dev's '-PnativeOpt': at -O0 a Massif tile decodes ~13x
# slower and an example opens after seconds of parsing its style on the main thread.
DEBUG_FLAGS="-O2 -g"
if [ "${NATIVE_OPT:-1}" = 0 ]; then
  DEBUG_FLAGS="-O0 -g"
fi

if [ ! -x "$SWIG" ]; then
  echo "SWIG executable not found at $SWIG - set SWIG=/path/to/mobile-swig/swig" >&2
  exit 1
fi

echo "==> Generating Objective-C bindings (profile: $PROFILE)"
(cd ../ && python3 swigpp-objc.py --profile "$PROFILE" --swig "$SWIG")

echo "==> Configuring the SDK Xcode project for $PLATFORM/$ARCH"
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
  -DCMAKE_OSX_SYSROOT="$SYSROOT" \
  -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS_DEBUG="$DEBUG_FLAGS" \
  -DCMAKE_CXX_FLAGS_DEBUG="$DEBUG_FLAGS" \
  -DINCLUDE_OBJC:BOOL=ON \
  -DSINGLE_LIBRARY:BOOL=ON \
  -DSHARED_LIBRARY:BOOL=OFF \
  -DWRAPPER_DIR="$BASE_DIR/generated/ios-objc/proxies" \
  -DSDK_CPP_DEFINES="$DEFINES -D_MASSIF_USE_METALANGLE -DZSTD_STATIC_LINKING_ONLY $PROFILER_DEFINES" \
  -DSDK_VERSION=Devel \
  -DSDK_PLATFORM=iOS \
  -DSDK_IOS_ARCH="$ARCH" \
  -DSDK_IOS_BASEARCH="$BASEARCH" \
  -S "$BASE_DIR/scripts/build" -B "$BUILD_DIR"

# project.yml refers to the SDK project through this symlink, so switching platform is a
# re-bootstrap rather than an edit.
rm -f .sdkproj .angle
ln -s "$BUILD_DIR" .sdkproj
# MetalANGLE ships one static slice per arch; project.yml links it through this symlink.
ln -s "$BASE_DIR/libs-external/angle-metal/$BASEARCH" .angle

echo "==> Generating MassifDemo.xcodeproj"
./regen.sh

echo
echo "Done. Open MassifDemo.xcodeproj, or:"
echo "  xcodebuild -project MassifDemo.xcodeproj -scheme MassifDemo -sdk $SYSROOT build"
