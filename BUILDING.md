# Building Massif Maps

**Use the prebuilt packages unless you are changing the SDK** — every one is listed in
[Packages](https://massif-maps.github.io/MassifMaps/docs/getting-started/packages). A full build takes
an hour or more.

## What this repo builds

| Part | Source | Build | Output |
|---|---|---|---|
| Android SDK (AAR) | `all/`, `android/` | [`build-android.py`](#android) | `dist/android/*.aar` |
| iOS SDK (xcframework) | `all/`, `ios/` | [`build-ios.py`](#ios) | `dist/ios_metal/*.zip` |
| Standalone routing library | `routing-lib/` | [`build-routing-*.py`](#standalone-routing-library) | `dist/routing-android/*.aar`, `dist/routing-ios/*.zip` |
| Web SDK (wasm) | `all/`, `web/` | [`build-web.py`](#web-sdk) | `dist/web/` |
| `@massif-maps/api`, `@massif-maps/web`, `@massif-maps/style-tools`, `@massif-maps/styles` | `bindings/js`, `web/package`, `tools/style-cli`, `styles/massif` | [`npm-packages.py`](#npm-packages) | `dist/npm/*.tgz` |
| Generated API bindings | `all/modules`, `all/native/api` | [`gen-api-bindings.sh`](#generated-api-bindings) | committed files |
| `css2xml` (native) | `libs-massif/cartocss/util` | [cmake](#css2xml) | `css2xml` |
| NativeScript plugin | `integrations/nativescript` (own repo) | [lerna](#nativescript-plugin) | `@nativescript-community/ui-massifmaps` |
| Xamarin, UWP | `dotnet/`, `winphone/` | [source only](#xamarin-and-uwp) | not released |
| Documentation site | `docs/`, `website/` | [Docusaurus](#documentation-site) | GitHub Pages |

## Toolchain

The versions are the ones CI uses (`.github/workflows/build.yml`). Linux or macOS; on Windows use WSL.

| Tool | Needed for | Version |
|---|---|---|
| git, unzip, curl, Python 3, CMake ≥ 3.14 | everything | Python 3.12 in CI |
| the SWIG fork ([below](#swig)) | Android, iOS, Xamarin, UWP | `massif-maps/mobile-swig` |
| JDK, Android SDK, NDK, Gradle | Android | JDK 17, NDK r27, Gradle 8.14.3 |
| Xcode | iOS | latest stable |
| Emscripten (`emsdk`) | web SDK, style tools wasm | 6.0.9 |
| Node.js | npm packages, docs site | current LTS |
| ninja, ccache | optional, faster Android builds | — |

### Sources

```
git submodule update --init --recursive
git -C libs-external/mlt/mlt sparse-checkout set cpp
```

Only `cpp/` of `libs-external/mlt/mlt` is used; the sparse checkout drops 142 MB of test fixtures.

### Boost

```
wget -q --show-progress -O boost_1_89_0.zip https://sourceforge.net/projects/boost/files/boost/1.89.0/boost_1_89_0.zip
unzip -q boost_1_89_0.zip
ln -s ../boost_1_89_0 libs-external/boost
(cd boost_1_89_0 && ./bootstrap.sh && ./b2 headers)
```

On Windows use `mklink /D` instead of `ln -s`.

### SWIG

The wrappers need the SWIG fork, not a stock SWIG:

```
brew install autoconf automake libtool
git clone https://github.com/massif-maps/mobile-swig.git
cd mobile-swig
wget https://github.com/PhilipHazel/pcre2/releases/download/pcre2-10.44/pcre2-10.44.tar.gz
./Tools/pcre-build.sh
./autogen.sh && ./configure && make
```

Pass its path as `--swig` below. Gradle never runs SWIG: rerun `swigpp-*.py` after touching `all/modules/*.i`.

## SDK profiles

A profile picks the features compiled in; profiles are defined in `scripts/build/sdk_profiles.json`
and combine with `+` (`standard+valhalla`). Releases ship `full`, `standard` and `lite`. `lite`
drops geocoding, routing and offline packages and is about 40% smaller.

## Native SDK

```
cd scripts
```

The Android-family scripts (`build-android.py`, `build-routing-android.py`, `build-xamarin.py`) pick
**ninja** over make and prefix the compiler with **ccache**, both auto-detected and both opt-out
(`--ninja none`, `--ccache none`). Raise the cache first — `ccache --max-size 30G` — since one ABI
writes ~1 GB of objects. Measured on one arm64 Release build with a warm cache: **13.8 s** against
70.9 s cold. Where build time and binary size go:
[`docs/internals/build-and-size.md`](docs/internals/build-and-size.md).

### Android

```
python swigpp-java.py --profile standard --swig ../mobile-swig/swig
python build-android.py --profile standard --build-aar --configuration=Release --build-version 6.1.0
```

`--android-abi arm64-v8a` builds one ABI only. The demo app, `scripts/android-dev`, builds the SDK
from source: [demo app](docs/contributing/demo-app.md).

### iOS

```
python swigpp-objc.py --profile standard --swig ../mobile-swig/swig
python build-ios.py --profile standard --configuration=Release --build-xcframework --use-metalangle --build-version 6.1.0
```

The demo app is `scripts/ios-dev`.

### Standalone routing library

Valhalla routing without the map view — no SWIG step:

```
python build-routing-android.py --configuration=Release --build-version 6.1.0
python build-routing-ios.py --configuration=Release --build-xcframework --build-version 6.1.0
```

### Xamarin and UWP

Built from source only, not covered by CI:

```
python swigpp-csharp.py --profile standard android --swig ../mobile-swig/swig
python build-xamarin.py --profile standard android        # ios: same two lines with `ios`
python swigpp-csharp.py --profile standard winphone --swig ../mobile-swig/swig
python build-winphone.py --profile standard               # Visual Studio 2022 + Windows SDK
```

## Web SDK

```
python3 scripts/build-web.py --profile standard --configuration Release --build-version 6.1.0 [--build-demo]
```

Emscripten on `PATH`, or `--emsdk DIR`. Writes `dist/web`; [`npm-packages.py`](#npm-packages) turns
it into `@massif-maps/web`. Details: [docs/maintenance/web-build.md](docs/maintenance/web-build.md).

## Generated API bindings

The facade's typings and name tables (`docs/api/massif-api.json`, `bindings/typescript/massif.d.ts`,
the Objective-C, Java and C name constants) are generated and committed:

```
scripts/gen-api-bindings.sh            # regenerate
scripts/gen-api-bindings.sh --check    # what CI runs: fail if anything is stale
```

## css2xml

```
cd libs-massif/cartocss/util
cmake -B build -DCMAKE_BUILD_TYPE=Release
make -C build -j css2xml
```

## Style tools

`massif-style` (`@massif-maps/style-tools`):

```
cd tools/style-cli && npm ci && npm run build
```

`mapbox2css` and `legend --svg` run from that alone. `css2xml` and `legend` also need the style
compiler as wasm in `tools/style-cli/wasm/` — attached to every SDK release, or built with emscripten:

```
cd libs-massif/cartocss/util
emcmake cmake -B build-wasm -DCMAKE_BUILD_TYPE=MinSizeRel && emmake make -C build-wasm massif-style
cp build-wasm/massif-style.{mjs,wasm} ../../../tools/style-cli/wasm/
```

Internals: [docs/contributing/style-tools.md](docs/contributing/style-tools.md).

## The Massif styles

```
(cd tools/style-sprite && npm ci)                                # sprite sheet and icon font
(cd styles/massif/release && npm ci && npx playwright install chromium)   # release screenshots
cd styles/massif
node ../../tools/style-sprite/build.mjs sprite-src sprite sprite   # after an SVG change
python3 build.py              # the MapLibre variants
python3 build.py --convert    # and the SDK CartoCSS project, into carto/
python3 release/release.py 1.2.0 --screenshots   # every flavour, zipped, and the npm package, into dist/
```

Look at a change in the style dev page: `python3 tools/style-preview/serve.py --mbtiles NAME=tiles.mbtiles`
([docs/contributing/style-preview.md](docs/contributing/style-preview.md)). How the styles are
built and released: [docs/contributing/massif-style-release.md](docs/contributing/massif-style-release.md).

## npm packages

One script builds and packs every npm package, locally and in CI:

```
python3 scripts/npm-packages.py pack 6.1.0-rc.1 [--only api,web,style-tools] [--styles-version 1.2.0-rc.1]
python3 scripts/npm-packages.py publish [--tag next] [--dry-run]
```

| Package | Version | `pack` needs first |
|---|---|---|
| `@massif-maps/api` | SDK | nothing |
| `@massif-maps/web` | SDK, depends on `@massif-maps/api` at the same version | `dist/web` ([Web SDK](#web-sdk)) |
| `@massif-maps/style-tools` | SDK | `tools/style-cli/wasm/massif-style.{mjs,wasm}` ([Style tools](#style-tools)) |
| `@massif-maps/styles` | its own, only with `--styles-version` | nothing |

`pack` writes `dist/npm/*.tgz` and leaves no diff behind. `publish` publishes those tarballs,
`@massif-maps/api` before `@massif-maps/web`, under `--tag` — by default `next` for a prerelease
version and `latest` otherwise; in GitHub Actions it adds `--provenance`. To try the tarballs in an
app before anything reaches npm:

```
npm install /path/to/dist/npm/massif-maps-api-6.1.0-rc.1.tgz /path/to/dist/npm/massif-maps-web-6.1.0-rc.1.tgz
npm install -g /path/to/dist/npm/massif-maps-style-tools-6.1.0-rc.1.tgz && massif-style --help
```

`@massif-maps/types` (`bindings/typescript`), `tools/style-sprite` and `web/package` are private
build helpers, never published.

## NativeScript plugin

`integrations/nativescript` is its own repo, `nativescript-community/ui-massifmaps`, with its own
version. It needs `yarn` or `pnpm` — `npm` does not work there:

```
cd integrations/nativescript
yarn
npm run build
npm run demo.vue.android        # or demo.svelte.ios, ...
```

On Android the plugin pulls the AAR from JitPack at the gradle property `massifSDKVersion`
(default in `packages/ui-massifmaps/platforms/android/include.gradle`) and `massifSDKVariant`
(`full`, `core`, `lite`); `massifAar` points it at a local AAR instead. It is released from its
own repo with `npm run publish` (lerna).

## Documentation site

```
cd website && npm install
npm start        # dev server + hot reload (also watches ../docs)
npm run build    # what CI runs — the only thing that checks links
```

See [docs/contributing/docs-site.md](docs/contributing/docs-site.md).

# Releasing

Every release is a manually dispatched workflow (**Actions → Run workflow**) with a `version` and a
`publish` switch; with `publish` off it only builds and keeps the result as workflow artefacts.

| What | Workflow | Tag | Publishes |
|---|---|---|---|
| SDK: Android, iOS, routing, web, style tools | `build.yml` | `v<version>` | GitHub release (AARs, xcframeworks, routing, web zip, `css2xml`, API bindings, style compiler wasm); JitPack and Swift Package tags; npm `@massif-maps/api`, `@massif-maps/web`, `@massif-maps/style-tools` |
| Massif styles | `release-styles.yml`, or `styles/massif/release/release.py --npm TAG --github` | `massif-styles-v<version>` | npm `@massif-maps/styles`, one zip per flavour; redeploys the website |
| NativeScript plugin | `npm run publish` in `integrations/nativescript` | its own | npm `@nativescript-community/ui-massifmaps` |
| Website and docs | `docs.yml` | — | GitHub Pages, on pushes to `docs/`, `website/`, nightly and after a release |

npm publishing needs the `NPM_TOKEN` secret and the `@massif-maps` npm organisation;
`release-style-tools.yml` only builds and packs, `build.yml` publishes its tarball.

## Pre-release, then release

1. **Pack locally** and try the tarballs ([npm packages](#npm-packages)).
2. **Pre-release from CI**: run `build.yml` with version `6.1.0-rc.1`, `publish` on, `prerelease` on.
   It makes a GitHub *prerelease*, publishes the npm packages under the `next` dist-tag (a plain
   `npm install` does not pick them up), tags JitPack and the Swift package `6.1.0-rc.1`, and does
   not touch `CHANGELOG.md`. Styles: `release-styles.yml` with `1.2.0-rc.1` does the same for them.
3. **Test every package** from the registries:
   - Android: `implementation 'com.github.massif-maps:MassifMaps-android-aar:6.1.0-rc.1'`, and
     `...:6.1.0-rc.1:routing@aar` for the routing library
   - iOS: `https://github.com/massif-maps/MassifMaps-ios-swift`, *Exact version* `6.1.0-rc.1`,
     products `MassifMaps` and `ValhallaRouting`
   - web: `npm install @massif-maps/web@next`
   - style tools: `npx @massif-maps/style-tools@next --help`
   - styles: `npm install @massif-maps/styles@next`
   - NativeScript: the demo with `massifSDKVersion=6.1.0-rc.1`
4. **Release**: the same run with version `6.1.0` and `prerelease` off. npm `latest`, the full
   `CHANGELOG.md` entry since the last final release, a regular GitHub release.

The run refuses a version below an existing `v` tag. npm publishes only after every platform built,
so a failed build never leaves a version taken on npm.

## After a release

- NativeScript plugin: bump the `massifSDKVersion` default, release it.
- The version in `README.md` and `docs/getting-started/installation.md`.
- The first npm release: set the web row of *Prebuilt artifacts published* to true in
  `website/src/data/platforms.js`.

The documentation pipeline: [docs/contributing/release-workflow.md](docs/contributing/release-workflow.md).
