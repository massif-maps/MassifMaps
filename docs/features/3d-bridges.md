---
sidebar_position: 26
---

# 3D bridges

A road on 3D terrain is draped: painted onto the terrain, so it follows every bump the elevation
data has. Right for a road on the ground, wrong for one on a bridge - the deck sags into the
valley it crosses, and a tunnel climbs over the hill it goes through. With 3D bridges on, a
feature the style marks as a **span** is laid straight between its two ends instead, on the
ground it leaves and the ground it reaches, and a bridge deck stands as a thin extrusion carrying
its road, its labels, its one-way arrows.

Off by default. Nothing runs until an app turns it on, and a map that never does pays nothing.

## Turning it on

Object API:

```java
terrainOptions.setBridges3DEnabled(true);
```

Facade:

```java
map.set("terrain.bridges3DEnabled", true);
```

Needs 3D terrain: with the terrain off, or while the map is auto-flattened, spans drape like the
ground and decks are not drawn.

## What the style says

Three properties, one per symbolizer, each `drape` (the default), `span` or `underground`:

| Property | On | Effect with the option on |
|---|---|---|
| `line-elevation-mode: span` | a line | The line runs straight between its two ends, at the ground height of each. A road on a bridge, a rail line, a footbridge. |
| `polygon-elevation-mode: span` | a fill | The fill lies on the same straight surface: a bridge bed. |
| `building-elevation-mode: span` | an extrusion | A **deck**: `building-min-height` and `building-height` become a thickness measured from the road surface, negative values hanging below it. The roof wears the span lines drawn on it. |
| `*-elevation-mode: underground` | any | Parses, and drapes like the ground for now - a tunnel drawn on the surface in its tunnel style, as mapbox does. |

A converted Mapbox Standard carries the tags the tiler needs (`structure = 'bridge'`), so the
rules are one line each. The demo's deck:

```css
#structure[class = 'land']['mapnik::geometry_type' = 3]::land_structure_deck {
  building-fill: #98a0b0;
  building-elevation-mode: 'span';
  building-min-height: -1.5;   /* a 1.4 m slab... */
  building-height: -0.1;       /* ...0.1 m under the road surface */
}
```

`mapbox2css` does not emit these properties yet; add them to the converted style by hand.

## What you get

- A deck that is one flat plane between its abutments whatever the elevation data does under it,
  whatever tile cut it, and through the auto-flatten ramp. Its road is drawn on the roof, its
  labels and arrows stand on it, it casts a shadow.
- Where the deck's outline runs on past the road's bridge segment - onto the quay - the roof
  shows the ground's own roads and crosswalks, so the approach is continuous.
- A bridge opened mid-deck, with neither end on screen, still finds its ends: the SDK fetches
  the coarser tiles that hold them.

## What it costs

On: two elevation reads per bridge per cull, a small render-to-texture per tile that holds a
deck, and up to 32 coarse tiles fetched for bridges whose ends are off screen. Measured at
Grenoble: 3.5-6.8 ms of union work per second of panning.

Off: none of that. The switch is checked before any span work starts; the only trace is the
per-feature "is this a span" test the renderer already made.

## Known limits

The two sides of an abutment are rarely at one height, and a level deck shows a sliver of ground
through one corner or a gap under the other; a corner that stops short of the road's bridge
segment leaves a few metres of road on the ground. The mechanism, the history and the open list
are in [the renderer page](../internals/rendering/17-bridges.md).
