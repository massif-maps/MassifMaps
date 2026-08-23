#!/bin/sh
#
# Compile tests for the generated autocompletion artefacts.
#
# Nothing here runs - COMPILING is the test. Each language is checked both ways: the good names
# have to type-check, and a made-up one has to fail. A test that only checks the first half passes
# just as well when the types have stopped meaning anything.
#
# Everything is skipped rather than failed when its toolchain is missing, so this runs on a Linux
# CI box and still does the TypeScript half.
#
#   tests/bindings/run.sh
#
set -u

root=$(cd "$(dirname "$0")/../.." && pwd)
here="$root/tests/bindings"
failures=0
skipped=0

report() {
  printf '%-10s %-46s %s\n' "$1" "$2" "$3"
}

fail() {
  report "$1" "$2" "FAIL"
  failures=$((failures + 1))
}

# --- TypeScript --------------------------------------------------------------------------------
#
# massif.test.ts is a type test: every @ts-expect-error only compiles when the line under it IS an
# error, so one file covers both directions.

tsc=""
for candidate in "$root/bindings/typescript/node_modules/typescript/bin/tsc" \
                 "$root/node_modules/typescript/bin/tsc" \
                 "$root/website/node_modules/typescript/bin/tsc"; do
  [ -f "$candidate" ] && tsc="$candidate" && break
done
if [ -z "$tsc" ]; then
  report "typescript" "no tsc (npm i in bindings/typescript)" "SKIP"
  skipped=$((skipped + 1))
else
  if node "$tsc" --noEmit --strict "$root/bindings/typescript/massif.test.ts" >/tmp/massif-ts.log 2>&1; then
    report "typescript" "massif.test.ts type-checks" "ok"
  else
    fail "typescript" "massif.test.ts type-checks"
    cat /tmp/massif-ts.log
  fi
fi

# --- Objective-C and Swift ---------------------------------------------------------------------

if ! xcrun --sdk iphonesimulator --show-sdk-path >/dev/null 2>&1; then
  report "objc" "no iOS SDK" "SKIP"
  report "swift" "no iOS SDK" "SKIP"
  skipped=$((skipped + 2))
else
  sdk=$(xcrun --sdk iphonesimulator --show-sdk-path)
  target=arm64-apple-ios15.0-simulator
  header="$root/ios/objc/api/MassifApiNames.h"

  if clang -fsyntax-only -fobjc-arc -isysroot "$sdk" -target "$target" \
      -I "$root/ios/objc/api" "$root/ios/objc/api/MassifApiNames.m" >/tmp/massif-objc.log 2>&1; then
    report "objc" "MassifApiNames.m compiles" "ok"
  else
    fail "objc" "MassifApiNames.m compiles"
    cat /tmp/massif-objc.log
  fi

  # The point of NS_TYPED_ENUM: Swift sees a struct with static members, so it writes
  # MassifProperty.opacity and never a string.
  if xcrun --sdk iphonesimulator swiftc -typecheck -sdk "$sdk" -target "$target" \
      -import-objc-header "$header" "$here/SwiftNames.swift" >/tmp/massif-swift.log 2>&1; then
    report "swift" "MassifProperty.opacity resolves" "ok"
  else
    fail "swift" "MassifProperty.opacity resolves"
    cat /tmp/massif-swift.log
  fi

  # And the other direction, which is the half that would silently rot: a name that does not
  # exist must NOT type-check.
  if xcrun --sdk iphonesimulator swiftc -typecheck -sdk "$sdk" -target "$target" \
      -import-objc-header "$header" "$here/SwiftNamesBad.swift" >/tmp/massif-swift-bad.log 2>&1; then
    fail "swift" "an unknown name is rejected"
  else
    report "swift" "an unknown name is rejected" "ok"
  fi
fi

echo
if [ "$failures" -gt 0 ]; then
  echo "$failures failure(s), $skipped skipped"
  exit 1
fi
echo "all binding checks passed ($skipped skipped)"
