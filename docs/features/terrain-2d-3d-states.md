---
title: 2D, 3D and everything between
description: "Every state a terrain map can be in - off, flat, flat and ready, 3D, 3D at zero relief - how to set each one, what it costs, and which to pick"
sidebar_position: 1.5
---

# 2D, 3D and everything between

A map with [terrain](/docs/features/3d-terrain) can be drawn flat or in 3D, and the SDK has several
ways to be flat. They look the same and cost differently. This page lists every state, how to get
into it, how to move between two of them, and what each is good for.

Numbers are from a Crosscall HLTE556N (Adreno 610) with the Massif style over Rhône-Alpes data and
extruded buildings, three runs per state. The device speed drifts, so read them as ratios.

## The switches

Three settings decide the state. You set the first three; the SDK derives the rest.

| Setting | Values | Decides |
|---|---|---|
| `Enabled` | `true` / `false` | Whether terrain exists at all. Off means no elevation data and no terrain code anywhere. |
| `Flattened` | `true` / `false` | Whether the terrain is **drawn** right now. The map eases between the two over `AutoFlattenDuration`. The auto-flatten rule ([see the terrain page](/docs/features/3d-terrain#rendering-flat-when-3d-buys-nothing)) writes this same setting. |
| `FlattenMode` | `RENDER` (default) / `FULL` | What a **flat** map keeps. `RENDER` keeps everything 3D needs, so switching back is free. `FULL` throws it away and the flat map is a plain 2D one. |
| `AlwaysDecode` | `false` (default) / `true` | Whether a **disabled** terrain still keeps its tiles ready, so `Enabled` can be switched without re-decoding. |
| `Exaggeration` | `0` … | Height scale. `0` is a flat ground that is still drawn as 3D. |

"Decoded for terrain" below means the tiles were built with the terrain tile set and the
geometry carries the terrain subdivision, with the elevation data warmed while they decode. A tile decoded for terrain
draws fine flat. A tile decoded plain does **not** draw right in 3D (a road chords straight between
its ends and floats over a valley), so the SDK re-decodes whenever it has to go from plain to
terrain. That re-decode is what every cost below is about.

## The states

| State | You set | Looks | Tiles | Building shadows | Cost against *Terrain off* |
|---|---|---|---|---|---|
| **Terrain off** | `Enabled false` (or no `TerrainOptions`) | 2D | plain | no | the baseline |
| **Flat, ready** | `Enabled true`, `Flattened true`, `RENDER` | 2D | for terrain | no | none measured: same tiles, bytes and frame time |
| **Flat, plain** | `Enabled true`, `Flattened true`, `FULL` | 2D | plain | no | not measured; the tiles are the baseline's by design |
| **3D** | `Enabled true`, `Flattened false` | terrain, drape | for terrain | yes | 27 to 47% more tiles, up to 35% more bytes, frame time +30 to +37% |
| **3D, zero relief** | as 3D, `Exaggeration 0` | flat ground, drawn as 3D | for terrain | yes | about +5 ms a frame at tilt 90 (+17%) |
| **Terrain off, ready** | `Enabled false`, `AlwaysDecode true` | 2D | for terrain | no | about +2% frame time, plus the elevation fetches |

A few things the table hides:

- **Shadows need the terrain pass.** Terrain shadows and building shadows are drawn by the 3D
  pipeline, so every flat state loses them, and so does the animation between flat and 3D. If you
  need building shadows on a map that looks flat, use *3D, zero relief*.
- **Auto-flatten writes `Flattened`.** At tilt 88° and above the SDK flattens by itself, so with the
  default `RENDER` you land in *Flat, ready* and the shadows go with it. Set
  `AutoFlattenTilt` to `0` to keep 3D at any tilt.
- **`AlwaysDecode` does not touch `FULL`.** In `FULL` a flat map is plain by definition; the option
  only covers a disabled terrain.

## Moving between states

| From → to | Re-decodes | What you see |
|---|---|---|
| *Flat, ready* ↔ *3D* (`Flattened`) | nothing | one ramp, `AutoFlattenDuration` long. Rising, only about 8 to 10 new tiles load, in about 2 s |
| *Flat, plain* ↔ *3D* (`Flattened`, `FULL`) | the visible tiles, the first time each way | the map stays flat until the 3D tiles are ready, then ramps. A repeat switch at the same view reuses the tiles it kept aside |
| *Terrain off* → *3D* (`Enabled`) | every visible tile | 3D draws at once on tiles that are not ready for it for about 5 s (31 tiles at tilt 50) |
| *Terrain off, ready* → *3D* (`Enabled`) | nothing but the tiles the wider 3D view adds | about 2 s, the same as flat-ready |
| *3D* → *Terrain off* | every visible tile | invisible if you flatten first, because the re-decode happens while the map is already flat |

`TerrainOptions.isSwitching()` is true while a rise waits for its tiles.

## Which one to use

- **A 2D map that never goes 3D**: no `TerrainOptions` at all. Nothing else comes cheaper.
- **A map with a 2D/3D button, or one that flattens itself far out and tilts back in**: *Flat, ready*.
  It costs what *Terrain off* costs and switches for free. This is the default, and the reason
  `RENDER` is the default.
- **A map that is flat most of the time and 3D rarely, where the flat state should be as cheap as
  possible**: *Flat, plain* (`FULL`). You pay one re-decode, hidden behind the wait, each time you go to
  3D for the first time at a view. The gallery's *2D/3D switch* example uses it.
- **An app that turns terrain on and off with `Enabled`** (a settings toggle, a layer switcher): set
  `AlwaysDecode`. Without it the first `Enabled` turns 3D on over tiles that are not ready for it.
  It is the only state that pays in 2D for something you may never use, so leave it off if the toggle
  is rare.
- **Buildings, models and their shadows on a map that looks flat**: *3D, zero relief*.
- **Straight-down maps with no shadows wanted**: leave auto-flatten on.

## Code

```java
TerrainOptions terrain = new TerrainOptions(demSource);
mapView.getOptions().setTerrainOptions(terrain);

// Flat, ready: attached and enabled, drawn flat until you ask for 3D.
terrain.setFlattened(true);                    // before the first layer decodes: nothing is built for a 3D not yet shown
terrain.setFlattened(false);                   // the button: ramps up, no re-decode

// Flat, plain: the flat map is a plain 2D one.
terrain.setFlattenMode(TerrainFlattenMode.TERRAIN_FLATTEN_MODE_FULL);

// An Enabled toggle that never re-decodes.
terrain.setAlwaysDecode(true);
terrain.setEnabled(false);                     // later: setEnabled(true)

// 3D at zero relief: flat ground, shadows kept.
terrain.setExaggeration(0f);
```

```js
const terrain = map.terrain();
terrain.set('flattened', true);
terrain.set('flattenMode', 'TERRAIN_FLATTEN_MODE_FULL');
terrain.set('alwaysDecode', true);
terrain.set('exaggeration', 0);
```

## Measured

Grenoble z16.2, tilt 26, Chamonix z14.2, tilt 40 and Grenoble z16.3, tilt 90, panning; frame time in
ms; the same run of each pair side by side because the device drifts between runs.

| | Terrain off | Flat, ready | 3D | 3D, zero relief |
|---|---|---|---|---|
| Grenoble tilt 26, tiles decoded | 189 | 190 | 240 | not measured |
| Chamonix tilt 40, tiles decoded | 49 | 49 | 72 | not measured |
| Grenoble tilt 90, tiles decoded | 66 | 67 | 89 | 85 |
| Grenoble tilt 90, frame | 30.8 | 31.0 | 42.2 | 36.0 |

The full table, with the method, is in the
[terrain internals](/docs/internals/rendering/terrain).
