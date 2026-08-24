#!/usr/bin/env bash
# Generate the Android (Java) API reference (Javadoc) into website/static/api/android.
#
# Steps: run the SWIG Java proxy generator to emit the public com.massifmaps.* Java
# sources, then run `javadoc` over them. No native compile is needed — Javadoc only
# reads the generated sources.
#
# Prerequisites:
#   - The SWIG fork executable. Point to it with $SWIG (default: `swig` on PATH).
#     Locally that is typically /Volumes/dev/carto/mobile-swig/swig.
#   - A JDK (`javadoc` on PATH).
#   - Submodules checked out (libs-massif, libs-external).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCRIPTS="$ROOT/scripts"
SWIG="${SWIG:-swig}"
PROFILE="${SWIG_PROFILE:-standard+valhalla+geocoding+routing+packagemanager}"
BUILD="$ROOT/build/docs-api/android"
PROXY="$BUILD/proxies"
OUT="$ROOT/website/static/api/android"

command -v "$SWIG" >/dev/null || { echo "SWIG not found ($SWIG). Set \$SWIG to the SWIG fork."; exit 1; }
command -v javadoc >/dev/null || { echo "javadoc not found (install a JDK)."; exit 1; }

# Public classes (MapView, BitmapUtils, …) reference android.* framework types, so
# javadoc needs android.jar on the classpath. Resolve the SDK from the usual places.
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
if [ -z "$SDK" ] && [ -f "$ROOT/scripts/android-dev/local.properties" ]; then
  SDK="$(sed -n 's/^sdk.dir=//p' "$ROOT/scripts/android-dev/local.properties" | head -1)"
fi
[ -z "$SDK" ] && [ -d "$HOME/Library/Android/sdk" ] && SDK="$HOME/Library/Android/sdk"
ANDROID_JAR=""
if [ -n "$SDK" ] && [ -d "$SDK/platforms" ]; then
  ANDROID_JAR="$(ls -d "$SDK"/platforms/android-*/android.jar 2>/dev/null | sort -V | tail -1)"
fi
[ -n "$ANDROID_JAR" ] && echo "==> android.jar: $ANDROID_JAR" || echo "==> WARNING: no android.jar found; javadoc may fail on android.* references"

rm -rf "$BUILD" && mkdir -p "$PROXY"

# The proxies of the enum namespaces carry an @IntDef, and androidx is not on a docs
# machine (nor in the CI job — no gradle here). A source stub is enough for javadoc.
STUBS="$BUILD/stubs"
mkdir -p "$STUBS/androidx/annotation"
cat > "$STUBS/androidx/annotation/IntDef.java" <<'EOF'
package androidx.annotation;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/** Stub of the androidx typedef annotation, so the docs build needs no androidx dependency. */
@Retention(RetentionPolicy.SOURCE)
public @interface IntDef {
  long[] value() default {};
  boolean flag() default false;
  boolean open() default false;
}
EOF

echo "==> Generating Java proxies (profile: $PROFILE)"
# swigpp-java.py resolves its paths relative to the scripts/ directory and emits
# Javadoc-ready sources (SWIG is invoked with -doxygen). Run it from there.
( cd "$SCRIPTS" && python3 swigpp-java.py \
    --profile "$PROFILE" \
    --swig "$SWIG" \
    --proxydir "$PROXY" \
    --wrapperdir "$BUILD/wrappers" \
    --moduledir "$BUILD/modules" )

echo "==> Running javadoc"
rm -rf "$OUT" && mkdir -p "$OUT"
# Document the public proxies only; the *ModuleJNI / *Module classes are internal
# JNI plumbing. Hand-written Android helpers (e.g. DontObfuscate) live in android/java
# and go on the -sourcepath so references resolve.
find "$PROXY" -name '*.java' ! -name '*ModuleJNI.java' ! -name '*Module.java' > "$BUILD/sources.txt"
COUNT=$(wc -l < "$BUILD/sources.txt" | tr -d ' ')
echo "   $COUNT public Java sources"
[ "$COUNT" -gt 0 ] || { echo "No Java sources generated — check the SWIG run above."; exit 1; }

# Unresolved Android-framework types are expected; -Xdoclint:none + continue past them.
javadoc \
  -d "$OUT" \
  -windowtitle "Massif Maps — Android API" \
  -doctitle "Massif Maps — Android API" \
  -notimestamp -quiet -Xdoclint:none \
  -sourcepath "$PROXY:$ROOT/android/java:$STUBS" \
  ${ANDROID_JAR:+-classpath "$ANDROID_JAR"} \
  @"$BUILD/sources.txt" || echo "   (javadoc reported warnings/unresolved externals — output still generated)"

# javadoc gives up at 100 errors and writes nothing; without this the CI job stays
# green and the published /api/android/ 404s.
[ -f "$OUT/index.html" ] || { echo "javadoc produced no output — see the errors above."; exit 1; }

echo "==> Android API reference at $OUT"
