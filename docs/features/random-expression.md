---
title: Per-feature randomness
description: "random(min, max) in a CartoCSS expression: a value that varies between features but never between frames, tiles or zoom levels."
sidebar_position: 28
---

# Per-feature randomness: `random()`

`random(min, max)` returns a number in `[min, max)` that differs from one feature to the next and is
the same every time the same feature is drawn — across frames, tiles and zoom levels. It is
mapbox's `["random", min, max, seed]`, and serves the same purpose: varying a size, a heading or a
tint so that a row of identical symbols stops looking stamped.

```css
#poi[class='tree'] {
  model-file: step(random(0, 3), (0, url('tree-spreading.glb')), (1, url('tree-layered.glb')), (2, url('tree-clumped.glb')));
  model-rotation: random(0, 360);
  model-scale: random(0.85, 1.15);
}
```

## What it is seeded with

| Call | Seed |
| --- | --- |
| `random(min, max)` | the feature id; in a `model` rule, each placed point (below) |
| `random(min, max, [field])` | the field's value — numbers as they are, strings hashed |
| `random(min, max, 7)` | the constant: the same value everywhere |

The default seed is the variable `[mapnik::seed]`, which a style can also read directly.

A tileset may pack many points into one MultiPoint feature — OpenMapTiles-style trees and wind
turbines are one feature per tile. Seeded by the feature id, every point of it would draw the same
value. A [`model`](./3d-models.md) rule therefore seeds each point from its own position, so each
tree in the pack gets its own model, size and heading, and keeps them when the tile is re-decoded at
another zoom.

Two calls with the same bounds and seed return the same value. To draw two independent values for
one feature, give them different seeds: `random(0, 360, [mapnik::seed] + 1)`.
