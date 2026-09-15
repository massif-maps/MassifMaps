---
title: Depth model
description: How z-fighting, see-through and content sinking into the ground are prevented — ported whole from tangram-ng.
sidebar_position: 5
---

# The depth model

Scope: how the ground, the content on it, and the vector elements above it relate in depth. This is
the single most failure-prone part of the renderer — rounds 45 to 56 of the terrain work were almost
entirely about it — so it is documented as a model plus a catalogue of what breaks.

**It is tangram's model, ported whole.** Read `res/scenes/terrain-3d.yaml` in tangram-ng, not just
their shaders: `polygon.vs` sets `depth_shift = 0.0` "to allow blocks to modify", and the terrain
scene is the block that modifies it.

## The model

```glsl
// tangram core/shaders/polygon.vs
gl_Position.z += (proxy - layer) * (TANGRAM_DEPTH_DELTA * gl_Position.w + depth_shift);

// tangram res/scenes/terrain-3d.yaml
depth_shift = -0.02 * u_proj[2][3];   // [2][3] is -1 with glm::perspective => a FLAT 0.02
#ifdef TANGRAM_RASTER_STYLE
proxy *= 48.0;                        // "prevent terrain poking through the level above"
#endif
```

with `TANGRAM_DEPTH_DELTA = 2⁻¹⁹` on a 24-bit depth buffer, `layer` the style layer's order, and —
`core/src/style/style.cpp` — opaque *and* translucent geometry drawing with depth test **and depth
write**.

Ours, matching it:

| piece | ours |
|---|---|
| the ground | drawn at true depth, writes depth, pushed back 48 units per **proxy** level |
| content (fills, lines, buildings) | writes depth, carries `(proxy − layer)` with the layer ordinal |
| `depth_shift` | flat, not scaled by the projection |
| ordinals | dense across the whole stack, one range per layer (see below) |
| near plane | camera height / 50, as tangram ([04-terrain.md](04-terrain.md#near-and-far-planes)) |

### Why the two terms differ

`TANGRAM_DEPTH_DELTA · w` is a **constant-NDC** bias: its eye-space tolerance grows as
distance²/near, which is how a bias of a few units becomes hundreds of metres at range. That is the
mechanism behind every see-through this project has had.

`depth_shift` is a **constant clip-space** offset: its NDC effect is `0.02/w` — strong near the
camera, where an un-subdivided fill chords furthest below the surface, and vanishing at range, where
it would leak. That is why tangram can afford it and why it is the term that pays for not
subdividing content.

### Ordinals, and the budget

Tangram numbers its style layers in one global list (`res/osm-bright.yaml` uses 1..93) and spends
`93 × 0.02 ≈ 1.86` clip units across the scene. Our stack is several renderers — a composite layer's
children included — so the ordinals are handed out in **draw order, one dense range per layer**:
`MapRenderer::drawLayers` gives each `TileLayer` an ordinal base and advances it by that layer's
style layer count.

The quantity that matters is **constant × span**, i.e. the total depth budget the stack gets, not the
constant. A style with nine style layers at their 0.02 would spend a tenth of their budget. So the
shift is derived from the budget instead: `TERRAIN_TANGRAM_DEPTH_BUDGET / span`, which gives back
exactly 0.02 for a 93-layer style.

Measured on device (45.244172/5.760595 z13.2 t26, 9 ordinals): **0.2 is the largest total with no
see-through**; 0.3 opens pale wedges through ridges, 0.5 sees straight through Saint-Eynard. Their
budget *is* the leak threshold. Do not raise it.

Numbering starts at **1**, not 0: the ground is a numbered draw at the bottom of the same list
(tangram draws the terrain raster at `order: global.earth_order`), so content at ordinal 0 would
share the ground's term exactly and have no clearance over ground it chords across.

### Proxy depth

Tangram, `core/src/tile/tileManager.cpp`:

```cpp
setProxyDepth(m_proxyCounter > 0 ? std::max(maxVisS - tileId.s, 1) : 0);
```

The `m_proxyCounter > 0` is a **guard**, and it is the whole point: proxy depth applies only to a
tile drawn in place of one that has no data. A legitimately coarse tile in a mixed-LOD cover is a
live tile and takes **zero**, however far away it is. Giving it a depth pushes most of a tilted
view's far field back by clip units and takes the hillshade paint (which carries the same push)
behind the ground it shades — far hillshade missing, blinking as the cover's deepest level moves.

The measure is in **levels** (how many levels coarser than the deepest level on screen), not a flat
1, and the raster/ground multiplier is **48**.

## Four ways to get this wrong, all of them tried

| attempt | what it produced |
|---|---|
| masks dropped, content not writing depth | the previous zoom's roads painting through every gap in the new tile's content, blinking as the blend ran |
| content writing depth, no per-style-layer ordinal | fills shredded into stripes, washed road casings |
| no subdivision **and** no `depth_shift` | all content sunk into the terrain — contours and roads swallowed |
| ground stand-in walking to an ancestor, content not | roads snapping to straight lines over ground that IS displaced |

Two more from choosing constants instead of copying them: `depth_shift` scaled by `|m22|` (did
nothing), then by `|m23|` (dragged a landcover fill in front of the mountain); and an ordinal stride
of 32 per renderer, which reached the leak range with five layers.

### Extrusions are in the numbering too

`renderGeometry3D` sets `_terrainDrawLayerOffset` from the same dense numbering `renderGeometry2D`
uses. Left out of it — which it was — an extrusion draws at **ordinal 0, the ground's own level**,
while every 2D style layer is pulled forward by its own ordinal, so any decal with a real ordinal
wins the depth test against a building of any height. Two symptoms, one cause:

- **roads over buildings, but only with the drape off** — with the drape on they are baked into the
  ground and never drawn live, so nothing is left to win;
- **`billboard-line` labels over buildings, drape or not** — clipped text is `POINT` geometry and is
  not draped, so it is always drawn live.

Both were chased first to the decals' own forward pull, and neither is it: setting
`DEFAULT_LINE_CLEARANCE_METERS` to 0 changes nothing, and so does removing the decal
`glPolygonOffset(-2, -8)`. The ordinal is the whole of it. Tangram has no such gap because a
building is a style layer with an `order` like any other.

The ordinal applies **only under the shared ground**, as in the 2D path; the other paths take their
clearance from pushing the surface back instead.

## A flat quad over a curved surface is a DECAL

Lines and points are both chains of flat quads laid over a height field, so neither is coincident
with it: between its corners a quad chords over relief the ground follows, and under a depth-writing
ground it is cut away wherever it sags. Both get the same treatment in `renderGeometry2D` — a
slope-scaled `glPolygonOffset(-2, -8)`, which tracks the sag because the sag scales with the
primitive's own depth slope, plus `_terrainLineClearance`, a clearance in **metres** (the sag does
not care how far away the quad is, which neither the clip-constant ordinal pull nor an NDC-constant
depth bias can express without leaking through ridges at range).

POINT was left out of that for a while and only lines got it, which is the wrong way round: a glyph
quad of `text-clip` text is tens of metres wide against a line's few. Whole letters disappeared over
3D terrain — **and only where the ground is draped**, because a draped tile draws its surface at TRUE
depth and writes it, while an undraped one pushes its pre-pass surface back and leaves clearance by
accident. Same drop at drape resolution 512 and 2048, absent in 2D: not the raster, the depth.

The vertex shaders match: both sample the terrain at the **extruded** corner (`applyTerrain(pos +
delta)`), never at the anchor. A quad placed at the anchor's height and then offset sideways is a
flat plate whose uphill half is under the ground.

## How far the map reaches

Four separate limits end the ground, and they are easy to confuse because all four look the same on
screen. `ViewState::calculateViewDistances` applies them in this order:

| Limit | What it is | Where |
|---|---|---|
| the ray horizon | nine rays through the frustum, hit-tested against the lowest visible ground | the bisection loop |
| the draw ceiling | `DrawDistance` (default 16) multiples of the camera height over `sin(tilt)`, capped at 127 tile widths | `ViewDistance::drawCeiling` |
| tangram's rule | `2 * height / cos(tilt' + fovy/2)`, capped at 127 tile widths | `calculateViewDistance` |
| the fog | `fog-range-end` multiples of the camera-to-focus distance, going opaque | [08-lighting-sky-fog.md](08-lighting-sky-fog.md) |

**The height all of them scale is `max(orbitDistance, cameraAltitude)`** — `ViewDistance::cameraHeight`,
tangram's `m_pos.z`. The orbit distance halves with every zoom step; the altitude does not. The draw
ceiling used to scale the orbit *alone*, which made it a function of the zoom: dropping the camera
towards the ground in mountains shortened the view with every step and cut peaks a few kilometres
away, **while the tile walk — the same height times a larger factor — had already fetched them**.
Raising the ceiling there draws tiles that were being paid for anyway.

**And it is divided by `sin(tilt)`** — mapbox's `cameraToSeaLevelDistance = altitude / cos(pitch)`
(`src/geo/projection/far_z.ts`), where pitch 0 is straight down and ours is tilt 90, so their
`cos(pitch)` is our `sin(tilt)`. Straight down is unchanged; tilt 20 reaches 2.9x further. Without
it the ceiling was the same looking at the camera's own feet and at the horizon, so descending
towards a valley floor cut the range off entirely — at Grenoble a camera 400 m up drew 6.4 km while
the Belledonne it was pointed at is 20 km out. Floored at `MIN_TILT_SIN` (0.1, tilt 5.7 degrees),
because a horizontal view divides by zero, and bounded by the 127-tile walk cap as before.

`adb shell setprop debug.massif.viewceiling 0` takes the ceiling back to scaling the orbit alone,
which is the A/B for anything that reads as a depth-precision regression (labels vanishing against 3D
content is the one to watch — the label occluder packs window depth into rgb, so its precision rides
on this far plane).

The ceiling keeps tangram's **127 tile widths** cap, and that is not cosmetic: with
`ViewDistanceFactor` 0 the cull envelope stops at the far plane *alone*
(`CullWorker::calculateEnvelope`), so a ceiling free to grow with the camera's altitude is a tile walk
free to grow with it — a high camera at a high zoom would ask for hundreds of tile widths of ground.

Mapbox has no zoom-only ceiling at all. Its far plane
(`src/geo/projection/far_z.ts`) is a law of sines off the camera's height above sea level, clamped at
`10 * cameraAltitude / cos(pitch)`. Comparing the two rules at `h` = camera height:

| tilt (ours; 90 is top-down) | our rule | mapbox `far_z.ts` |
|---|---|---|
| 90 | 2.07 h | ~1.0 h |
| 45 | 4 h | 1.9 h |
| 20 | 22.9 h | 11.1 h |
| 5 | ∞, capped at 127 tiles | 115 h (the horizon clamp) |

**Mapbox's far plane is about half of ours at every tilt below the horizon**, so porting it whole
would shorten the view, not lengthen it. What is worth taking from mapbox is the *absence* of a
zoom-derived ceiling, which is the change above.

Build the demo with `-PprofileRender` (`-DMASSIF_FRAME_PROFILER=1`) and a `PROF VIEW` line names which
of the four ended the map, once a second, in kilometres:

```
PROF VIEW: zoom 15.30 tilt 20.4 | orbit 1.42 alt 3.10 height 3.10 km | terrain 0.55..4.21 km
  | ray far 61.80 ceiling 49.60 rule 71.02 -> near 0.0620 far 49.60 km | fog 2.48..24.80 km
```

`far` equal to `ceiling` means the draw ceiling cut it; equal to `rule` means tangram's rule did; well
under both means the horizon is genuinely there. A `fog` end shorter than `far` means nothing was
clipped at all — the ground is simply invisible.

## Rules to keep

1. **Never push the reference surface back.** Slack belongs on the content, forward and test-only. A
   backward push only works for opaque depth-writing content; translucent content double-blends and
   non-writing geometry leaks in the band.
2. **Content that writes depth must discard transparent fragments** (`alpha < 0.004`), or the
   transparent corners of sprite and dash quads pollute the depth buffer — white boxes and dark
   roads.
3. **Keep the ordinal out of the clip-space slack.** Ordinal-scaled clip slack was itself a leak;
   ordinal-scaled NDC pull washed out road casings and fills.
4. **Sort near to far inside a style layer** ([03-vt-renderer.md](03-vt-renderer.md)), because
   content writes depth.
5. **An emulator pass is not a device pass** for anything in this file. Every regression here was
   found on a device after the emulator was clean.

## Known open artifact

Landcover fills break into triangular shards over the hillshade **while the LOD is changing**, and
heal when tiles settle. Measured *not* to be the cause: fill subdivision (1 vs 2 cells identical) and
the depth-shift magnitude (0.02 / budget / 0.5 identical at a settled camera). The remaining suspect
is the proxy path — the ground stand-in and the content of the outgoing zoom disagreeing during the
swap. Not yet reproduced on an emulator.
