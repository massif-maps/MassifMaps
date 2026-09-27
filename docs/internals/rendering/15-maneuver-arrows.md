---
title: Maneuver arrows
description: Navigation arrows cut from a route and drawn as a line with an arrow head.
sidebar_position: 15
---

# Navigation maneuver arrows

Scope: the turn arrow a navigation app draws on the route — how the geometry is cut, how it is
styled, and where it lands in the draw order. Nothing new draws: the arrow is a line, and the head
is a line property ([03-vt-renderer.md](03-vt-renderer.md#line-end-arrows)).

The reference is [maplibre-navigation-ios](https://github.com/maplibre/maplibre-navigation-ios),
which slices the route around the maneuver and gives the piece a line layer plus a symbol layer for
the head. We slice the same way, but the head is part of the line rather than a symbol.

## The one piece

| Class | Where | Does |
|---|---|---|
| `ManeuverArrowBuilder` | `all/native/geometry/` | cuts the arrow out of a route geometry |

That is the whole SDK surface. It takes the route points and a maneuver (a position, or the point
index a `RoutingInstruction` carries), walks `LengthBefore` metres back and `LengthAfter` metres
forward along the polyline — clamped at the ends of the route — and returns a `FeatureCollection` in
WGS84 holding **one line**, running the way the driver goes.

The walk is done in an equirectangular plane anchored at the maneuver's latitude. An arrow is tens
of metres long, so that plane is exact at this scale and the walk stays plain 2D arithmetic.

Serving it is three lines of ordinary SDK API — there is no maneuver-specific data source:

```java
GeoJSONVectorTileDataSource source = new GeoJSONVectorTileDataSource(0, 24);
int layer = source.createLayer("maneuver");
source.setLayerFeatureCollection(layer, null, builder.buildArrowAtIndex(null, points, index));
```

`setLayerFeatureCollection` replaces the whole layer, so an app showing SEVERAL arrows (the current
maneuver and the next, or a whole route preview) keeps its own id → collection map and rebuilds from
it; `DemoMap.setManeuverArrow` is twenty lines of exactly that. A `ManeuverArrowDataSource` doing it
in the SDK was written first and dropped: once the head became a style property an arrow is a single
line feature, and a keyed feature set is not navigation-specific enough to earn a class.

Nothing simplifies the sliced geometry: the source's own `SimplifyTolerance` already applies at
tiling time, in tile pixels, which is the unit that matters.

## Styling contract

Four rules, no assets:

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

**Whole shaft first, head over it** — four attachments, because an attachment is drawn at the
position of its FIRST rule. `line-arrow-only` draws the head alone (the shaft rules carry no arrow,
so the line runs its full length underneath), and the head's base carries a slot one line width wide
so nothing of it crosses the shaft it docks on. Shaft and head therefore read as one polygon, and
the head still keeps its outline where it lands on its own shaft — a U-turn, once the map is zoomed
out far enough. See [03-vt-renderer.md](03-vt-renderer.md#line-end-arrows) for what the other two
orderings cost.

**The casing's arrow numbers are SMALLER than the fill's, and that is not a typo.** They are read
against its own, wider line, so repeating the fill's numbers draws a head 13/8 bigger — a border
round the head about 1.7× the one along the shaft. The two heads are similar triangles about a
common incenter, so the casing's numbers can be worked back from the border the shaft has;
`DemoStyles.maneuverCasingArrowScale` is that formula, and it depends only on the ratio of the two
widths, so it holds at every zoom.

**A custom head** goes in the same rules: `line-arrow-path` takes the `d` attribute of an SVG path
(`M/L/H/V/C/S`, `Z`, absolute or relative, curves flattened) and fits it into the arrow box, so a
contour lifted from an icon set works whatever its viewBox. Both rules carry the same path, and the
casing shrinks its BOX by `fill / casing` — otherwise its skeleton is bigger on top of its own
offset and the border doubles. `line-arrow-scale` and `line-arrow-rotation` finish the placement. It must be **convex**
([03-vt-renderer.md](03-vt-renderer.md#line-end-arrows)): a cloud icon fits, docks and scales
correctly, and its border folds where two lobes meet — the SDK logs a warning rather than silently
redrawing the shape.

**One knob drives the whole arrow.** The widths interpolate over `[view::zoom]` — the LIVE camera
zoom, re-evaluated every frame, not the tile's zoom — and the head is a multiple of the width, so
shaft, head and border shrink together as the map zooms out. The arrow stays an arrow instead of
swallowing the junction, and no second rule has to be kept in sync.

## Why not a marker for the head

A `marker-file` / `marker-type: arrow` symbol rotated by a `bearing` property was built first — it
is what maplibre does — and every one of these had to be fought:

- **Tinting.** `marker-fill` only colours the built-in `ellipse` / `arrow` shapes; a bitmap from
  `marker-file` is multiplied by **`marker-color`**.
- **A two-colour head can not be tinted at all**, so a fill and a casing meant two markers on one
  point, which then fought over draw order and label identity.
- **Draw order.** An attachment is drawn at the position of its *first* rule, so a head fill in the
  default attachment lands under its own casing — every part needs its own attachment.
- **The shaft's cap showed through the head**, because the line ran to the middle of the triangle
  instead of stopping at its base, and nothing in a style can trim a line by a screen distance.
- **Terrain drape clipped it.** With `marker-clip: true` (plain geometry) the head is baked into the
  per-tile drape texture and cut in half at every tile edge it overhangs. A bigger layer buffer does
  not help — the cut is the drape, not the tile data. `--es drape false` renders it whole, which is
  how this was pinned down. (Also worth knowing: `GeoJSONVectorTileDataSource`'s layer buffer, default 4, is in
  tile pixels now — `MBVTTileBuilder::createLayer` divides by `TILE_PIXELS`. When this was pinned
  down it was applied as a fraction of a tile, and 64 wrapped the `uint16` in `makeTileOptions` to
  zero.)
- **`marker-clip: false`** avoids the drape (labels are not baked into it) but puts the head on the
  label path, where it is sized differently and two markers on one point collide over a label id.

The line property has none of these: one geometry, one style layer, one draw order, no bitmap.

## Where it lands in the draw order

Two wirings, both supported, and the choice is only about z-order:

**A layer of its own.** Add a `VectorTileLayer` over the `GeoJSONVectorTileDataSource` at the wanted
position in the layer list. It then draws over every layer below it, and under every `Marker`, `Label` or popup:
billboards are drawn in one global pass after **all** layers ([01-frame.md](01-frame.md)). This is
what the demo does (`DemoMap.createManeuversLayer`), because it works with any style.

**A slot inside the base style.** With a `CompositeVectorTileLayer` base map,
`addVectorDataSource("maneuver", source)` puts the arrow at the position of the `maneuver` entry in
the style's `layers` array — over the roads, under the labels ([09-composite-layer.md](09-composite-layer.md)).
This is the production wiring, and it needs the rules above to live in the style rather than in
an inline CartoCSS string. Note that with this wiring the route line itself should move into a tile
source too, or the arrow (inside the base layer) ends up under a route drawn as a `VectorLayer`.

Within a vector tile layer, labels are drawn after that layer's geometry, so a label belonging to a
group *before* the arrow's slot draws under the arrow. `setLabelRenderOrder(VECTOR_TILE_RENDER_ORDER_LAST)`
on the base map moves every base-map label above all geometry, arrow included.

## Demo

A head can be tried straight off the device: `adb push my-head.svg /sdcard/alpimaps_mbtiles/`, then
`--es maneuverSvg my-head.svg` — or the ACTIONS panel's *maneuver head: next svg*, which cycles the
built-in triangle, every `.svg` in the data directory and the bundled one. Only the first `d`
attribute is read; the rest of the SVG is ignored, because the renderer wants a contour, not a
picture.

`--es maneuvers true` switches the layer on. It seeds a **gallery** around the start position — right
and left 90°, slight and sharp turns, a U-turn and a roundabout — each a synthetic route in metres
run through the real builder, so one screenshot judges every shape navigation actually produces
instead of waiting for a route to contain a hairpin. The offline routing test replaces them with the
real turns of the route it computes (Valhalla maneuver types 0–6 are the start and
destination ones and get no arrow). Knobs: `maneuverBefore`, `maneuverAfter`, `maneuverWidth`,
`maneuverCaseWidth`, `maneuverColor`, `maneuverCaseColor`, `maneuverArrowWidth`,
`maneuverArrowLength`, `maneuverZoomRef`, `maneuverZoomMin`, `maneuverMinScale`.

Verified on the emulator: a real route at 45.16878 / 5.79692, z17, with 3D terrain on — shaft and
head one shape, even casing all the way round, head along the direction of travel; and the gallery
at z13.5 / z15.6 / z17, where the whole arrow scales with the camera and every shape stays legible.
Not yet checked on a device, and not yet checked against a composite slot.
