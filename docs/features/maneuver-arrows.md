---
title: Navigation Maneuver Arrows
sidebar_position: 13
---

# Navigation Maneuver Arrows

The turn arrow a navigation app draws on the route: `ManeuverArrowBuilder` cuts the piece of the
route around a maneuver, and the head is a **line property** — no marker, no bitmap, no second
symbol layer.

:::info Fork feature
Added in PR [#61](https://github.com/massif-maps/MassifMaps/pull/61). The reference is
[maplibre-navigation-ios](https://github.com/maplibre/maplibre-navigation-ios), which slices the
route the same way but draws the head as a symbol. Technical notes:
[`docs/internals/rendering/15-maneuver-arrows.md`](/docs/internals/rendering/maneuver-arrows).
:::

<figure class="docs-figure">

![Maneuver arrows of every shape over a city map](/img/features/maneuver-arrows.jpg)

<figcaption>The demo's arrow gallery — right and left 90°, slight and sharp turns, a U-turn and a roundabout — each a synthetic route run through the real builder. Captured at z15.4.</figcaption>

</figure>

## Building the geometry

```java
ManeuverArrowBuilder builder = new ManeuverArrowBuilder();
builder.setLengthBefore(30f);   // metres walked back along the route
builder.setLengthAfter(30f);    // metres walked forward

// from a RoutingInstruction's point index …
FeatureCollection arrow = builder.buildArrowAtIndex(null, routePoints, instruction.getPointIndex());
// … or from a position on the route
FeatureCollection arrow2 = builder.buildArrow(null, routePoints, maneuverPos);
```

It returns a `FeatureCollection` in WGS84 holding **one line**, running the way the driver goes,
clamped at the ends of the route. Pass a `Projection` if the points are not WGS84 (`null` = WGS84).
The walk is done in an equirectangular plane anchored at the maneuver's latitude — exact at the tens
of metres an arrow spans.

Serving it is ordinary SDK API; there is no maneuver-specific data source:

```java
GeoJSONVectorTileDataSource source = new GeoJSONVectorTileDataSource(0, 24);
int layer = source.createLayer("maneuver");
source.setLayerFeatureCollection(layer, null, arrow);
```

`setLayerFeatureCollection` replaces the whole layer, so an app showing several arrows (the current
maneuver and the next) keeps its own id → collection map and rebuilds from it.

### With the surface API

`ManeuverArrowBuilder` has no spec type — it produces geometry rather than being a map object — but
the source that serves the arrow does, so only the builder itself stays object-API:

```java
MassifSource arrows = map.source("maneuver-src", Spec.of("geojson").set("maxZoom", 24));
int layer = arrows.createLayer("maneuver");
// The builder returns SDK geometry, so the object-API setter is the one that takes it directly.
((GeoJSONVectorTileDataSource) Massif.rawSource("maneuver-src"))
    .setLayerFeatureCollection(layer, null, arrow);

map.addLayer("maneuver", Spec.of("vector")
    .set("source", "maneuver-src")
    .set("style", "route-style"));
```

## Styling contract

Two rules, four attachments, no assets:

```css
#maneuver::case     { line-color: @casing; line-width: linear([view::zoom], (12, 3.9), (17, 13));
                      line-join: round; line-cap: round; }
#maneuver::fill     { line-color: @fill;   line-width: linear([view::zoom], (12, 2.4), (17, 8));
                      line-join: round; line-cap: round; }
#maneuver::headcase { line-color: @casing; line-width: linear([view::zoom], (12, 3.9), (17, 13));
                      line-end-arrow: true; line-arrow-only: true;
                      line-arrow-width: 2.18; line-arrow-length: 1.72; }
#maneuver::head     { line-color: @fill;   line-width: linear([view::zoom], (12, 2.4), (17, 8));
                      line-end-arrow: true; line-arrow-only: true;
                      line-arrow-width: 2.4; line-arrow-length: 1.9; }
```

- **Whole shaft first, head over it.** An attachment is drawn at the position of its *first* rule,
  hence four of them. `line-arrow-only` draws the head alone, so the shaft runs its full length
  underneath, and the head's base carries a slot one line width wide so nothing of it crosses the
  shaft.
- **The casing's arrow numbers are smaller than the fill's, and that is not a typo.** They are read
  against its own, wider line; repeating the fill's numbers draws a border round the head ~1.7× the
  one along the shaft.
- **One knob drives the whole arrow.** The widths interpolate over `[view::zoom]` — the *live*
  camera zoom, re-evaluated per frame — and the head is a multiple of the width, so shaft, head and
  border shrink together and the arrow never swallows the junction.

### A custom head

`line-arrow-path` takes the `d` attribute of an SVG path (`M/L/H/V/C/S`, `Z`, absolute or relative,
curves flattened) and fits it into the arrow box, so a contour lifted from an icon set works whatever
its viewBox. Both rules carry the same path, and the casing shrinks its **box** by `fill / casing`.
`line-arrow-scale` and `line-arrow-rotation` finish the placement.

The shape must be **convex** — a shape with lobes folds its border where they meet, and the SDK logs
a warning rather than silently redrawing it.

## Where it lands in the draw order

| Wiring | Result |
|---|---|
| A `VectorTileLayer` of its own over the GeoJSON source | draws over every layer below it, under every `Marker` / `Label` / popup (billboards are one global pass after all layers). Works with any style. |
| `CompositeVectorTileLayer.addVectorDataSource("maneuver", source)` | the arrow lands at the `maneuver` slot of the base style's layer order — over the roads, under the labels. The production wiring; the two rules must live in the style. |

With the composite wiring the route line itself should move into a tile source too, or the arrow
(inside the base layer) ends up under a route drawn as a `VectorLayer`. And within a vector tile
layer, labels draw after that layer's geometry —
`setLabelRenderOrder(VECTOR_TILE_RENDER_ORDER_LAST)` moves every base-map label above the arrow.

## Why not a marker for the head

A rotated `marker-file` symbol was built first (it is what maplibre does) and every one of these had
to be fought: `marker-fill` does not tint a bitmap (`marker-color` does), a two-colour head can not
be tinted at all, each part needs its own attachment for draw order, the shaft's cap showed through
the head, and with `marker-clip: true` the head is baked into the per-tile terrain drape texture and
cut in half at every tile edge it overhangs. The line property has none of these: one geometry, one
style layer, one draw order, no bitmap.

## See also

- [GeoJSON Vector Tiling](/docs/features/geojson-vector-tiles) — the source that serves the arrow.
- [Routing](/docs/guides/routing) — where the maneuvers come from.
