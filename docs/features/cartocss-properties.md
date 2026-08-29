---
title: CartoCSS property reference
description: "Every property a CartoCSS style may set, its value kind, and whether a param:: driving it stays live"
sidebar_position: 20
---

# CartoCSS property reference

:::info Generated
`scripts/gen-cartocss-properties.py` reads the `bindProperty()` calls in
`libs-massif/mapnikvt/src/mapnikvt/*Symbolizer.h` and the name map in
`CartoCSSMapnikTranslator.cpp`. Edit those, then re-run the script — never this page.
:::

219 properties across 12 symbolizers.

## Reading the table

- **live** — the value is re-evaluated every frame, so a `param::` reaching *only* properties
  like these changes the map with a **redraw**. See
  [Style parameters](style-parameters.md).
- **baked** — the property is also read while the tile is built (it sizes a raster, shapes a
  join, decides a glyph). A `param::` reaching one forces a **re-decode of every visible
  tile**, however live its type is.
- Liveness is decided **per parameter across the whole style**, not per use: one baked use
  anywhere makes the parameter baked everywhere.
- **Default** is also the fallback: a value reading a field the feature does not carry (or a
  parameter with no value) evaluates to unset, and an unset value takes the default rather
  than failing. Write an explicit guard — `[color] <> null ? [color] : '#0000ff'` — when the
  fallback should be something else.

Live-capable properties: 50 of 219.

## `building`

| CartoCSS | mapnik | Value | Default | Live | Baked |
|---|---|---|---|---|---|
| `building-fill` | `fill` | color | `#808080` | yes |  |
| `building-fill-opacity` | `fill-opacity` | float | `1.0` | yes |  |
| `building-geometry-transform` | `geometry-transform` | transform |  |  |  |
| `building-height` | `height` | float | `0.0` |  |  |
| `building-min-height` | `min-height` | float | `0.0` |  |  |

## `contour`

| CartoCSS | mapnik | Value | Default | Live | Baked |
|---|---|---|---|---|---|
| `contour-base-interval` | `base-interval` | float | `10.0` |  |  |
| `contour-min-visible-zoom` | `min-visible-zoom` | float | `12.0` |  |  |
| `contour-resolution` | `resolution` | float | `128.0` |  |  |
| `contour-simplify-tolerance` | `simplify-tolerance` | float | `1.0` |  |  |
| `contour-visible` | `visible` | bool | `true` |  |  |

## `hillshade`

| CartoCSS | mapnik | Value | Default | Live | Baked |
|---|---|---|---|---|---|
| `hillshade-accent-color` | `accent-color` | color | `#000000` |  |  |
| `hillshade-comp-op` | `comp-op` | comp-op | `src-over` |  |  |
| `hillshade-contour-color` | `contour-color` | color | `#804000` |  |  |
| `hillshade-contour-interval` | `contour-interval` | float | `0.0` | yes |  |
| `hillshade-contour-width` | `contour-width` | float | `1.0` | yes |  |
| `hillshade-contrast` | `contrast` | float | `0.5` | yes |  |
| `hillshade-exaggeration` | `exaggeration` | float | `1.0` | yes |  |
| `hillshade-height-scale` | `height-scale` | float | `1.0` | yes |  |
| `hillshade-highlight-color` | `highlight-color` | color | `#ffffff` |  |  |
| `hillshade-illumination-direction` | `illumination-direction` | float | `335.0` | yes |  |
| `hillshade-method` | `method` | string | `standard` |  |  |
| `hillshade-opacity` | `opacity` | float | `1.0` | yes |  |
| `hillshade-shadow-color` | `shadow-color` | color | `#000000` |  |  |
| `hillshade-visible` | `visible` | bool | `true` |  |  |

## `line`

| CartoCSS | mapnik | Value | Default | Live | Baked |
|---|---|---|---|---|---|
| `line-arrow-length` | `arrow-length` | float | `2.5` |  |  |
| `line-arrow-only` | `arrow-only` | bool | `false` |  |  |
| `line-arrow-path` | `arrow-path` | string |  |  |  |
| `line-arrow-rotation` | `arrow-rotation` | float | `0.0` |  |  |
| `line-arrow-scale` | `arrow-scale` | float | `1.0` |  |  |
| `line-arrow-width` | `arrow-width` | float | `3.0` |  |  |
| `line-blur` | `blur` | float | `0.0` | yes |  |
| `line-cap` | `stroke-linecap` | enum | `butt` |  |  |
| `line-color` | `stroke` | color | `#000000` | yes |  |
| `line-comp-op` | `comp-op` | comp-op | `src-over` |  |  |
| `line-dasharray` | `stroke-dasharray` | string |  |  |  |
| `line-emissive-strength` | `stroke-emissive-strength` | float | `1.0` | yes |  |
| `line-end-arrow` | `end-arrow` | bool | `false` |  |  |
| `line-gap-width` | `gap-width` | float | `0.0` | yes |  |
| `line-geometry-transform` | `geometry-transform` | transform |  |  |  |
| `line-join` | `stroke-linejoin` | enum | `miter` |  |  |
| `line-miterlimit` | `stroke-miterlimit` | float | `4.0` |  | yes |
| `line-offset` | `offset` | float | `0.0` | yes |  |
| `line-opacity` | `stroke-opacity` | float | `1.0` | yes |  |
| `line-width` | `stroke-width` | float | `1.0` |  | yes |

## `line-pattern`

| CartoCSS | mapnik | Value | Default | Live | Baked |
|---|---|---|---|---|---|
| `line-pattern-comp-op` | `comp-op` | comp-op | `src-over` |  |  |
| `line-pattern-file` | `file` | string |  |  |  |
| `line-pattern-fill` | `fill` | color | `#ffffff` | yes |  |
| `line-pattern-geometry-transform` | `geometry-transform` | transform |  |  |  |
| `line-pattern-offset` | `offset` | float | `0.0` | yes |  |
| `line-pattern-opacity` | `opacity` | float | `1.0` | yes |  |

## `marker`

| CartoCSS | mapnik | Value | Default | Live | Baked |
|---|---|---|---|---|---|
| `marker-allow-overlap` | `allow-overlap` | bool | `false` |  |  |
| `marker-allow-overlap-same-feature-id` | `allow-overlap-same-feature-id` | bool | `false` |  |  |
| `marker-clip` | `clip` | bool | `false` |  |  |
| `marker-color` | `color` | color | `#ffffff` | yes |  |
| `marker-comp-op` | `comp-op` | comp-op | `src-over` |  |  |
| `marker-feature-id` | `feature-id` | value |  |  |  |
| `marker-file` | `file` | string |  |  |  |
| `marker-fill` | `fill` | color | `#0000ff` |  |  |
| `marker-fill-opacity` | `fill-opacity` | float | `1.0` |  |  |
| `marker-halo-fill` | `halo-fill` | color | `#ffffff` | yes |  |
| `marker-halo-opacity` | `halo-opacity` | float | `1.0` | yes |  |
| `marker-halo-radius` | `halo-radius` | float | `0.0` | yes |  |
| `marker-height` | `height` | float | `0.0` |  | yes |
| `marker-ignore-placement` | `ignore-placement` | bool | `false` |  |  |
| `marker-line-color` | `stroke` | color | `#000000` |  |  |
| `marker-line-opacity` | `stroke-opacity` | float | `1.0` |  |  |
| `marker-line-width` | `stroke-width` | float | `0.5` |  | yes |
| `marker-max-distance` | `max-distance` | float | `0.0` |  |  |
| `marker-opacity` | `opacity` | float | `1.0` | yes |  |
| `marker-placement` | `placement` | value | `point` |  |  |
| `marker-placement-priority` | `placement-priority` | float | `0.0` |  |  |
| `marker-rank` | `rank` | float | `0.0` | yes |  |
| `marker-same-feature-id-dependent` | `same-feature-id-dependent` | bool | `false` |  |  |
| `marker-sdf` | `sdf` | bool | `false` |  |  |
| `marker-spacing` | `spacing` | float | `100.0` |  |  |
| `marker-transform` | `transform` | transform |  |  |  |
| `marker-type` | `marker-type` | value | `auto` |  |  |
| `marker-width` | `width` | float | `0.0` |  | yes |

## `point`

| CartoCSS | mapnik | Value | Default | Live | Baked |
|---|---|---|---|---|---|
| `point-comp-op` | `comp-op` | comp-op | `src-over` |  |  |
| `point-file` | `file` | string |  |  |  |
| `point-opacity` | `opacity` | float | `1.0` | yes |  |
| `point-transform` | `transform` | transform |  |  |  |

## `polygon`

| CartoCSS | mapnik | Value | Default | Live | Baked |
|---|---|---|---|---|---|
| `polygon-comp-op` | `comp-op` | comp-op | `src-over` |  |  |
| `polygon-emissive-strength` | `fill-emissive-strength` | float | `1.0` | yes |  |
| `polygon-fill` | `fill` | color | `#808080` | yes |  |
| `polygon-geometry-transform` | `geometry-transform` | transform |  |  |  |
| `polygon-opacity` | `fill-opacity` | float | `1.0` | yes |  |

## `polygon-pattern`

| CartoCSS | mapnik | Value | Default | Live | Baked |
|---|---|---|---|---|---|
| `polygon-pattern-comp-op` | `comp-op` | comp-op | `src-over` |  |  |
| `polygon-pattern-file` | `file` | string |  |  |  |
| `polygon-pattern-fill` | `fill` | color | `#ffffff` | yes |  |
| `polygon-pattern-geometry-transform` | `geometry-transform` | transform |  |  |  |
| `polygon-pattern-opacity` | `opacity` | float | `1.0` | yes |  |

## `raster`

| CartoCSS | mapnik | Value | Default | Live | Baked |
|---|---|---|---|---|---|
| `raster-comp-op` | `comp-op` | comp-op | `src-over` |  |  |
| `raster-filter-mode` | `filter-mode` | string | `bilinear` |  |  |
| `raster-opacity` | `opacity` | float | `1.0` | yes |  |
| `raster-visible` | `visible` | bool | `true` |  |  |

## `shield`

| CartoCSS | mapnik | Value | Default | Live | Baked |
|---|---|---|---|---|---|
| `shield-allow-overlap` | `allow-overlap` | bool | `false` |  |  |
| `shield-anchors` | `anchors` | string |  |  |  |
| `shield-avoid-edges` | `avoid-edges` | value |  |  |  |
| `shield-background-border-fill` | `background-border-fill` | color | `#000000` |  |  |
| `shield-background-border-opacity` | `background-border-opacity` | float | `1.0` |  |  |
| `shield-background-border-width` | `background-border-width` | float | `0.0` |  |  |
| `shield-background-fill` | `background-fill` | color | `transparent` |  |  |
| `shield-background-opacity` | `background-opacity` | float | `1.0` |  |  |
| `shield-background-padding-x` | `background-padding-x` | float | `3.0` |  |  |
| `shield-background-padding-y` | `background-padding-y` | float | `2.0` |  |  |
| `shield-background-radius` | `background-radius` | float | `0.0` |  |  |
| `shield-character-spacing` | `character-spacing` | float | `0.0` |  |  |
| `shield-clip` | `clip` | bool | `false` |  |  |
| `shield-comp-op` | `comp-op` | comp-op | `src-over` |  |  |
| `shield-dx` | `shield-dx` | float | `0.0` |  |  |
| `shield-dy` | `shield-dy` | float | `0.0` |  |  |
| `shield-face-name` | — | ignored |  |  |  |
| `shield-feature-id` | `feature-id` | value |  |  |  |
| `shield-file` | `file` | string |  |  |  |
| `shield-fill` | `fill` | color | `#000000` | yes |  |
| `shield-halo-fill` | `halo-fill` | color | `#ffffff` | yes |  |
| `shield-halo-opacity` | `halo-opacity` | float | `1.0` | yes |  |
| `shield-halo-radius` | `halo-radius` | float | `0.0` | yes |  |
| `shield-halo-rasterizer` | `halo-rasterizer` | value |  |  |  |
| `shield-horizontal-alignment` | `horizontal-alignment` | value | `auto` |  |  |
| `shield-icon-background-border-fill` | `icon-background-border-fill` | color | `#000000` |  |  |
| `shield-icon-background-border-opacity` | `icon-background-border-opacity` | float | `1.0` |  |  |
| `shield-icon-background-border-width` | `icon-background-border-width` | float | `0.0` |  |  |
| `shield-icon-background-fill` | `icon-background-fill` | color | `transparent` |  |  |
| `shield-icon-background-opacity` | `icon-background-opacity` | float | `1.0` |  |  |
| `shield-icon-background-padding-x` | `icon-background-padding-x` | float | `3.0` |  |  |
| `shield-icon-background-padding-y` | `icon-background-padding-y` | float | `2.0` |  |  |
| `shield-icon-background-radius` | `icon-background-radius` | float | `0.0` |  |  |
| `shield-icon-dx` | `icon-dx` | float | `0.0` |  |  |
| `shield-icon-dy` | `icon-dy` | float | `0.0` |  |  |
| `shield-icon-face-name` | `icon-face-name` | string |  |  |  |
| `shield-icon-fill` | `icon-fill` | color | `#000000` | yes |  |
| `shield-icon-halo-fill` | `icon-halo-fill` | color | `#000000` | yes |  |
| `shield-icon-halo-radius` | `icon-halo-radius` | float | `0.0` | yes |  |
| `shield-icon-name` | `icon-name` | string |  |  |  |
| `shield-icon-opacity` | `icon-opacity` | float | `1.0` | yes |  |
| `shield-icon-size` | `icon-size` | float | `0.0` |  |  |
| `shield-image-scale` | `image-scale` | float | `1.0` | yes |  |
| `shield-line-spacing` | `line-spacing` | float | `0.0` |  |  |
| `shield-max-distance` | `max-distance` | float | `0.0` |  |  |
| `shield-min-distance` | `minimum-distance` | float | `0.0` |  |  |
| `shield-name` | — | ignored |  |  |  |
| `shield-occlusion-opacity` | `occlusion-opacity` | float | `-1.0` |  |  |
| `shield-orientation` | `orientation` | float | `0.0` |  |  |
| `shield-placement` | `placement` | value | `point` |  |  |
| `shield-placement-priority` | `placement-priority` | float | `0.0` |  |  |
| `shield-rank` | `rank` | float | `0.0` | yes |  |
| `shield-sdf` | `sdf` | bool | `false` |  |  |
| `shield-size` | `size` | float | `10.0` |  | yes |
| `shield-spacing` | `spacing` | float | `0.0` |  |  |
| `shield-text-dx` | `dx` | float | `0.0` |  |  |
| `shield-text-dy` | `dy` | float | `0.0` |  |  |
| `shield-text-horizontal-alignment` | `text-horizontal-alignment` | string |  |  |  |
| `shield-text-opacity` | `opacity` | float | `1.0` | yes |  |
| `shield-text-optional` | `text-optional` | bool | `false` |  |  |
| `shield-text-transform` | `text-transform` | value | `none` |  |  |
| `shield-unlock-image` | `unlock-image` | bool | `false` |  |  |
| `shield-vertical-alignment` | `vertical-alignment` | value | `auto` |  |  |
| `shield-wrap-before` | `wrap-before` | bool | `false` |  |  |
| `shield-wrap-character` | `wrap-character` | string |  |  |  |
| `shield-wrap-width` | `wrap-width` | float | `0.0` |  |  |

## `text`

| CartoCSS | mapnik | Value | Default | Live | Baked |
|---|---|---|---|---|---|
| `text-allow-overlap` | `allow-overlap` | bool | `false` |  |  |
| `text-allow-overlap` | `allow-overlap` | bool | `false` |  |  |
| `text-allow-overlap-same-feature-id` | `allow-overlap-same-feature-id` | bool | `false` |  |  |
| `text-avoid-edges` | `avoid-edges` | value |  |  |  |
| `text-background-border-fill` | `background-border-fill` | color | `#000000` |  |  |
| `text-background-border-opacity` | `background-border-opacity` | float | `1.0` |  |  |
| `text-background-border-width` | `background-border-width` | float | `0.0` |  |  |
| `text-background-fill` | `background-fill` | color | `transparent` |  |  |
| `text-background-opacity` | `background-opacity` | float | `1.0` |  |  |
| `text-background-padding-x` | `background-padding-x` | float | `3.0` |  |  |
| `text-background-padding-y` | `background-padding-y` | float | `2.0` |  |  |
| `text-background-radius` | `background-radius` | float | `0.0` |  |  |
| `text-callout-align` | `callout-align` | string |  |  |  |
| `text-callout-line-anchor` | `callout-line-anchor` | string |  |  |  |
| `text-callout-line-width` | `callout-line-width` | float | `1.0` |  |  |
| `text-callout-max-rows` | `callout-max-rows` | float | `8.0` |  |  |
| `text-callout-offset` | `callout-offset` | float | `0.0` |  |  |
| `text-callout-persist` | `callout-persist` | float | `0.0` |  |  |
| `text-callout-screen-anchor` | `callout-screen-anchor` | float | `-1.0` |  |  |
| `text-callout-step` | `callout-step` | float | `0.0` |  |  |
| `text-character-spacing` | `character-spacing` | float | `0.0` |  |  |
| `text-clip` | `clip` | bool | `false` |  |  |
| `text-comp-op` | `comp-op` | comp-op | `src-over` |  |  |
| `text-dx` | `dx` | float | `0.0` |  |  |
| `text-dy` | `dy` | float | `0.0` |  |  |
| `text-face-name` | — | ignored |  |  |  |
| `text-feature-id` | `feature-id` | value |  |  |  |
| `text-fill` | `fill` | color | `#000000` | yes |  |
| `text-halo-fill` | `halo-fill` | color | `#ffffff` | yes |  |
| `text-halo-opacity` | `halo-opacity` | float | `1.0` | yes |  |
| `text-halo-radius` | `halo-radius` | float | `0.0` | yes |  |
| `text-halo-rasterizer` | `halo-rasterizer` | value |  |  |  |
| `text-horizontal-alignment` | `horizontal-alignment` | value | `auto` |  |  |
| `text-line-spacing` | `line-spacing` | float | `0.0` |  |  |
| `text-max-distance` | `max-distance` | float | `0.0` |  |  |
| `text-min-distance` | `minimum-distance` | float | `0.0` |  |  |
| `text-name` | — | ignored |  |  |  |
| `text-occlusion-opacity` | `occlusion-opacity` | float | `-1.0` |  |  |
| `text-opacity` | `opacity` | float | `1.0` | yes |  |
| `text-orientation` | `orientation` | float | `0.0` |  |  |
| `text-placement` | `placement` | value | `point` |  |  |
| `text-placement-priority` | `placement-priority` | float | `0.0` |  |  |
| `text-rank` | `rank` | float | `0.0` | yes |  |
| `text-same-feature-id-dependent` | `same-feature-id-dependent` | bool | `false` |  |  |
| `text-secondary-dx` | `secondary-dx` | float | `0.0` |  |  |
| `text-secondary-dy` | `secondary-dy` | float | `0.0` |  |  |
| `text-secondary-fill` | `secondary-fill` | color | `#000000` | yes |  |
| `text-secondary-name` | `secondary-name` | string |  |  |  |
| `text-secondary-opacity` | `secondary-opacity` | float | `1.0` | yes |  |
| `text-secondary-scale` | `secondary-scale` | float | `0.7` |  |  |
| `text-size` | `size` | float | `10.0` |  | yes |
| `text-spacing` | `spacing` | float | `0.0` |  |  |
| `text-transform` | `text-transform` | value | `none` |  |  |
| `text-vertical-alignment` | `vertical-alignment` | value | `auto` |  |  |
| `text-wrap-before` | `wrap-before` | bool | `false` |  |  |
| `text-wrap-character` | `wrap-character` | string |  |  |  |
| `text-wrap-width` | `wrap-width` | float | `0.0` |  |  |
