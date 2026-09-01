---
title: GL vector-tile renderer
description: "The GL draw path: style layers, batching, draw counts, shaders."
sidebar_position: 3
---

# The GL vector-tile renderer

Scope: `libs-massif/vt/src/vt/GLTileRenderer.*` and its bridge `all/native/renderers/TileRenderer.*`.
Terrain specifics are in [04-terrain.md](04-terrain.md), depth in [05-depth-model.md](05-depth-model.md),
labels in [06-labels.mdx](06-labels.mdx).

## The bridge

`TileRenderer` (all/native) owns a `vt::GLTileRenderer` and, once per frame, pushes the state the
renderer cannot know by itself: view state, interaction mode, blending speeds, layer filters, and
the whole terrain block — elevation texture provider, regular-grid mode, painter order, depth shift,
slack scale, lighting, fog, shadow map, ground tiles and layer ordinals.

That "terrain state block" is also where an elevation version change is turned into work:
`invalidateTileSurfaces` / `invalidateLabelElevation` for the changed tiles, or a full
`resetTileSurfaces` (debounced by `SURFACE_RESET_DELAY`). Measured cost of the block during a pan:
**0.04 ms** — it is not the pan hang it was once believed to be.

## Per-frame phases

```
startFrame(dt)      blend render tiles, re-anchor labels whose elevation changed, blend labels
renderGeometry()    the 2D pass, then the 3D pass
renderLabels()      glyph quads, built fresh every frame, uploaded as batches
endFrame()          sweep compiled resources whose owners expired
```

`renderGeometry2D` (GLTileRenderer.cpp:2588) is the heart of it:

1. Bucket every visible render tile's layers by **style layer index** into `renderLayerMap`.
2. In terrain mode, sort each style layer's tiles **near to far** (content writes depth, so near
   tiles must occlude far ones). The distance is computed **once per tile** into a small map — doing
   it inside the comparator made the sort 21% of the render thread, because on the terrain
   transformer `calculateTileBBox` samples the elevation manager and transforms in double precision.
3. For each style layer, in order: resolve the layer's opacity/comp-op, set the per-layer depth
   ordinal, and draw each tile's geometry (`renderTileGeometry`).

### Render tiles

A *render tile* is a target tile id plus the source tile actually available for it (possibly an
ancestor), one `RenderTileLayer` per style layer, with `active` telling whether it is the live tile
or one retained for the cross-fade. `buildRenderTiles` merges the new set with what was on screen so
a tile can blend out rather than pop.

### What a draw costs

Measured on an Adreno 610: a 6-style-layer and a 21-style-layer style submit the same ~300k indices,
but 59 draws against 500, and cost **3 ms against 20**. The per-frame cost tracks the **draw count**,
not the triangle count. `RenderStats` therefore counts draws, indices, style layers, render tiles and
surface draws separately (`geomDraws=…` lines).

The counters split a draw into `geomProgramNs` (program selection + fog uniforms), `geomTerrainNs`
(MVP, depth bias, terrain/shadow uniforms), `geomStyleNs` (style parameter uploads) and
`geomStyleEvalNs` (the colour/width functions themselves), so a regression can be attributed without
guessing.

### What splits a tile's style layer into several draws

`TileLayerBuilder` accumulates a style layer's features into one `TileGeometry` and calls
`appendGeometry()` — one more draw — whenever the next feature cannot share the vertex format or the
style table: a different geometry **type**, a different **comp-op** or **translate**, a full
16-slot parameter table, or more than 65535 indices.

**A polygon pattern is not one of them any more.** A style layer that alternates patterned and plain
fills (`polygon-pattern-file` on some features) used to split at every alternation, and that was
**48% of every geometry draw** in a 2D city frame — 584 draws where 337 (tile, style layer) pairs
actually drew. Each slot of the style table now carries a **pattern flag** (`patternScales`,
`uPatternTable`, packed into `vUV.z`), so a plain fill sits in the same geometry and the same draw as
a patterned one and simply keeps its flat colour. Only a **second, different** pattern still splits.

Measured on a Crosscall, 2D city pan: **584 → 363 draws a frame, 17.9 → 20.3 fps**, the `layers` CPU
section 21.9 → 15.1 ms ([performance-log.md 16.6](../performance-log.md)).

Lines never had the problem: they go through the shared `StrokeMap` atlas and select their dash by a
per-slot stroke id, which is the same trick one level down. The remaining floor is one draw per
(tile, style layer); going below it needs cross-tile batching, which nothing here does yet.

## Shaders

Programs are built on demand from source in `GLTileRendererShaders.h` and cached by a key made of
the shader kind plus a bitmask of feature flags (terrain, VTF, lighting, shadow, derivatives, fog,
paint surface, …). Consequences worth knowing:

- Every new flag combination is a **new program compile** on first use, on the render thread.
- A uniform the compiler drops has location `-1` from `glGetUniformLocation`, but
  `Shader::getUniformLoc` returns **0** — a valid location that then clobbers uniform 0. Always use
  `glGetUniformLocation` with a `>= 0` guard (`SkyRenderer` is the pattern to copy).

## Line borders (`line-border-width` / `line-border-color`)

maplibre's line border: a casing `line-border-width` px wide on **each** side of the line. One rule
replaces the `::case` / `::fill` attachment pair a CartoCSS style used to need.

The line quad is extruded **in the vertex shader** from `uWidthTable`, so the same vertex buffer
draws at any width. `renderTileGeometry` uses that: with a border it issues **two**
`glDrawElements` over one buffer — border table first (`widths[i] + borderHalf`, border colour),
then the fill table. Half the tesselation, half the vertex memory and half the decode CPU of a
casing/fill pair, for the same two draws.

- **Border first, for the whole batch** — that is what keeps a junction clean. mapbox orders a whole
  casing layer under a whole fill layer; this is that order inside one draw pair, so a crossing
  road's fill still covers this road's casing. Colouring border and fill in a *single* pass from the
  fragment's distance would be one draw, but the two features are in the same batch with no ordering
  between them and the casing would stripe across every intersection.
- **`line-gap-width` composes**: the border pass shrinks the gap by the same `borderHalf`, so both
  edges of a casing strip keep their border.
- **Shared with the fill, by construction**: joins, caps, `line-offset`, the end arrow and the dash
  pattern — one geometry. A solid casing under a *dashed* fill is still two rules, since the pattern
  covers the border too.
- Ordering against **other** style layers is unchanged: the pair sits where the layer sits.

## Line end arrows

`line-end-arrow: true` puts an arrow head on a line's last vertex, sized by `line-arrow-width` and
`line-arrow-length` in **multiples of the line width**. It is built in `tesselateLineEndArrow` out
of the same screen-space extrusion the line itself uses — the offsets ride the binormal attribute,
which the vertex shader multiplies by the line width — so the head keeps its screen size at every
zoom and needs no bitmap, no marker and no label.

Two details are what make it usable rather than a triangle stuck on the end:

- **The head hangs on its incenter**, and the line is pulled back to the head's base by the same
  amount. A homothety about the incenter moves every edge of a triangle by the same distance, so
  two heads about a common incenter stay a constant distance apart everywhere. Anchoring the tip
  instead pins them together at the point: the border is then zero at the tip and widest at the
  base, a wedge.
- **A casing rule must not simply repeat the numbers.** They are multiples of *its own* width, so a
  13-wide casing over an 8-wide fill draws a head 13/8 bigger — a border round the head ≈1.7× the
  `(casing − fill)/2` the shaft has. Because the two heads are similar triangles about a common
  incenter, the casing's numbers can be worked back from the border you want; `DemoStyles`
  (`maneuverCasingArrowScale`) is the formula, and it is zoom-independent — it depends only on the
  ratio of the two widths.

- **`line-arrow-only` draws the head and nothing else**, so a style can order the parts by hand:
  shaft casing, shaft fill, head casing, head fill. The head then paints OVER the line and keeps its
  silhouette where it lands on its own shaft — a U-turn seen from far enough away — instead of
  dissolving into it.
- **A head drawn on its own has a SLOT cut out of its base**, one line width wide: the width of the
  very line the same rule draws elsewhere, so it needs to know nothing about the other rule. The
  slot leaves the shaft it docks on untouched, so no bar of head colour crosses the line, while the
  shoulders, barbs and tip still paint over it. That is what makes shaft and head read as ONE
  polygon while the head still sits on top. Both alternatives were tried and are worse: no slot with
  the head on top puts a casing bar across the line; casings-then-fills (a union order) loses the
  head's outline wherever it overlaps its own shaft.

- **`line-arrow-path` replaces the built-in triangle with a contour of your own** — the `d`
  attribute of an SVG path: `M/L/H/V/C/S` and `Z`, absolute or relative, curves flattened. Any
  viewBox works, because the contour is **fitted into the arrow box** — `line-arrow-length` along
  the line by `line-arrow-width` across it — and SVG's downward y is flipped. So the two size
  properties still drive the size in path mode.
  The fitted contour is a SKELETON: what is drawn is it offset outward by half of *each rule's own*
  line width, triangulated by ear clipping. `line-arrow-scale` multiplies the fitted contour and
  `line-arrow-rotation` turns it about its centre, in degrees clockwise, relative to the direction
  of travel — an icon drawn pointing up needs 90. A custom head is CENTRED on the last vertex and
  carries no docking slot: a slot is a notch where the shaft enters an arrow head, and cutting one
  through the middle of an icon only gashes it. Centring also matters for the border: place the
  contour by its back edge instead and the casing, whose units are its own wider line, sits further
  back than the fill, so the border reads slid along the arrow rather than wrapping it.
- **One binormal unit is a QUARTER of the line width** — the line's own edge sits two units out.
  Measured, not assumed: an offset of one unit made the head's border exactly half the shaft's,
  which is what the eye caught first.
- **A casing rule must shrink its box by the ratio of the widths** (`fill / casing`). The box is in
  multiples of the line width, so repeating the fill's numbers draws a skeleton 13/8 bigger *on top
  of* its own larger offset — the border comes out about twice too thick. Scaled back, both rules
  describe the same skeleton and the offsets alone make the border, exactly `(casing - fill) / 2`.
  `DemoStyles.maneuverStyle` does it in one line; the built-in triangle needs the incenter formula
  instead, because its numbers describe the drawn shape rather than a skeleton.
- **Custom heads must be CONVEX.** Offsetting a contour that turns back on itself folds the offset
  over and the border blows out into blobs where the shape is concave — a Homer Simpson silhouette
  renders, docks and scales correctly, and its hair is a mess. Removing those loops is a polygon
  offsetting algorithm of its own (Clipper-style); a maneuver arrow head has never needed one.
  `LineSymbolizer::isConvexArrowPath` logs a warning rather than silently redrawing the shape.

  > This is the part to replace when a custom (SVG) head lands: treat the head's outline as a
  > **skeleton stroked by the line width**, the way the shaft already is, and the border falls out
  > for any shape with no arithmetic in the style. Scaling only works because triangles are special
  > — growing an arbitrary polygon is a Minkowski offset, not a scaled copy.
- **The head is solid, with no antialias ramp.** The distance field a line carries (`vDist` against
  `vWidth` in `lineFsh`) describes a band one width wide; stretched over a triangle it faded the
  silhouette near the barbs while leaving the tip hard. The head's corners carry a zero distance,
  which the fragment shader keeps opaque — so its edges are aliased, like polygon fills here.

A navigation maneuver arrow is one line with this property set: see
[15-maneuver-arrows.md](15-maneuver-arrows.md).

## Line joins

`TileLayerBuilder::tesselateLine` picks per vertex between a miter, a bevel, a round fan and a split,
from the dot product of consecutive binormals. Three things about it:

- **`line-miterlimit` is a ratio**, miter length over line width, and the stroke width must not enter
  it. It used to (`asin(min(width/limit, 1))` as the half-angle), which cut in two wrong directions at
  once: a 0.8-wide contour kept mitering into a needle five half-widths long, while any line wider than
  the limit never mitered at all. `dot = 2/limit² − 1` is the whole rule.
- **A sharp join must not overlap itself.** Two full-width quads meeting at a point overlap in a lens on
  the inside of the turn, and every pixel of that lens blends twice — which is what darkened a line with
  `line-opacity` at each hairpin. The inner corners are collapsed onto the centre line instead (mapbox's
  inner join) so the quads only touch; one triangle closes the outer gap. Offset lines keep the old
  overlapping split: their offset is `binormal × offset × side` in the shader, so a zero binormal would
  drop the offset entirely.
- **A sharp join is still a round join.** Turns sharper than a right angle leave the bevel/fan
  branch for the split branch above, and closing that corner with the single triangle cuts it flat —
  the join reads as *square*. It only shows on geometry sparse enough for a turn to pass 90°, which
  is why simplifying a route surfaced it, and it comes and goes with zoom because which vertices
  survive simplification is decided per tile zoom. The same fan runs in both branches.
- **`line-join: round` builds tangram's 5-triangle fan** (`ROUND_JOIN_TRIANGLES`, their
  `JoinTypes::round`). Getting it right took three device rounds, all invisible in a syntax check:
  the fan vertices must sit **between** the two cross-sections (appended after them, the next segment
  links to fan vertices and the line comes apart); the hub must be on the **centre line**, not the miter
  point (that point is at zero alpha in the antialias ramp); and two extra triangles must close the
  sliver against each quad's end chord, which runs from its outer corner to the miter point and so does
  not pass through the hub. Winding mirrors with the turn direction — 2D geometry is drawn with back-face
  culling on, and a fan wound one way loses every join that turns the other way. Below ~10° the fan is
  skipped for a plain miter: tangram fans at every angle, but their line shader has no AA ramp and five
  near-degenerate slivers each carrying one is a visible seam.

## Translucent layers: no single-blend pass (removed)

A translucent style layer blends a pixel again wherever its geometry overlaps itself — a line whose
vertices sit closer together than it is wide folds at every join, a route doubles back inside its own
width — which reads as darker knots along the line.

A **single-blend stencil pass** used to suppress that: one spare stencil bit per layer,
`glStencilOp(KEEP, KEEP, GL_INVERT)` marking each pixel as it was painted and a `GL_EQUAL` test
rejecting the second fragment. It was **removed**, because "one blend per pixel" is not a property of
a pixel but of a symbolizer, and the stencil cannot tell them apart:

- **A second symbolizer in the same layer is punched out.** A cartocss instance (`back/line-...`)
  lives in the SAME attachment, so it is the same vt layer. With `back/line-opacity` set, the wide
  back line paints first, flips the bit over its whole footprint, and the narrower main line — drawn
  after, entirely inside it — is rejected everywhere. The reported symptom was "I see the back line,
  I never see the line".
- **Every internal join grows a seam.** The first fragment to reach a pixel owns it, so a
  partial-coverage antialias edge can no longer be filled in by its neighbour. On a translucent
  `line-width: 10` that reads as the line breaking at each vertex.
- Tangram and maplibre have no equivalent pass. The same commit that added it also made the join
  geometry **non-overlapping** (see the join section above), which is what actually removes the
  common case; what is left is a line genuinely crossing itself, and every renderer blends that
  twice.

A style that really needs one composite still has the layer's own `opacity` + `comp-op`, which draws
the layer opaque into the overlay buffer and composites it once: no seams, but a full-screen pass per
layer, and that buffer carries no depth, so in 3D terrain the layer stops being occluded by ridges.
Measured on an Adreno 610 while the pass existed (demo route, casing + fill, translucent, scripted
pan, two reps of 25 one-second samples): **37.7 fps with the pass against 26.5 fps through the
overlay buffer**, `layers` 2.43 vs 3.62 ms — that cost is what the overlay route still carries.

## Lines over terrain

A line is a chain of quads whose width is an offset along a per-vertex binormal, and three things
about that offset are easy to get wrong. All three were, and the symptoms all looked like terrain or
depth bugs rather than line bugs.

**Clip every line fragment to its own tile.** Lines are built with a clip buffer of **an eighth of a
tile** (`TileLayerBuilder.h`, `_clipBox` = −0.125…1.125; polygons only 0.002), so every tile carries
a long stretch of its neighbours' roads and draws it — displaced with *its own* target tile's
elevation texture and lattice, which is a different DEM level than the tile that overflow actually
lies on. The same road is then painted twice at two different heights: from straight down the copies
coincide and it looks perfect, and the moment the camera tilts they separate. That tilt-only
signature is the tell. The stencil tile masks were what used to clip this, but they need a stencil
buffer and the shared-ground target has none (`GL_STENCIL_BITS` reads **0**), so they never run.
`lineFsh` therefore discards outside the tile, using `uTileUnitScale` / `uTileUnitOffset`
(vertex-frame units → TARGET tile units, set in `setupTerrainUniforms`; a **0 scale means no
elevation**, which disables the test) and a `vTileUnit` varying. No attachment, no extra draw.

> **The offset is what makes this survive a STAND-IN, and it was missing.** For content,
> `setupTerrainUniforms` is called with the *target* tile (whose elevation texture the content
> stands on) and the *source* tile's vertex frame (the vertices are source-tile-local). Source and
> target are the same tile normally — but not while a tile loads, when an ancestor stands in for it.
> With a scale and no offset, `vTileUnit = pos.xy * uTileUnitScale` put the source's unit square in
> [0, 2^dz], so the `> 1.0005` test discarded everything except the one quadrant that happened to
> land in [0, 1]. Since **every** visible tile becomes a stand-in for a second or two after an
> integer zoom step, all line content — contours, roads — vanished at every integer zoom in terrain
> mode, and only in terrain mode (the test is off without elevation). Measured at
> lat 45.210031 lon 5.730591 z14.99 tilt 26 over a scripted zoom, contour pixels per frame went
> 26k → **30** → 23 409; with the offset they decay smoothly and never collapse. Measuring the
> position from the target tile's own origin (`offset = (frame(i,3) − target(i,3)) / target(i,i)`,
> signed, not `abs()`) gives each target its own share of the ancestor, so the four together still
> paint the whole thing, each with the elevation mapping of the surface it stands on.
>
> The same scale also feeds the lattice edge-coarsening test in `commonVsh` ([terrain](04-terrain.md)),
> which is wrong for a stand-in by the same argument. It is deliberately **not** changed: adding the
> offset there moves settled contour positions (2.8 % of the frame) because it changes elevation
> interpolation, which needs judging on device rather than bundling into a clipping fix.
>
> What this was *not*, each ruled out by measurement before the clip was found: missing draw data,
> deep or empty stand-ins, the renderer not being fed, elevation-texture misses, render-tile blend
> (`blend` was 1.0 throughout), and the terrain coarsening floor `_terrainMinTileZoom` — that one
> does re-cull the whole far field at every integer zoom, but the blank is identical with
> `MaxTileZoomCoarsening` raised so the far field does not churn, so it only lengthens recovery.

**Width: tangram's model, capped at nominal.** The offset is extruded in model space and displaced
onto the terrain, exactly as tangram does (`polyline.vs` + `res/scenes/terrain-3d.yaml`), so a line
is a world quad through the projection and tapers with distance. Left unbounded it also *grows*
towards the camera until a near contour is a blob, so the projected offset is measured and **shrunk**
back to the nominal width when it exceeds it. The factor is ≤ 1 by construction — it can never
manufacture an oversized quad, which is what an unbounded screen-space fit does.

The ceiling is **this vertex's own extrusion**, `roundedWidth * length(aVertexBinormal *
uBinormalUnitScale)`, not one line width. Not every line vertex sits one width out: a round cap's
corners are at √2, and an end arrow's barbs at several. Clamping those to one width squashes the
shape they belong to back into the line's silhouette — which is what made `line-end-arrow` draw
nothing at all in terrain mode while it drew fine on a flat map.

> **`aVertexBinormal` is PACKED — its raw length is not a number of line widths.** Binormals ship
> as `GL_SHORT`, un-normalised (`GL_FALSE`), against a per-geometry scale
> (`calculateScale` in `TileLayerBuilder`, a power of two ≈ 32768 for unit vectors), which is why
> the extrusion itself reads `aVertexBinormal * (uBinormalScale * roundedWidth)`. Taking
> `length(aVertexBinormal)` alone made the ceiling ~32768× too large, so `edgeLen > nominalLen`
> never fired and **every** terrain line fell back to tangram's unbounded world-space extrusion:
> near contours grew into fat, soft-edged blobs (their antialias band grows with them), roads read
> as draped again, and neighbouring vertices no longer agreed on a width so joins came apart.
> `uBinormalUnitScale` (`1 / binormalScale`, uploaded with `uScreenScale`) undoes the packing:
> 1 for a plain vertex, more for a miter, a cap corner or an arrow barb.

> **The capped vertex takes its DEPTH from the terrain, and its XY from the screen.** Both halves
> are load-bearing. Applying the shrunk offset to `centerClip.xy` and keeping `centerClip.z` — the
> obvious way — gives the outer edge of a wide line the *centreline's* depth; on a cross-slope that
> is below the ground on the uphill side, so the depth test against the terrain surface eats the
> line in a ragged, stair-stepped band. The widest layer hits the cap first, which is why a route's
> white casing broke up while its blue fill survived, and why it only showed at a tilt. Shrinking
> the **world** position instead fixes the depth but makes the projected width only approximately
> nominal, and it drifts vertex to vertex — segments of visibly different width at z14.38. So:
> screen-space xy for the width, `mix(centerPos, edgePos, shrink)` for the depth.
>
> Ruled out first, each by measurement, before the cap was suspected: line tesselation and joins,
> the route source's simplify tolerance (real but separate — it is applied per TILE ZOOM in
> `MBVTTileBuilder::simplifyAndCacheLayers`, so a coarse tile collapses hairpins into chords),
> `TerrainOptions::MeshResolution` (32/64/128, no effect) and `Options::TileLODFactor` (no effect).
> The two A/Bs that settled it: with the content depth test disabled the casing is complete, and
> with the cap disabled the casing is complete but every line is visibly fatter.

**Antialias in device pixels, from a per-frame constant.** The ramp is one unit of the quad, and a
unit is not a pixel: widths are unscaled-DPI units, so at 2.6× density one unit is ~1.8 device px and
a 1 px contour is almost entirely ramp — that is what "blurry contours" was. `uAntialiasScale`
(screen height ÷ normalized resolution, set by `TileRenderer`) makes the ramp one device pixel;
measured on a contour at z17, the edge transition went 5 px → 2 px and the solid core 4 px → 8 px.
`GL_OES_standard_derivatives` is **not** exposed on this context, so an `fwidth`-based ramp is dead
code — check by forcing `a = 0` in that branch and seeing whether the line still draws.

> **Never measure a line's screen width per vertex against `roundedWidth`.** A `screenHalfWidth /
> roundedWidth` varying is in the *tile's* units, and the terrain LOD picks tiles up to
> `MaxTileZoomCoarsening` (default 3) levels below the camera zoom on a grazing slope. There the
> ratio is wrong by the zoom difference, the antialias ramp becomes a hard cut, and lines break into
> fat wedges and detached triangles. For the same reason, never divide the offset by
> `centerClip.w + deltaClip.w`: that is a second perspective divide by something that is not a
> position's w, and it explodes when the offset is large in world units.

## Polygon patterns keep their phase across a tile border

A `polygon-pattern` is a repeating texture sampled with `GL_REPEAT`, and the texcoord a vertex
carries is tile-local: `TileLayerBuilder::tesselatePolygon` gives the tile a phase `u0` and a step
`du_dx` across it. The fragment stage samples `uPattern` at
`texCoord / (texCoordScale * widthScale)`, and `texCoordScale` is only the int16 packing scale, so
it cancels — **one pattern period is `bitmap->width * widthScale` in texcoord units.**

`u0` has to wrap at exactly that. It used to wrap at the **bitmap's width**, which is the same
number of texels but not the same units, so a fraction of a period was dropped at every tile
border and the pattern stepped across it. MapTiler's 25 px construction hatch spans 18.2 periods
per tile — a fifth of a period lost each time. The accumulation is done in `double`: a z21 tile
index reaches 2^21, and a float step loses the remainder long before that.

Wrapping at a period leaves the pattern's **size** untouched, which matters — the shipped eink
style spans 0.9 (a 512 px scrub) to 14.2 (a 32 px vineyard) periods per tile, and rounding the
step to a whole number of periods instead would have redrawn the big ones 11% smaller.

It only shows at high overzoom. At z17 a drawn tile is most of the polygon and the few borders fall
outside it; at z21 a tile is a few hundred pixels and the borders are all over a single polygon,
which is why a hatch looked fine zoomed out and broke up zoomed in.

Measuring it: a diagonal hatch is invariant under a 1 px diagonal translation, so
`|px(x, y) - px(x+1, y-1)|` averaged down a column is ~2 everywhere and spikes at a discontinuity.
Before, the two tile borders on screen scored 279 and 144 (140x and 72x the median); after, nothing
exceeds 8 — the pattern's own stripe edges. Estimating the stripe period per window and comparing
phases does NOT work: a fractional error in the period accumulates across the gap and reads as a
jump that is not there.

## Style evaluation

CartoCSS values may be functions of view state (zoom, style parameters), so colours, widths and
opacities are evaluated per frame per style layer through small caches keyed by the function plus
view state (`_colorFuncCache` and friends, with `styleFuncLookups`/`styleFuncMisses` counters).

## Interaction with the rest of the frame

- The renderer holds one mutex covering its tile/label state. The label placement worker holds it
  for the whole of `buildLabelMaps`, which is the only place the GL thread has been observed
  waiting on it (`mutexWaitNs`); during pans it has measured **0.00 ms**.
- `endFrame` sweeps every compiled-resource map (bitmaps, surfaces, geometries, label batches)
  looking for expired owners — cost tracked as `endFrameNs` / `endFrameSwept`.
- Client-side vertex arrays and bound VBOs are a cross-renderer hazard: the terrain paint pass once
  left `GL_ARRAY_BUFFER` bound and `SkyRenderer`, which draws from a client array, turned its quad
  into an offset into that buffer — the sky went black. **Unbind after every draw loop.**
