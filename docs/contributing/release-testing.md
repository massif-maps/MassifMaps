---
title: Testing a Release
description: "Check every published package of a release - npm, JitPack, Swift Package Manager - from scratch apps, before calling it final."
sidebar_position: 2.5
---

# Testing a published release

A release is tested the way an app consumes it: from the registries, never from this repo's
sources. [`scripts/release-check`](https://github.com/massif-maps/MassifMaps/tree/master/scripts/release-check)
holds one scratch consumer per channel. Run all three on a release candidate before dispatching the
final version; how a candidate is made is in
[BUILDING.md](https://github.com/massif-maps/MassifMaps/blob/master/BUILDING.md#pre-release-then-release).

The examples below use `6.1.0-rc.3`; replace it with the version under test.

## Before you start

- The GitHub release exists and its assets download: JitPack and the Swift package fetch them from
  it. A candidate is a *Pre-release* on the
  [Releases page](https://github.com/massif-maps/MassifMaps/releases).
- npm carries the version under the right dist-tag — `next` for a candidate, `latest` for a final:

  ```bash
  npm view @massif-maps/web dist-tags
  ```

  A package published minutes ago can still answer 404 at its root while
  `https://registry.npmjs.org/@massif-maps/web/6.1.0-rc.3` resolves: the registry caches a miss.
  Wait, do not republish.

## npm: api, web, style-tools, styles

```bash
cd scripts/release-check
npm install          # the `next` dist-tag; for a final: npm install @massif-maps/{api,web,style-tools,styles}@latest
npm run check
npm run serve        # http://localhost:8099
```

`npm run check` must end with `all passed`. It checks that:

- all four packages installed, and their versions are the ones under test;
- `@massif-maps/api` imports;
- `@massif-maps/web` depends on the same `@massif-maps/api` version and ships both modules
  (`massif-web.*` and `massif-web-full.*`);
- `massif-style css2xml` compiles the Massif CartoCSS project from `@massif-maps/styles`, and
  `mapbox2css --validate` converts its MapLibre style — both through the style compiler wasm.

The page draws Mont Blanc with `@massif-maps/web` in the Massif style and reports on itself, top
left; every line must read `ok`. Then load `?variant=full` (offline routing must be *present*) and
`?style=outdoor`, `topo`, `hybrid`, `eink`.

To test the tarballs of `npm-packages.py pack` before anything is published, install them instead:
`npm install --no-save ../../dist/npm/*.tgz`.

## Android: JitPack

```bash
cd scripts/release-check/android
cp ../../android-dev/local.properties .        # or any file with sdk.dir=
./gradlew installDebug -PmassifVersion=6.1.0-rc.3
./gradlew installDebug -PmassifVersion=6.1.0-rc.3 -PmassifVariant=core   # and lite
```

The app takes `com.github.massif-maps:MassifMaps-android-aar` at that version — `full` by default,
the other variants as classifiers — and its `routing` variant. The first request of a new tag
makes JitPack build it, which takes a few minutes; its log is at
`https://jitpack.io/com/github/massif-maps/MassifMaps-android-aar/6.1.0-rc.3/build.log` and must list
every variant, `routing` included.

On the device, over the map and in logcat under the tag `massif-release-check`:

```
ok    map SDK
ok    routing library profile pedestrian
```

Read the log with `python3 scripts/devtap.py logs android --device <serial> --grep massif-release-check`.
The two emulators of the demo app are shared between sessions: test on a device or an AVD of your own.

## iOS: Swift Package Manager

```bash
cd scripts/release-check/ios
./generate.sh 6.1.0-rc.3                    # needs xcodegen; MassifMapsCore or MassifMapsLite as 2nd argument
open ReleaseCheck.xcodeproj
```

The project pins `massif-maps/MassifMaps-ios-swift` at that exact version and has two apps:

| Scheme | Links | Shows |
|---|---|---|
| `MapCheck` | the map product given to `generate.sh` (`MassifMaps`, the full profile, by default) | `ok    map SDK` |
| `RoutingCheck` | `MassifMapsCore` and `ValhallaRouting` | `ok    map SDK`, `ok    routing library profile pedestrian` |

`ValhallaRouting` is never linked beside `MassifMaps`: the full profile defines the same routing
classes ([Packages](../getting-started/packages.md)). The lines also go to the device log with
the prefix `massif-release-check`.

If package resolution fails with *"Revision … does not match previously recorded value"*, the tag
moved after this machine had resolved it (a re-run release forces its tags). Clear the record:

```bash
rm ~/Library/org.swift.swiftpm/security/fingerprints/massifmaps-ios-swift*
```

A checksum mismatch, instead, means the `Package.swift` on the tag does not describe the release's
zips: the release is broken, not the machine.

## NativeScript

The plugin is released from its own repo. Point its demo at the candidate with the gradle property
`massifSDKVersion=6.1.0-rc.3` and run `npm run demo.vue.android` in `integrations/nativescript`.

## When something fails

Once the GitHub release is public nothing is rolled back: fix the cause and re-run the failed job.
The JitPack and Swift tags are forced, and `npm-packages.py publish` skips a version npm already
has. A bug in the packages themselves means a new candidate (`rc.4`): npm never accepts a version
twice. The job order and what each publishes: [Release workflow](release-workflow.md).
