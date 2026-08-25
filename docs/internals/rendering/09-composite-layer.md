---
title: Composite layer
description: How CompositeVectorTileLayer renders several sources through one CartoCSS style.
sidebar_position: 9
---

# CompositeVectorTileLayer: one style, several sources

Scope: `all/native/layers/CompositeVectorTileLayer.*`. Read this when a hillshade, satellite or
contour source has to sit at a **specific place in the style's layer order** rather than above or
below the whole map.

## The idea

A `VectorTileLayer` that weaves named external sources into the master CartoCSS layer order. Each
source is placed at the position of the matching layer name in the style project's `layers` array,
and configured by a matching `#name { … }` block — including zoom- and style-parameter-dependent
expressions.

Three source types:

| type | how it is drawn |
|---|---|
| `COMPOSITE_SOURCE_TYPE_RASTER` | its own child `RasterTileLayer` at the slot |
| `COMPOSITE_SOURCE_TYPE_HILLSHADE` | its own child `HillshadeRasterTileLayer` at the slot (which may be a paint — [07-hillshade-contours.md](07-hillshade-contours.md)) |
| `COMPOSITE_SOURCE_TYPE_VECTOR` | another MBVT source (e.g. `ContourTileDataSource`) drawn at its slot as a child `VectorTileLayer` using the master decoder, filtered to its own layer name |

Sources can be added and removed at runtime (`addExternalDataSource` / `addVectorDataSource` /
`removeExternalDataSource`).

## Everything is style-driven — this is the part people trip over

The child layers take their settings **from the resolved style**, not from the setters on the child
class. A `HillshadeRasterTileLayer` created by the app and configured with `setContourEnabled(…)` is
a *stand-alone* layer; the child inside a composite is not that object, and never sees those calls.

Properties applied from the style (`CompositeVectorTileLayer.cpp`, per frame for cheap ones, on
change for the rest):

- hillshade: `hillshade-opacity`, `-exaggeration`, `-height-scale`, `-contrast`, `-method`,
  `-illumination-direction`, `-shadow-color`, `-highlight-color`, `-accent-color`,
  **`-contour-interval`, `-contour-color`, `-contour-width`** (interval > 0 turns contours on);
- raster: `raster-opacity`, `raster-comp-op`, …;
- merged/child vector sources that are a `ContourTileDataSource`: `contour-base-interval`,
  `contour-resolution`, `contour-min-visible-zoom`, `contour-simplify-tolerance`,
  `contour-label-stubs`, `contour-label-interval`.

Generation parameters (the last group) regenerate tiles when they change, so they are evaluated at a
neutral zoom and applied only on an actual change (`_lastVectorConfig`).

## Rendering

`renderComposite` draws, in order: group 0 (this layer's own vt render for the style layers before
the first slot), then each draw item — a child raster/hillshade layer, or an internal
`VectorTileLayer` rendering a later style-layer group.

`collectDrapeLayers` must expose the **children** as well, with the same order and gating as
`renderComposite`. If it reports only itself, every slot and every later style-layer group keeps its
own terrain pre-pass and depth domain — exactly the split the shared ground exists to remove.

For depth, the children's style layers are part of the stack's ordinal numbering like any other
layer's ([05-depth-model.md](05-depth-model.md#ordinals-and-the-budget)) — which is why the ordinals
are handed out per layer in draw order rather than per renderer.

Under 3D terrain the same list decides the **drape order**, and a slot kept out of the drape bake
(`TerrainOptions::NoDrapeLayerFilter` — contours) has to be occluded by the layers above it rather
than drawn over them: [04-terrain.md](04-terrain.md#putting-the-live-layer-back-in-its-style-position).

## Why this exists at all

Tangram has one ordered style list, so "hillshade under the roads but over the landcover" is just an
`order:` value, and their hillshade is computed inside the terrain raster draw
(`res/scenes/hillshade.yaml`) rather than being a layer at all. This fork has separate layer objects
with separate renderers, and the composite layer is what buys back the single ordered list. The
long-term convergence is the same as theirs: fewer separate tile sets, more of the terrain-derived
paint computed inside the ground draw.

An app's own content can take a slot the same way, not just terrain-derived sources: a maneuver arrow
source added with `addVectorDataSource` draws over the roads and under the labels, see
[15-maneuver-arrows.md](15-maneuver-arrows.md).

## What could be better

- **One decode per style-layer group.** The master style is decoded once per group (groups = slots
  + 1), so a style with three external slots decodes every tile four times. A single-pass renderer
  that filters at draw time instead of at build time is the fix; it is blocked on
  `rendererLayerFilter` being baked into the tile build, which is why the split exists at all.
- **Fold the terrain-derived paint into the ground draw**, as tangram does — a hillshade slot would
  then cost no tile set and no extra pass. Round 26 of the
  [performance log](../performance-log.md) measured the opposite arrangement (contours as an extra
  pass) as *slower*, which is the evidence for going this way.
- **`raster-comp-op` is parsed and ignored** — `RasterTileLayer` has no blend-mode setter. Use
  `raster-opacity` until it does.

