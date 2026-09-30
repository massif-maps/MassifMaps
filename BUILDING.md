# Building Massif Maps 

**We strongly suggest to use the precompiled SDK versions that can be found in
the [Releases](https://github.com/massif-maps/MassifMaps/releases) section** 

Getting all the SDK dependencies resolved and waiting for the build to complete can be very time-consuming.

## Dependencies
The following instructions assume **Linux** or **MacOS** operating system. For Windows-based builds we
recommend first installing **Windows Subsystem for Linux (WSL)**. Once installed, the following
instructions can be used on Windows also. Otherwise minor changes are needed, like using 
`mklink /D` instead of `ln -s` in the following instructions.

We assume command-line versions of **git**, **unzip** and **curl** are already installed.

Use `git submodule` to resolve source-level dependencies:

```
git submodule update --init --recursive
```

Only `cpp/` is used out of `libs-external/mlt/mlt` (maplibre-tile-spec); its `test/` fixtures are
142 MB. The submodule is marked shallow, but a plain init still fetches those blobs — restrict it
once and the checkout drops to about 1 MB:

```
git -C libs-external/mlt/mlt sparse-checkout set cpp
```

Download and set up 'boost' library:

```
wget -q --show-progress -O boost_1_89_0.zip https://sourceforge.net/projects/boost/files/boost/1.89.0/boost_1_89_0.zip
unzip -q boost_1_89_0.zip
cd libs-external
ln -s ../boost_1_89_0 boost
cd ../boost_1_89_0
./bootstrap.sh
./b2 headers
cd ..
```

Special **swig** version (swig-2.0.11-nutiteq branch) is needed for generating language-specific wrappers, this can be downloaded from https://github.com/CartoDB/swig. Clone it and compile it using usual `./autogen.sh; ./configure; make` routine. Make sure build script refers to this one.

```
brew install autoconf automake libtool
git clone https://github.com//massif-maps/mobile-swig.git
cd mobile-swig
wget https://github.com/PhilipHazel/pcre2/releases/download/pcre2-10.44/pcre2-10.44.tar.gz
./Tools/pcre-build.sh
./autogen.sh
./configure
make
```

**Python 2.7.x** or **Python 3.x** is used for build scripts

**CMake 3.14 or later** is required by build scripts

Android build requires **Android SDK**, **JDK 10** or newer and **Android NDK r22** or newer.

iOS build requires **XCode 12** or later.

Universal Windows Platform build requires **Visual Studio 2022** and **Microsoft Windows SDK**.

## SDK profiles
Massif Maps can be compiled with different features. The feature set is defined by **profiles**,
which are defined in 'scripts/build/sdk_profiles.json' file. Different profiles can be combined, for
example the official SDK builds are currently compiled with 'valhalla+nmlmodellodtree' profiles. The
following instructions use 'standard' profile as an example.

In order to make SDK binaries as small as possible, 'lite' profile can be used. This profile disables
geocoding, routing and offline support, but resulting binaries are about 40% smaller.

## Building process
Be patient - full build will take 1+ hours. You can speed it up by limiting architectures and platforms where it is built.

The Android-family scripts (`build-android.py`, `build-routing-android.py`, `build-xamarin.py`) pick
**ninja** over make and prefix the compiler with **ccache**, both auto-detected and both opt-out
(`--ninja none`, `--ccache none`). Ninja comes from `PATH`, otherwise from the newest
`$ANDROID_HOME/cmake/*/bin/ninja`. Switching generator clears the affected `build/<target>-<abi>`
directory — CMake refuses to reconfigure a Makefiles tree as Ninja. Raise the cache before the first
run — `ccache --max-size 30G` — because one ABI writes ~1 GB of objects and the 5 GB default makes
the four ABIs evict each other. Measured on one arm64 Release build with a warm private cache:
**13.8 s** against 70.9 s cold. Where the binary size and the build time actually go is measured in
[`docs/internals/build-and-size.md`](docs/internals/build-and-size.md).

Go to 'scripts' library where the actual build scripts are located:

```
cd scripts
```

## Android build 
```
python swigpp-java.py --profile standard --swig ../mobile-swig/swig
python build-android.py --profile standard --build-aar --configuration=Release
```

## iOS build
```
python swigpp-objc.py --profile standard --swig ../mobile-swig/swig
python build-ios.py --profile standard
```

## Xamarin Android build
```
python swigpp-csharp.py --profile standard android --swig ../mobile-swig/swig
python build-xamarin.py --profile standard android
```

## Xamarin iOS build
```
python swigpp-csharp.py --profile standard ios --swig ../mobile-swig/swig
python build-xamarin.py --profile standard ios
```

## Universal Windows Platform build
```
python swigpp-csharp.py --profile standard winphone --swig ../mobile-swig/swig
python build-winphone.py --profile standard
```

## Web build
```
python3 scripts/build-web.py --profile standard --configuration Release --build-demo
```
Emscripten on `PATH` (or `--emsdk DIR`). Details: [docs/maintenance/web-build.md](docs/maintenance/web-build.md).

# Building the style tools and the styles

## Style tools (`massif-style`)
```
cd tools/style-cli && npm ci && npm run build
```
`mapbox2css` and `legend --svg` run from that alone. `css2xml` and `legend` also need the style
compiler as wasm in `tools/style-cli/wasm/`: built by `release-style-tools.yml`, or with emscripten:
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
python3 release/release.py 1.2.0-rc.1 --npm next  # publish a prerelease from this machine
```
Look at a change in the style dev page: `python3 tools/style-preview/serve.py --mbtiles NAME=tiles.mbtiles`
([docs/contributing/style-preview.md](docs/contributing/style-preview.md)). How the styles are
built and released: [docs/contributing/massif-style-release.md](docs/contributing/massif-style-release.md).

# Releasing

Everything is released by a manually dispatched workflow (**Actions → Run workflow**), each with a
`version` and a `publish` switch; with `publish` off it only builds, and keeps the result as an
artefact.

| What | Workflow | Tag | Publishes |
|---|---|---|---|
| SDK: Android, iOS, web | `build.yml` | `v<version>` | GitHub release (AAR, frameworks, web zip), npm `@massif-maps/api`, `@massif-maps/web` |
| Style tools | `release-style-tools.yml` | `style-tools-v<version>` | npm `@massif-maps/style-tools`, draft GitHub release with the wasm |
| Massif styles | `release-styles.yml`, or `styles/massif/release/release.py --npm TAG --github` from a machine | `massif-styles-v<version>` | npm `@massif-maps/styles`, GitHub release with one zip per flavour; redeploys the website |
| Website and docs | `docs.yml` | — | GitHub Pages: runs on pushes to `docs/`, `website/`, `styles/massif/`, nightly and after a release |

npm publishing needs the `NPM_TOKEN` secret. The documentation process:
[docs/contributing/release-workflow.md](docs/contributing/release-workflow.md).

# Usage
* Documentation: https://massif-maps.github.io/MassifMaps/
* Demo benches in this repo: `scripts/android-dev` (Android) and `scripts/ios-dev` (iOS)
* Scripts for preparing offline packages: https://github.com/nutiteq/mobile-sdk-scripts

The archived CartoDB sample apps ([android](https://github.com/CartoDB/mobile-android-samples),
[ios](https://github.com/CartoDB/mobile-ios-samples), [dotnet](https://github.com/CartoDB/mobile-dotnet-samples))
still build against the pre-rebrand API. See [`docs/migration.md`](docs/migration.md)
for what to rename.

# Support, Questions?
* Post an [issue](https://github.com/massif-maps/MassifMaps/issues) or a [Pull Request](https://github.com/massif-maps/MassifMaps/pulls)


# Building css2xml
 Go into `libs-massif/cartocss/util`
 Then run :
 ```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cd build
make -j $(( $(nproc) + 1 )) css2xml
```