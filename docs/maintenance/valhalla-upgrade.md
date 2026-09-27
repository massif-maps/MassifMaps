---
title: Upgrading Valhalla
description: Merging an upstream Valhalla release, regenerating protos/locales/tz, and the fork patches that must survive.
sidebar_position: 1
---

# Upgrading Valhalla

Last run: **3.5.1 → 3.8.3**, 2026-08-15. 536 upstream commits, 23 conflicts, ~1 day of work.

Valhalla lives in **three** places:

| Path | What it is |
|------|-----------|
| `libs-external/valhalla/valhalla/` | nested submodule, our fork `massif-maps/valhalla`, branch `mbtiles-support` |
| `libs-external/valhalla/{CMakeLists.txt,config,proto}` | our build wrapper + **checked-in generated artefacts** |
| `scripts/routing/CMakeLists.txt`, `scripts/build/CMakeLists.txt` | which subprojects the routing lib / the SDK build |

Nothing in the upstream CMake is used: the wrapper globs the sources we want and every generated
file (protos, locales, tz database) is pre-generated and checked into `libs-external`.

## 0. Before starting

- `git -C libs-external/valhalla/valhalla status -sb` — the submodule is routinely on a **detached
  HEAD**. Branch first or the work lands off-branch.
- The fork is ~45 commits ahead of its merge base. `git diff <merge-base>..HEAD --stat` is the list
  of everything that must survive the merge. Read it before resolving anything.
- History matters: `050fff4` in `libs-external` reverted an earlier "use latest date" bump because
  routing **crashed in production**. The cause was the timezone data, not the date library — see
  [§5](#5-the-timezone-database).

## 1. Merge

```bash
cd libs-external/valhalla/valhalla
git remote add upstream https://github.com/valhalla/valhalla.git   # once
git fetch upstream --tags
git checkout -b chore/merge-valhalla-<version>
git merge <version>          # e.g. 3.8.3
```

`third_party/just_gtfs` fails to fetch (upstream references a commit that no longer exists). Ignore
it — nothing we compile uses it.

### Conflicts that come back every time

| File | Why it conflicts | Resolution |
|------|------------------|------------|
| `src/baldr/graphreader.cc` | the mbtiles constructor + the `GetGraphTile` mbtiles branch | keep ours, take upstream around it; every `const` member added upstream must be initialised in the mbtiles ctor |
| `valhalla/baldr/graphreader_mbtiles.h` | fork-only file | never conflicts, but its includes rot — it followed `filesystem.h`, which upstream deleted |
| `src/sif/bicyclecost.cc` | `non_network_penalty` | re-add our field and the `JSON_PBF_RANGED_DEFAULT` line |
| `src/sif/pedestriancost.cc` | our `use_roads` stress model on pedestrian | port it onto upstream's current `EdgeCost` shape |
| `proto/descriptors/options.proto` | **field numbers collide** | fork-only fields live at **200+**; upstream took 92 for `hierarchy_limits` |
| `src/skadi/sample.cc` | elevation is gutted on mobile | keep the `return NO_DATA_VALUE;` / commented bodies |
| `src/tyr/locate_serializer.cc`, `src/thor/*`, `valhalla/worker.h` | null-tile guards, `ParseApi(customLocales)` | keep ours, adopt upstream's renames |
| `src/baldr/datetime.cc` | we swallow a missing timezone instead of throwing | keep the `try`/`catch` |

Anything under `third_party/` should end up **exactly** at upstream's pointers:

```bash
git checkout <version> -- third_party
git rm --cached third_party/<submodule-deleted-upstream>
```

## 2. Fork patches that are NOT conflicts

These auto-merge and then break silently. Check each one after every merge:

- `valhalla/midgard/logging.h` — upstream logs through `std::format`, which Apple gates behind an
  **iOS 16.3** availability annotation. Replaced with a `{}`-only shim. Also `src/midgard/logging.cc`
  (`std::format_to` with chrono → `strftime`) and `src/loki/worker.cc` (two `std::format` calls).
  **Delete the shim and restore upstream once the iOS floor reaches 16.3.**
- `src/baldr/curler.cc` — the no-CURL path is upstream's own (`ENABLE_HTTP` off by default), no
  patch needed any more. `curl_tilegetter.h` is header-only and compiles without libcurl.
- `HAVE_FILESYSTEM` guards — deleted in the 3.8.3 merge. Upstream now uses `std::filesystem`
  everywhere, so the guards would compile out code the rest of the tree calls.

## 3. Regenerate the protos

The `.pb.cc`/`.pb.h` in `libs-external/valhalla/proto/valhalla/proto/` are checked in and **must be
generated with a protoc matching the vendored runtime** (`libs-external/protobuf`, currently
3.20.3). Homebrew's protoc is far newer and produces code the runtime cannot compile.

```bash
cmake -S libs-external/protobuf/protobuf/cmake -B /tmp/protoc-build \
  -DCMAKE_BUILD_TYPE=Release -Dprotobuf_BUILD_TESTS=OFF
cmake --build /tmp/protoc-build --target protoc -j 8      # ~2 min

cd libs-external/valhalla
/tmp/protoc-build/protoc --proto_path=valhalla/proto/descriptors \
  --cpp_out=proto/valhalla/proto valhalla/proto/descriptors/*.proto
```

The source of truth is `proto/descriptors/*.proto` — there is no `proto/*.proto`.
`fileformat`/`osmformat` are OSM-PBF only (mjolnir) and are not generated.

## 4. Regenerate `locales.h`

`libs-external/valhalla/config/valhalla/odin/locales.h` holds **en-US only** (~410 KB). Generating
all 34 locales produces a 14.6 MB header and ~2.5 MB of binary; the fork ships one locale and apps
add the others at runtime through `Options.customLocales`.

```bash
cd libs-external/valhalla/valhalla
mkdir -p /tmp/onelocale && cp locales/en-US.json /tmp/onelocale/
cmake -DMSVC= -P cmake/ValhallaBin2Header.cmake /tmp/onelocale/ \
  ../config/valhalla/odin/locales.h --locales
```

## 5. The timezone database

Upstream compiles `src/baldr/tz_alt.cpp` — its own copy of date's `tz.cpp` with the IANA database
embedded as generated headers. That is what makes time-dependent routing work with no filesystem,
no shell and no download, and it replaces the fork's old hand-vendored `libs-external/date/src/`.
**This is the piece whose absence crashed routing in production in 2024.**

```bash
cd libs-external/valhalla/valhalla
(cd third_party/tz && make leapseconds)        # awk, not in the tz repo
for f in africa antarctica asia australasia backward etcetera europe \
         northamerica southamerica leapseconds; do
  cmake -DMSVC= -P cmake/ValhallaBin2Header.cmake third_party/tz/$f \
    ../config/tzdb/date_time_$f.h --variable-name date_time_$f --skip 1 --raw
done
cmake -DMSVC= -P cmake/ValhallaBin2Header.cmake date_time/windowsZones.xml \
  ../config/tzdb/date_time_windows_zones.h --variable-name date_time_windows_zones_xml --skip 1 --raw
python3 ../config/tzdb/strip-comments.py
```

`strip-comments.py` drops the comment and blank lines the tz parser skips anyway, and asserts the
parser-visible lines are unchanged: 806 KB → 166 KB of embedded data (`libs-external` `e3b028a`,
~640 KB per ABI). Skip it and the next tzdata bump silently puts that back. The old hand-rolled
`tz_data.h` was 220 KB. `date_time_windows_zones.h` is only used under
`_WIN32`; it has internal linkage so it costs nothing elsewhere.

`HAS_REMOTE_API=0 AUTO_DOWNLOAD=0 USE_SHELL_API=0` must be defined for the whole valhalla target
(`libs-external/valhalla/CMakeLists.txt`) or `tz_alt.cpp` pulls in `curl/curl.h`.

Verify without a device — this is the exact code path that used to crash:

```bash
V=libs-external/valhalla
clang++ -std=c++20 -DHAS_REMOTE_API=0 -DAUTO_DOWNLOAD=0 -DUSE_SHELL_API=0 \
  -I$V/valhalla/third_party/date/include -I$V/config/tzdb \
  test.cpp $V/valhalla/src/baldr/tz_alt.cpp -o /tmp/tztest
```

with a `main` that calls `date::get_tzdb()` and `locate_zone("Europe/Paris")`. Add
`src/baldr/datetime.cc` + `src/midgard/logging.cc` to also exercise
`valhalla::baldr::DateTime::get_tz_db()`, which throws when the compiled-in tz map and the embedded
database disagree.

## 6. `date` itself

`date` is **headers only** and comes from `libs-external/valhalla/valhalla/third_party/date` — the
commit upstream valhalla pins. It is no longer a subproject in `scripts/routing/CMakeLists.txt` or
`scripts/build/CMakeLists.txt`, and `libs-external/date/` is dead (2018 sources; delete when
convenient). The old copy hardcoded `!defined(__APPLE__)` around `std::uncaught_exceptions()`,
which C++20 broke; upstream's `HAS_UNCAUGHT_EXCEPTIONS` has no such carve-out.

## 7. The default config

`all/native/assets/ValhallaDefaultConfig.h` and `routing-lib/assets/ValhallaDefaultConfig.h` are
our own JSON, not upstream's. A new key that a worker reads with `get_child()` — no default — is a
**runtime** failure, invisible to any compile check:

```
java.lang.RuntimeException: Exception in callRaw(route):
No such node (loki.service_defaults.mvt_min_zoom_road_class)
```

3.8.3 added the `/tile` (MVT) defaults, needed even though `tile_action.cc` is not compiled — the
`loki_worker_t` constructor parses them unconditionally:

```json
"mvt_min_zoom_road_class": [7, 7, 8, 11, 11, 12, 13, 14],
"mvt_cache_min_zoom": 11,
"mvt_max_age": "1800"
```

Exactly 8 zoom values, ascending, or the constructor throws. Boost's ptree stores them as strings
either way, so quoting them is equivalent; the unquoted form above matches upstream's
`scripts/valhalla_build_config` and is what was verified.

To find the next one before a user does, diff the keys the code demands against the keys we ship:

```bash
cd libs-external/valhalla/valhalla
grep -rhoE '(config|pt)\.get<[^>]*>\("[a-z_.]+"\)' src/loki src/thor src/odin src/worker.cc \
  src/meili src/sif src/baldr | grep -o '"[a-z_.]*"' | tr -d '"' | sort -u
grep -rn 'get_child("' src/loki/worker.cc src/thor/worker.cc src/worker.cc
```

`get<T>(path, default)` calls are safe; `get<T>(path)` and `get_child(path)` are not.

## 8. Wrapper and floors

`libs-external/valhalla/CMakeLists.txt`:

- add new non-`*.cc` sources by hand (`src/baldr/tz_alt.cpp`)
- `src/exceptions.cc` is a separate TU since 3.6 (was inside `worker.cc`)
- exclude what drags new dependencies: `src/loki/tile_action.cc` (vtzero + protozero)
- include dirs must cover `config/tzdb`, `third_party/date/include`,
  `third_party/unordered_dense/include`
- bump `config/valhalla/valhalla.h` to the new version

Platform floors, raised for 3.8.3:

| | Floor | Why |
|---|---|---|
| iOS | **13.0** (`build-ios.py`, `build-routing-ios.py`) | `std::filesystem` |
| iOS, if the logging shim is dropped | 16.3 | `std::format` → `std::to_chars` for floats |
| Android NDK | **27** | `std::format`, and libc++ `std::filesystem` |

## 9. Verify

Cheapest first — a full build is not needed to find the breakage:

1. Syntax-check every TU the wrapper compiles, for both platforms. 135 files, ~2 min at `-P 8`:
   `clang++ -fsyntax-only -std=c++20 -target arm64-apple-ios13.0-simulator` and the NDK 27
   `--target=aarch64-linux-android21`, with the wrapper's include set and
   `-DRAPIDJSON_HAS_STDSTRING=1 -DHAS_REMOTE_API=0 -DAUTO_DOWNLOAD=0 -DUSE_SHELL_API=0`.
2. Syntax-check the SDK consumers with `-D_MASSIF_VALHALLA_ROUTING_SUPPORT` — without that define
   the whole Valhalla include block in `ValhallaRoutingProxy.cpp` is skipped and the check proves
   nothing. `all/native/routing/**` and `routing-lib/native/routing/**`.
3. tz host test ([§5](#5-the-timezone-database)).
4. Real builds:
   ```bash
   cd scripts && python3 build-routing-ios.py --ios-arch arm64-simulator --configuration Release
   ANDROID_HOME=~/Library/Android/sdk ANDROID_NDK_HOME=$ANDROID_HOME/ndk/27.3.13750724 \
     python3 build-routing-android.py --android-abi arm64-v8a --configuration Release \
     --gradle $PWD/routing-android/gradlew
   ```
   Both currently fail **after** the library links, in packaging only (iOS: `ditto` looks for a
   `.framework` the xcframework flow does not produce; Android: gradle cannot create its jar cache
   in a fresh `--gradle-user-home`). Check for `ARCHIVE SUCCEEDED` / `Linking CXX shared library`.
5. End-to-end: the demo's **offline** routing test. It needs the `.vtiles` package on the device
   (`DemoConfig.ROUTING_VTILES_NAME`); the online test exercises none of this code (plain HTTP POST).
   ```bash
   cd scripts/android-dev && ./gradlew :app:assembleDebug -x lint      # ~5 min, native included
   adb install -r -t app/build/outputs/apk/debug/app-debug.apk
   adb shell am start -n com.massifmaps.MassifDemo/.MainActivity
   ```
   Driving the panel from adb beats hunting pixels: tap the ⚙, type in "filter settings", then tap
   the one button left. `uiautomator dump /sdcard/ui.xml` gives the exact bounds of every control.
   Success looks like `I DemoTests: route drawn (536 points, 39 maneuver arrows)` in logcat — that
   request also carries a `fr-FR` language and a `non_network_penalty` costing option, so it covers
   the locales header and the fork's proto field in one shot.

   3.8.3 was verified this way on an emulator (2026-08-15). A crash or a `callRaw` exception here,
   with everything above green, almost always means a missing config key ([§7](#7-the-default-config)).
