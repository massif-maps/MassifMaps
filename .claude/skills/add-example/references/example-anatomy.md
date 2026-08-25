# What an example is made of

Reference for the [`add-example`](../SKILL.md) skill. Everything here is the shape of an existing,
working example — copy from one rather than inventing.

## The three files

| Platform | Path | Registered by |
|---|---|---|
| Android | `scripts/android-dev/.../examples/<section>/<Name>Example.java` | `gen-examples.py` → `ExampleRegistry.java` |
| iOS | `scripts/ios-dev/MassifDemo/Examples/MSF<Name>Example.m` | nothing — the catalogue enumerates classes at runtime |
| NativeScript | `integrations/nativescript/demo-snippets/svelte/examples/<Name>.svelte` | `scripts/examples/index.mjs` → `generated.ts` |

Closest working set to copy: **`terrain-3d`** (layers, terrain, sky, fog, toggles) and
**`atmosphere`** (a day cycle, a custom shader, several cooperating toggles).

`@ExampleInfo` on the Android class is the only place the title, description, section and order
live. `gen-examples.py` parses it out of the `.java` text and writes `docs/examples/examples.json`,
which the app gallery, the website and the NativeScript grid all read. Nothing else has a list.

## The host

The screen is behind an interface so an example file reads as map code. Same five things
everywhere, spelled per platform:

| | Android | iOS | NativeScript |
|---|---|---|---|
| entry | `onStart(ExampleHost host)` | `-startWithHost:` | `function start(host: ExampleHost)` |
| the map | `host.map()` | `host.map` | `host.map` |
| caption | `host.caption(s)` | `[host caption:s]` | `host.caption(s)` |
| button | `host.button(label, Runnable)` | `[host button:label action:^{}]` | `host.button(label, fn)` |
| toggle | `host.toggle(label, on, OnToggle)` | `[host toggle:label on: action:^(BOOL){}]` | `host.toggle(label, on, fn)` |
| delay | `host.postDelayed(r, ms)` | `[host after:s run:^{}]` | `host.after(ms, fn)` |
| cache dir | `host.cachePath(name)` | `[host cachePath:name]` | `cached()` in `shared.ts` |

Android's `onStart` runs on a **worker** thread (building a layer decodes a style, which would be an
ANR on the UI thread); every host method is safe to call from it. `onStop` / `-stop` is only for
what the host cannot release itself — a timer, a sensor.

## Writing map code: the facade

Examples use the facade API, not the SWIG classes: `map.addLayer(id, spec)`, `map.style(id, spec)`,
`map.terrain(spec)`, `map.sky(spec)`, `map.fog(spec)`, `map.light(spec)`, `map.camera().moveTo(…)`,
`map.set("path", value)`, `group.apply(spec)`.

**The schema is the reference, not guesswork.** `docs/api/massif-api.json` lists every class, every
property with its type, and every spec kind with its keys; `bindings/typescript/massif.d.ts` is the
same thing as typings. One `python3 -c` over the JSON answers "does this property exist and what is
it called" in one step — faster and more reliable than grepping headers.

Notes that have already cost a round:

- **A nested spec is not the top level.** `Spec::create`'s own loop is path-aware, so
  `"metaData.dem_encoding"` works there; a spec built as a CHILD (a layer's `source`, terrain's
  `source`) goes through `applySpecProperties`. Both accept an indexed key now, but the **whole-map
  form is what the typings declare** and what reads best: `metaData: { dem_encoding: 'terrarium' }`.
- **An enum is written by its constant NAME** — `"SKY_TYPE_GRADIENT"`, not `0`. An unknown name is
  refused rather than read as 0.
- **`dem_encoding` picks the elevation decoder**, per tile, out of the source's meta data map.
  Without it the SDK assumes mapbox encoding; mapbox-decoding terrarium tiles gives heights in the
  hundreds of kilometres, and the terrain swallows the camera.
- **A shader property is a whole-block replacement.** `SkyOptions.shaderSource`,
  `FogOptions.shaderSource` and `TerrainOptions.surfaceShaderSource` each declare their uniforms and
  helpers; redeclaring one is a compile error and the renderer falls back silently. None of them may
  fog itself — the SDK applies the frame's fog to whatever they return. Contracts are on the
  setters and in [`docs/features/sky-sun-shadows.md`](../../../../docs/features/sky-sun-shadows.md).

## Remote sources are always cached

Every example that reads from a server puts a `persistent-cache` in front of it. These are other
people's free services, and a demo that gets panned around re-fetches the same tiles every run.

```java
.set("source", Spec.of("persistent-cache")
    .set("databasePath", host.cachePath("osm-raster.db"))
    .set("capacity", 100 * 1024 * 1024)
    .set("source", Spec.of("http").set("url", "…")))
```

Share the database name across examples that use the same server — they then warm each other's
cache. NativeScript centralises this in `demo-snippets/svelte/examples/shared.ts` (`osmRaster`,
`vectorTiles`, `satelliteTiles`, `demTiles`); Android and iOS spell it inline, because an example
file there is read as documentation and an indirection would hide the pattern.

The encoding stays on the **HTTP** source, not on the cache in front of it — a wrapper source with
no map of its own answers with its wrapped source's.

## Screenshots

`scripts/capture-examples.py` writes `docs/examples/screenshots/<id>.png`, which both the gallery
and the website show as a **wide vignette in a grid**. Every rule below was learned the hard way:

- **Centre the subject vertically.** The stored file IS the vignette; anything drifting to the top
  of the frame is cropped out of it.
- **Landscape, no chrome.** The script turns the device and passes `--es ui false`.
- **Pick the place and the camera deliberately.** A default camera over an arbitrary city is what
  makes a gallery look unfinished.
- **READ BACK the camera.** `ExampleActivity` logs `camera lon=… lat=…` on every start. A dark frame
  once read as "the shadows are broken" and was the open Atlantic.
- **Iterate with intent extras, not rebuilds** — `--es lat/lon/zoom/tilt/rotation` on
  `.ExampleActivity` — then put the numbers back into the example's `moveTo`.
- Terrain traps: a high tilt buries a peak in the ridge behind it, and a close zoom puts the camera
  *inside* the slope (the terrain keeps a clearance above the ground). Tilt 90 is straight down, so
  a landscape view is a LOW tilt.

Workflow details: [`docs/contributing/examples.md`](../../../../docs/contributing/examples.md).

## Repos

`integrations/nativescript` is its **own git repository**, like `libs-massif` and `libs-external` —
a change there is a commit and a PR in that repo, on a branch, not on `master`.
