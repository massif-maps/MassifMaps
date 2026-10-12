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

The Massif styles carry these rules behind the `models_3d` parameter, off by default, but not the
`.glb` files: download the five from
[`styles/massif/models`](https://github.com/massif-maps/MassifMaps/tree/master/styles/massif/models),
put them in a `models/` folder at the root of the style package beside `project.json`, and set
`params.models_3d` to 1 on a tileset whose `poi` layer has tree and wind turbine points. The
[`3d-models` example](/examples#3d-models) (Android, iOS, NativeScript, web) turns it on and adds
models of its own: a GeoJSON source of park trees and a spinning wind turbine, its `.glb` files read
by URL.

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
| `model-allow-density-reduction` | thin dense points to one per grid cell (below) | `true` | no |
| `model-allow-overlap` | draw a model even where its crown crosses one already placed (below) | `false` | no |
| `model-max-distance` | metres from the camera past which the model is not drawn (below); `0` for no limit | `0` | no |
| `model-shadow-max-distance` | the same for its shadow, never past `model-max-distance` | `0` | no |

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
app: `--es apiSet modelAnimationsEnabled=false`). A spinning model that casts a shadow redraws its
part of the shadow map every frame, so its shadow turns with it.

`Options.modelShadowsEnabled = false` stops every model casting a shadow, whatever its rule says;
models still receive the buildings' and the terrain's. On a low-end GPU the tree shadows cost more
than the trees (`--es apiSet modelShadowsEnabled=false` in the demo).

## Distance

A tilted view reaches far: trees a few pixels tall and shadows nobody sees still cost their draws.
`model-max-distance` stops drawing a model that far from the camera, and `model-shadow-max-distance`
stops its shadow sooner. Over the last 15% of the distance a model shrinks into the ground, each
point at its own distance so they do not all go on one line (mapbox's building cutoff does the same);
past it, its tiles are skipped whole, so the saving is real draw calls and shadow-pass work. A
spinning model past its shadow distance no longer redraws the shadow map every frame. Taps ignore
models past their distance.

```css
#trees { model-max-distance: 1500; model-shadow-max-distance: 600; }
```

## Density, fade and detail by zoom

- **Density**: as mapbox does for its trees, each tile keeps one instance per cell of a grid shared
  by every model rule of the layer — 100 cells a side up to z14, 256 at z15, no thinning from z16
  (mapbox's table for a z14 source). `model-allow-density-reduction: false` keeps every point.
- **Overlap**: as `marker-allow-overlap` does for icons, models do not cross. The crown is 3/4 of the
  disc inscribed in the model's box, seen from above, times `model-scale`, in metres, so neighbours
  may cross a little. Of two crossing
  crowns the one ranked higher stays; the rank is drawn from the point's position, so it is the same
  at every zoom, and zooming in keeps every tree where it was, only adding those the density grid had
  thinned. A crown can lose to a neighbour that itself lost, so a dense row keeps fewer trees than a
  first-come rule would. The models of one point (a tower and its rotor) never collide.
  `model-allow-overlap: true` places every point. Crowns wider than 16 m in radius may differ
  between zooms near tile edges.
- **Fade-in**: give `model-opacity` a zoom ramp, `linear([view::zoom], (15, 0), (16, 1))`.
- **Detail by zoom**: pick a lighter file at lower zooms with `step([zoom], ...)` in `model-file`;
  the choice is made per tile, at its zoom.

## Clicking and highlighting a model

A click on a model fires the layer's `vectortile.clicked` event like any other feature, with the
feature that placed it and, for a MultiPoint, the index of the point that was hit
(`VectorTileClickInfo.getFeaturePosIndex()`). The test is the model's bounding box, turned and
scaled as drawn and standing on the terrain; a spinning part is tested against every box its spin
sweeps.

Hand the same two values back to draw that one instance in a highlight colour:

```java
layer.setVectorTileEventListener(new VectorTileEventListener() {
    @Override
    public boolean onVectorTileClicked(VectorTileClickInfo info) {
        layer.setModelHighlight(info.getFeatureId(), info.getFeaturePosIndex(), new Color(0xffff8800));
        return true;
    }
});
// later
layer.clearModelHighlight();
```

The colour's alpha is how much of the model's own colour it replaces. Through the facade API:
`call(layer, "setModelHighlight", [featureId, featurePosIndex, 0xffff8800])` and
`call(layer, "clearModelHighlight", [])`. A highlight picks the instance by feature id and point
index, so it singles out one instance only where the source gives its features distinct ids.

## Limits

- Points only: a line or polygon feature is skipped with a warning.
- Not drawn in globe mode yet.
- Translucent materials blend without depth sorting.
- Each model is one draw call per mesh **per tile**, so the cost grows with the number of distinct
  models on screen, not with the number of trees.

Internals: [3D models](../internals/rendering/19-models.md).
