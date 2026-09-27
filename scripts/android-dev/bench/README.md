# Render benchmark scripts

Used for every number in [`docs/internals/performance-log.md`](../../../docs/internals/performance-log.md). They
read the `PROF` / `RenderStats` logcat lines, so build with the profilers on:

```sh
cd scripts/android-dev && ./gradlew :app:assembleDebug -x lint -PprofileRender
```

Set the device first: `export ANDROID_SERIAL=<serial>` (`adb devices -l`).

The map is **`.BenchActivity`**, not `.MainActivity` - `MainActivity` is the example gallery. A script
pointed at the wrong one launches a screen with no map in it and quietly measures nothing, which is
what every script here did until 2026-09-20.

| script | what it does |
|--------|--------------|
| `ab.sh <label> [extras]` | one run at the demo camera, panning east; prints tagged `PROF` lines |
| `ab2.sh <apk> <label> [extras]` | same at the mountain camera |
| `north.sh <label> [extras]` | pans NORTH into the mountains, full stack - the slow case |
| `rotate.sh <label> [extras]` | fast ROTATION at a 3D terrain camera - what renames the drape cover fastest |
| `city3d.sh <label> [extras]` | buildings and cast shadows over terrain, rotating (read the `shadows ACTIVE` line first, see the script) |
| `dem.sh <apk> <label> [extras]` | the DEM pipeline over a whole session, load included |
| `abapk.sh <apk> <label> [extras]` | install an APK then run `ab.sh` - for interleaving two builds |
| `abprop.sh <label> <linesourcedensity> <depthshift> [extras]` | A/B by system property, one APK |
| `startup2.sh <style>` | launch -> first tile request -> first drawn tile, per style source |
| `absum.py` | median summary of tagged `PROF` lines (discards idle windows > 1600 ms) |
| `demsum.py` | per-ENCODE summary of `dem.sh` output - the counters are per-interval sums |

Device numbers drift: the same build measured 14.6-17.4 fps across one morning. Only **interleaved**
comparisons count - alternate two builds and take medians over >= 40 windows:

```sh
{ sh abapk.sh /tmp/a.apk "A"; sh abapk.sh /tmp/b.apk "B"; \
  sh abapk.sh /tmp/a.apk "A"; sh abapk.sh /tmp/b.apk "B"; } | python3 absum.py
```
