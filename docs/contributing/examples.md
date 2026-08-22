---
title: Adding an example
description: "How the Android example gallery, the screenshots and the website page are generated from one file"
sidebar_position: 3
---

# Adding an example

An example is **one Java file** in the Android demo app. Adding it puts it in the app's gallery
*and* on [the website's examples page](https://massif-maps.github.io/MassifMaps/examples) — nothing
else has a list to update.

Examples are written against the **facade API** (`com.massifmaps.api`), never the object API. That
is the point of them: they are the reference for what an app should look like, and the facade is
the API the SDK is moving to ([`api-facade.md`](../internals/api-facade.md)).

## The file

`scripts/android-dev/app/src/main/java/com/massifmaps/MassifDemo/examples/<section>/XxxExample.java`

```java
@ExampleInfo(
    id = "display-a-map",              // kebab-case, unique, names the screenshot and the URL
    title = "Display a map",
    description = "One raster layer from one spec, and a camera pointed at it.",
    section = Sections.BASICS,         // must be a constant from Sections.ALL
    order = 10)                        // position within the section
public class DisplayMapExample extends MapExample {

    @Override
    public void onStart(ExampleHost host) {
        MassifMap map = host.map();
        map.addLayer("basemap", Spec.of("raster")
            .set("source", Spec.of("http").set("url", "https://…/{z}/{x}/{y}.png")));
        map.camera().moveTo(new MapPos(6.8652, 45.8326), 11);
        host.caption("What to look at.");
    }
}
```

Rules that matter:

- **`onStart` runs on a WORKER thread**, not the UI one — building a layer decodes its style, and a
  real style project takes seconds. Every `ExampleHost` method is safe to call from there.
- **The example owns no Android.** Buttons, toggles, captions and toasts all come from
  `ExampleHost`, so the file reads as map code. That is what makes it usable as documentation.
- **Ids are global per kind.** The map, and everything built through it, is released when the
  screen closes, so ids may be reused between examples — but not within one.
- The section must exist in `Sections.ALL`. An unknown one is reported by the generator rather
  than silently dropped.

## Generating

```bash
python3 scripts/gen-examples.py
```

Writes `ExampleRegistry.java` (the ordered class list the gallery iterates) and
`docs/examples/examples.json` (the manifest the website builds from, including each example's
source). The gradle build runs it, so a plain `./gradlew :app:assembleDebug` is usually enough.

It reports what it could not do — an unknown section, a duplicate id, a file that extends
`MapExample` but carries no annotation, and every example still missing a screenshot.

## Screenshots

```bash
python3 scripts/capture-examples.py                    # every example
python3 scripts/capture-examples.py terrain-3d markers # only these
python3 scripts/gen-examples.py                        # record them in the manifest
```

They land in `docs/examples/screenshots/<id>.png` — **one home**, shipped inside the APK as an
asset for the gallery grid and read from the same place by the website.

The script turns the device to **landscape**, hides the status bar, and launches the example with
`--es ui false` so no app chrome is in the picture. See
[the screenshot rules](#composing-a-screenshot) below before capturing.

## Composing a screenshot

The vignette is a wide rectangle in a grid, so:

- **Compose in the middle of the frame.** The stored file is the vignette; a subject drifting to
  the top is cropped out.
- **Pick a real place, and the camera that flatters it.** A default camera over an arbitrary city
  is what makes a gallery look unfinished.
- **Show the capability, not just the pixels.** The 3D terrain example is satellite imagery draped
  on the mesh *with roads and summit labels over it*, because that is what the SDK can do that a
  picture of a mountain cannot say.
- **Iterate with intent extras rather than rebuilds:**

  ```bash
  adb shell am start -n com.massifmaps.MassifDemo/.ExampleActivity \
      --es example terrain-3d --es ui false \
      --es lat 45.9650 --es zoom 11.5 --es tilt 28 --es rotation 180
  ```

  The activity logs the camera it actually ended up at (`camera lon=… lat=…`) on every start —
  **read it**. A dark frame that looked like broken shadows turned out to be the open Atlantic.
  Once it looks right, put the numbers back in the example's own `moveTo`.

Terrain gotchas, both hit while composing the Matterhorn view: a high tilt buries a peak in the
ridge behind it, and a close zoom puts the camera *inside* the slope, because the terrain keeps a
clearance above the ground.

## Where things live

| Path | What |
|---|---|
| `.../examples/<section>/XxxExample.java` | the example |
| `.../examples/Sections.java` | the section list and its order |
| `.../examples/ExampleRegistry.java` | **generated** |
| `docs/examples/examples.json` | **generated** manifest, read by the website |
| `docs/examples/screenshots/` | the captures, shared by the app and the site |
| `app/src/main/style-projects/<name>/` | CartoCSS style projects, zipped into the APK by gradle |
| `website/src/pages/examples.js` | the published gallery |

## Known gaps

- The gallery is Android only; iOS has no example app yet
  ([#154](https://github.com/massif-maps/MassifMaps/issues/154)).
- Screenshots are captured on an emulator. Text rendering and imagery differ slightly on a device.
- There is no check that an example still runs — a broken one shows an empty map and a toast.
