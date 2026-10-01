---
title: Legends
description: "A map legend resolved from the live CartoCSS style: colours, widths and icons follow the style parameters."
sidebar_position: 27
mdx:
  format: mdx
---

import LiveExample from '@site/src/components/LiveExample';

# Legends

A legend lists what the map draws: a motorway, a T4 trail, a lake, a peak, a road number. A legend
with hard-coded colours breaks as soon as the app recolours the style through a parameter, or a user
overrides a `.mss` file. So a Massif legend holds no colours.

Instead, it is a **spec** of synthetic features. The SDK resolves each one against the compiled
style, the same way it decodes a real feature: it evaluates the rules and parameters and finds the
draw order across every attachment. What comes back is the **swatch** the map draws that feature
with. This works for any CartoCSS style.

<LiveExample id="map-legend" />

## The spec — `legend.json`

Ship it beside the style's `project.json`. The SDK and `massif-style legend` read it from there when
they are given no spec of their own.

```json
{
  "title": { "en": "Massif Outdoor", "fr": "Massif Randonnée" },
  "zoom": 15,
  "sections": [
    { "id": "trails", "label": "Hiking difficulty (SAC scale)", "items": [
      { "id": "sac-t2", "label": "T2 mountain hiking", "layer": "transportation", "geometry": "line",
        "properties": { "class": "path", "sac_scale": "mountain_hiking" } }
    ]},
    { "id": "mtb", "label": "Mountain bike difficulty", "items": [
      { "id": "mtb-hard", "label": "MTB 3+", "layer": "transportation", "geometry": "line",
        "attachment": "^mtb", "properties": { "class": "path", "mtb_scale": 4 } }
    ]}
  ]
}
```

| Key | Meaning |
|---|---|
| `layer` | the vector-tile **source layer**, the CartoCSS `#selector` |
| `geometry` | `point`, `line` or `polygon`, which is what `[mapnik::geometry_type]` filters read |
| `properties` | the feature's fields, as the tiles would carry them |
| `attachment` | optional regex over the CartoCSS attachment (`::name`). Only matching attachments count, so an overlay such as MTB is shown without the path drawn under it |
| `zoom` | the view zoom the item is resolved at. An item without one uses its section's, then the spec's |

Every other key passes through untouched: `title`, section and item `id` and `label` (a string or a
`{lang: text}` object), and any key of the app's own.

## The legend

```json
{ "id": "sac-t2", "label": "T2 mountain hiking", "kind": "line",
  "lines": [ { "color": "#ffffff", "width": 3, "opacity": 0.8 },
             { "color": "#c0392b", "width": 1.5, "dasharray": [4, 2] } ] }
{ "id": "wood", "label": "Forest", "kind": "fill", "color": "#c8e6b0", "pattern": "icons/forest.svg" }
{ "id": "spring", "label": "Spring", "kind": "poi", "marker": { "color": "#1e88e5", "size": 7, "border": "#ffffff", "borderWidth": 1.5 } }
{ "id": "n7", "label": "National road", "kind": "shield",
  "text": { "value": "N 7", "color": "#ffffff" }, "plate": { "color": "#c0392b", "border": "#ffffff", "borderWidth": 1.5, "radius": 3 } }
```

| `kind` | Swatch |
|---|---|
| `line` | `lines`, **bottom to top**: a casing or halo is simply an earlier line. Each has `color`, `width`, and optionally `dasharray`, `offset` and `opacity`, or a `pattern` image |
| `fill` | `color` and `opacity`, a `pattern` image, and an `outline` line |
| `poi` | an `icon` image (with `iconColor` when the style tints it), a `marker` circle, or a `glyph` of an icon font |
| `shield` | a road number: its `text` on an `icon` image, or on a `plate` with `color`, `border`, `borderWidth` and `radius` |
| `label` | `text` only: `value`, `color`, and optionally `halo` and `haloWidth` |

- **Units and formats.** Widths are the style's own pixels at the item's zoom. Colours are CSS
  `#rrggbb`, or `#rrggbbaa` when not opaque. Images are paths in the style's asset package.
- **Hidden items are dropped.** An item the style does not draw with the current parameters is left
  out, and so is a section left empty. With the MTB overlay off, its section is gone.

## From an app

```java
String legend = decoder.getLegend("");        // the legend.json the style ships
String custom  = decoder.getLegend(specJson);  // any spec
```

`getLegend` reads the decoder's **live** parameter values: call it again after `setStyleParameter`
and the swatches follow. The result is empty when the style ships no `legend.json`, and a malformed
spec throws.

:::note Surface API
`getLegend` is a method of the `style` object. The spec is optional; without one, the result is
`null` when the style ships no legend.

```js
const legend = massif.call(massif.find('style', 'base'), 'getLegend', [spec]);
```
:::

## From the command line

```sh
massif-style legend project.json --params variant=eink --svg legend.svg
```

This runs the same resolver through WebAssembly and prints the legend. `--svg` also draws it as a
sheet, with the images inlined; see [style tools](../contributing/style-tools.md).
