# Release check

The npm packages consumed the way an app does — from npm or from `pack`'s tarballs, never from this
repo's sources. Part of the pre-release steps in [BUILDING.md](../../BUILDING.md#pre-release-then-release).

```sh
cd scripts/release-check
npm install                                   # the `next` dist-tag from npm
npm install --no-save ../../dist/npm/*.tgz    # or: the tarballs `npm-packages.py pack` wrote
npm run check     # api imports, web ships both modules, style-tools compiles and converts the styles
npm run serve     # http://localhost:8099: a map from @massif-maps/web drawn with @massif-maps/styles
```

The page reports on itself (top left). Try `?variant=full` — offline routing must then be present —
and `?style=outdoor`, `topo`, `hybrid`, `eink`. For a final release, `npm install
@massif-maps/web@latest ...` instead.

## Android — `android/`

The AAR from JitPack, and the routing library as its `routing` variant, in one app:

```sh
cd scripts/release-check/android
./gradlew installDebug                          # massifVersion from gradle.properties
./gradlew installDebug -PmassifVersion=6.1.0-rc.2 -PmassifVariant=core
./gradlew installDebug -PmassifAarDir=/path/to/aars   # a map AAR + the routing AAR, not JitPack
```

It needs `local.properties` (the Android SDK path). Every check logs under the tag
`massif-release-check` and shows over the map: `ok    map SDK`, `ok    routing library ...`.

## iOS — `ios/`

The Swift package at an exact version, from GitHub. `MapCheck` is a map product alone; `RoutingCheck` is
`ValhallaRouting` beside `MassifMapsCore`, the pairing it is for:

```sh
cd scripts/release-check/ios
./generate.sh 6.1.0-rc.2 [MassifMaps|MassifMapsCore|MassifMapsLite]   # needs xcodegen
open ReleaseCheck.xcodeproj
```

Same `massif-release-check` lines, in the device log and over the map.
