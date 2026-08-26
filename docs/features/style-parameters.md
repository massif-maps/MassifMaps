---
title: Live Style Parameters
sidebar_position: 11
---

# Live Style Parameters (`param::`)

A style parameter is a value the **app** owns and the style reads. The fork adds table parameters,
and — the point of this page — makes a colour-only change **repaint instead of re-decoding every
visible tile**.

:::info Fork feature
Table parameters and the repaint paths were added in PRs
[#73](https://github.com/massif-maps/MassifMaps/pull/73) and
[#76](https://github.com/massif-maps/MassifMaps/pull/76).
:::

<figure class="docs-figure">

![One route highlighted by a style parameter](/img/features/style-parameter-selection.jpg)

<figcaption>The demo's selection bench: 12 routes from one GeoJSON source, one of them selected. Setting <code>selected_id</code> changes its colour and width with a repaint — no tile is decoded again.</figcaption>

</figure>

## Declaring and setting

```json
"styleparameters": {
  "show_relief":  { "default": true },
  "lang":         { "default": "en" },
  "routes_type":  [0, 1, 2],
  "poi_colors":   { "default": { "restaurant": "#c0392b", "cafe": "#8e6e53" } },
  "zoom_steps":   { "default": [10, 12, 14] }
}
```

| Form | Meaning |
|---|---|
| `{ "default": <scalar> }` | a bool / integer / float / string parameter |
| `[a, b, c]` | an **enum**: the allowed values, the last one is the default |
| `{ "default": <object \| array> }` | a **table** the style indexes into |

```java
decoder.setStyleParameter("show_relief", "true");
decoder.setStyleParameters(Map.of("lang", "fr", "buildings", "1"));
decoder.setJSONStyleParameters("{\"lang\":\"fr\"}");
```

Through the [surface API](/docs/api/) the same parameters are a **property bag** on
the style — the rest of the path is the parameter's name, and the property itself takes every one
at once:

```java
style.set("params.show_relief", "true");
style.apply(Spec.object().set("params", Spec.object().set("lang", "fr").set("buildings", "1")));
```

A parameter the style does not declare is refused there rather than dropped, which is the one
difference worth knowing: `setStyleParameter` returns `false` for it, and the surface API turns
that into `RESULT_UNKNOWN_PROPERTY`.

Runnable, on three platforms: [the style parameters example](/examples#style-parameters).

## Reading them

Scalars read like any other variable, in an expression or a filter:

```css
#road['param::show_underground' = 1] { line-color: @underground_color; }
#label { text-size: 12 / [param::_fontscale]; }
```

Tables are read with `get` (plus `has` and `length`). One table parameter replaces one parameter per
class, and the app can rewrite the whole table at once:

```css
#poi     { marker-fill: get([param::poi_colors], [class], #888888); }
#contour { line-width: get([param::widths], 0, 0.8); }
```

```java
decoder.setStyleParameter("poi_colors", "{\"restaurant\":\"#c0392b\",\"cafe\":\"#8e6e53\"}");
```

`get(table, key)` takes a member by name from an object or an element by index from an array, and is
unset when the key is missing — so the third argument is what you usually want. `getStyleParameter`
returns a table as JSON.

## What a change costs

Changing a parameter is either a **redraw** or a **re-decode of every visible tile** (~130 ms of CPU
per tile), decided per parameter when the style loads:

- **Redraw** — the parameter is read *only* by properties the renderer evaluates per frame: colours,
  opacities, widths, and only where the expression reads nothing else fixed at decode time.
- **Re-decode** — everything else, deliberately: the parameter appears in a **filter** (it decides
  which geometry the tile contains at all); it feeds a property also read while the tile is built
  (`text-size`, `shield-size`, marker `width`/`height`/`stroke-width`, line `stroke-width`,
  `stroke-miterlimit`); the expression also reads a **feature field** or the zoom; or it is
  `_geometryscale`, `_fontscale` or `_zoomlevelbias`.

So a colour an app exposes as a setting (`"water_color"`) is free to change, while a table read per
feature class still costs a decode.

Which side a given property falls on is in the
[CartoCSS property reference](cartocss-properties.md), one **live** / **baked** column per property.
The decision is made **per parameter across the whole style**, not per use: one baked use anywhere
makes the parameter baked everywhere it appears.

## Selecting one feature without a decode

Highlighting the tapped road is "a parameter compared with a feature field", which is normally the
expensive case. A style can **ask** for it to be free:

```json
"styleparameters": {
  "selected_id": { "default": "", "selects": true }
}
```

```css
@is_selected: [param::selected_id] = [osmid] + '';
#routes {
  line-color: @is_selected ? #ff3b00 : #3388ff;
  line-width: 5 + (@is_selected ? 4 : 0);
}
```

```java
decoder.setStyleParameter("selected_id", Long.toString(osmid));   // no tile is decoded again
```

The decoder folds the comparison both ways at decode time, so a tile carries the selected and the
unselected appearance as two style slots; setting the parameter rewrites one byte per vertex and
redraws.

The rules are narrow, and a style that breaks one falls back to the re-decode path **with a warning
in the log naming the reason** — `selects` never fails silently:

- only `line-color`, `line-opacity` and `line-width` of a **line** rule may read the parameter;
- always as `[param::x] = <expression of feature fields>`, the same expression everywhere, and never
  together with another parameter in one property;
- never in a **filter** — `when (…)::casing` decides whether the casing geometry *exists*, and no
  repaint can build geometry. Write the casing as a width and a colour instead of as a rule;
- not on a **dashed** line whose width is selected: the dash raster is sized by the width.

## The render-mode variable (`render::3d`)

`render::3d` is **not** a style parameter: the SDK owns it, the app cannot set it. It is `true` while
the layer draws on 3D terrain (`TerrainOptions.setEnabled(true)`), `false` otherwise — which lets one
style carry both looks:

```css
#road_label {
  text-name: [name];
  text-orientation-mode: [render::3d] ? billboard-line : line;
  text-size: [render::3d] ? 12 : 11;
}

#building['render::3d' = true] { building-height: [render_height]; }
```

In an expression it is written `[render::3d]`; in a **selector** it must be quoted,
`['render::3d' = true]`, the same as `['param::x' > 0]` (the selector grammar has no `::` in a bare
field name).

It is resolved **at decode time**, like a feature field or the zoom — so it works everywhere,
including on properties no repaint can change (`text-orientation-mode`, `text-name`, marker choice,
filters), and it costs nothing per frame. Toggling terrain therefore re-decodes the tiles, but that
already happened: the terrain switch drops every tile cache anyway, so reading `render::3d` is free.

The value comes from the layer's tile transformer (elevation-based or not), which is the same object
the tile cache is keyed against — a tile can never be drawn with the wrong answer baked in.

## See also

- [3D Terrain](/docs/features/3d-terrain) — what switches `render::3d`.
- [Composite Vector Tile Layer](/docs/features/composite-vector-tile-layer) — external sources configured from `param::`-dependent expressions.
