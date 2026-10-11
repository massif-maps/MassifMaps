---
title: 3D models
description: "glTF models placed at the points of a vector tile layer and styled in CartoCSS: trees, wind turbines, landmarks - lit, shadowed, fogged and draped on the terrain like the buildings beside them."
sidebar_position: 29
---

# 3D models from a data source

A `model` rule draws a glTF binary model (`.glb`) at every point of a vector tile layer — an MVT
source, a GeoJSON source, anything a style reads. The models are instanced on the GPU, lit by the
same sun as the buildings, cast and receive the terrain shadows, fog with the distance, and stand on
the 3D terrain.

```css
#poi[class='tree'][zoom >= 15] {
  model-file: step(random(0, 3), (0, url('models/tree-spreading.glb')),
                   (1, url('models/tree-layered.glb')), (2, url('models/tree-clumped.glb')));
  model-scale: random(0.85, 1.15);
  model-rotation: random(0, 360);
  model-color: #6b8f3a;
  model-color-mix: random(0, 0.25);
  model-opacity: linear([view::zoom], (15, 0), (16, 1));
}

#poi[class='wind_turbine'][zoom >= 13] {
  ::tower { model-file: url('models/wind-turbine-tower.glb'); }
  ::rotor {
    model-file: url('models/wind-turbine-rotor.glb');
    model-z-offset: 81.4;
    model-spin: 14;          // revolutions per minute
    model-spin-axis: y;
  }
}
```

`random()` is evaluated **per point**: a tileset that packs every tree of a tile into one MultiPoint
still gets a different model, size and heading for each tree, stable across zoom levels. See
[Per-feature randomness](./random-expression.md).

## Properties

| Property | Value | Default | Per point |
| --- | --- | --- | --- |
| `model-file` | `.glb` path or URL, relative to the style | — | yes |
| `model-scale` | uniform scale | `1` | yes |
| `model-rotation` | heading in degrees, clockwise from north | `0` | yes |
| `model-z-offset` | metres above the ground | `0` | yes |
| `model-color` | tint colour | `#ffffff` | yes |
| `model-color-mix` | `0` keeps the model's colours, `1` replaces them by `model-color` | `0` | yes |
| `model-opacity` | opacity; may follow `[view::zoom]` for a fade-in | `1` | no |
| `model-cast-shadows` | cast shadows on the terrain and on buildings | `true` | no |
| `model-spin` | spin in revolutions per minute about `model-spin-axis` | `0` | no |
| `model-spin-axis` | `x`, `y` or `z`, the model's own axis through its origin | `y` | no |

The generated [CartoCSS property reference](./cartocss-properties.md) lists them too.

## The model file

- **glTF 2.0 binary** (`.glb`), metres, Y up as glTF specifies; the origin is the point placed on the
  ground. Node transforms are flattened at load.
- **Materials**: base colour factor and texture (PNG/JPEG embedded in the file), vertex colours,
  `alphaMode` `MASK` (cut-outs) and `doubleSided`. PBR metal/roughness is read but lit by the map's
  own sun model, so a model looks like the buildings around it rather than like a PBR viewer.
- **Not supported**: skinning, glTF animations, morph targets, Draco and meshopt compression, external
  buffers. A file asking for one of those in `extensionsRequired` is refused with a log line.
- Models without normals are shaded flat, as glTF asks.

A spinning part (a wind turbine's rotor) is its own model with its origin on the spin axis, placed
over the tower with `model-z-offset`. The map keeps redrawing while a spinning model is on screen;
`Options.modelAnimationsEnabled = false` freezes them, for battery-bound or e-ink apps (in the demo
app: `--es apiSet modelAnimationsEnabled=false`).

## Limits

- Points only: a line or polygon feature is skipped with a warning.
- Not drawn in globe mode yet.
- Translucent materials blend without depth sorting.
- Each model is one draw call per mesh **per tile**, so the cost grows with the number of distinct
  models on screen, not with the number of trees.

Internals: [3D models](../internals/rendering/19-models.md).
