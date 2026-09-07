---
title: 3D bridges
description: How a bridge deck stands on the chord between its portals - the unions, the cache, the deck extrusion and its drape, the opt-in switch - and what is still wrong.
sidebar_position: 17
---

# 3D bridges: spans, chords and decks

Scope: everything behind `TerrainOptions::setBridges3DEnabled`. What the style has to say is on
the feature page, [3D bridges](../../features/3d-bridges.md); this page is the mechanism, its
history, where the code lives, and the open problems. It grew out of the terrain page, which
still owns the DEM, the surfaces and the drape it builds on.

## Where the code lives, and how modular it is

| Piece | File | Owner |
|---|---|---|
| Pure geometry: portals, chord parameter, meet/merge rules, borrow rule | `vt/SpanGeometry.h` | libs-massif, host-tested |
| The state machine: unions per cull, chord cache, portal reads, per-vertex bases, label chords, unresolved ends, the on/off switch | `vt/SpanResolver.h/.cpp` | libs-massif, host-tested with a fake elevation provider |
| Deck light on the roof | `vt/SpanDrapeLight.h` | libs-massif, host-tested |
| Span records, chord slot per vertex | `vt/TileGeometry.h`, `vt/TileLayerBuilder.*` | libs-massif |
| The GL side: span drape bake and sampling, `SPAN` shader flags, "an unresolved deck is not drawn", the ground drape on the overhang | `vt/GLTileRenderer.*`, `vt/GLTileRendererShaders.h` | libs-massif |
| Style properties `*-elevation-mode` | `mapnikvt` symbolizers | libs-massif |
| Span drape bake loop, ground drape hand-over | `renderers/MapRenderer.cpp` | SDK |
| Reference tile fetches for stranded pieces | `layers/VectorTileLayer.cpp`, `layers/TileLayer.cpp` | SDK |
| Chord height reads (DEM fallback) | `renderers/utils/ElevationTextureCache.cpp` | SDK |
| The option | `components/TerrainOptions.*`, `renderers/TileRenderer.cpp` | SDK, SWIG, facade |

`SpanResolver` is the module. `GLTileRenderer` owns one and calls it at three moments - the cull
(`build`: the tiles, the reference tiles, the visible tile ids, the elevation version), the frame
(`resolve`, one geometry at a time) and label anchoring (`chords`) - and reads back the ends it
could not chord and whether a label needs re-anchoring. The resolver has no GL, no lock (the
renderer's mutex guards every call, as it did when this was renderer state) and no view: the
elevation provider and the metres-to-internal scale are handed in, the tile matrix is a pure
function of the tile id. That is what lets `tests/vt/SpanResolverTest.cpp` drive it with a fake
provider: build a span line through the real `TileLayerBuilder`, hand the resolver a ground that
answers by position, and read the bases back.

What stays outside it is GL or SDK plumbing: the span drape (four functions, a bake into a
texture the owner allocates), the shader flags, the draw-time rule for an unresolved deck, the
bake loop and the reference tile cache. Each is a ten-line hook that calls into the layer.

Before the extraction (2026-09-07) all of the state machine was ~460 lines of `GLTileRenderer`
members; the opt-in switch made it inert when off but not separate.

## What the reader needs from the terrain page

The DEM, the elevation texture and node field (`ElevationTextureCache`), the regular-grid surface,
the drape bake and cache, the auto-flatten ramp: [3D terrain](04-terrain.md). A chord is two
`getDisplayHeight` reads against that texture, a deck is a `POLYGON3D` extrusion whose base is
patched on the CPU like a building's, and the roof's drape is the same bake the ground gets.

## The mechanism


A road on 3D terrain is **draped** — painted into the terrain texture — so it follows every bump
the DEM has. That is right for a road on the ground and wrong for one on a bridge: the deck sags
into the valley it crosses, and a tunnel climbs over the hill it goes through. A DSM makes it
worse, since it catches the deck itself as terrain and spikes the middle upward.

**Opt-in: `TerrainOptions::setBridges3DEnabled`, default false.** Off, the vt renderer's
`_spansEnabled` gate makes every span feature drape like the ground and skips every span deck:
`buildSpanUnions` returns before touching a piece, `resolveSpanBases` answers "unresolved" without
a read, `collectSpanDrapeTiles` names no tile (so the owner bakes no span drape and hands none
over), `collectUnresolvedSpanEnds` is empty (no reference tile fetches), and the line, polygon
and extrusion programs are built without the `SPAN` flag. What remains is the per-geometry
"has span records?" test that a map without spans made already, and the span records and the
per-vertex chord slot the builder still emits for a styled span (a few bytes per vertex of a
bridge). Turning it on later resolves from a clean cache; turning it off clears the chords and
re-anchors the labels. Not measured as an A/B on device; the demo's `--es bridges3d false` is the
switch for one.

`line-elevation-mode` / `polygon-elevation-mode` (`drape` | `span` | `underground`) mark the
features that do not lie on the ground. A span leaves the drape bake by construction —
`isDrapeableGeometry` returns false for any geometry carrying span records, whatever the layer
filter says — and takes its height from a **chord** between its two portals instead of from the
DEM under it. An `underground` feature is parsed and carried but DRAPES like the ground for now:
laid straight between its portals under the terrain it showed only where the ground dipped below
its chord, which read as tunnels missing at most zooms; mapbox draws a tunnel on the surface in its
tunnel style, and so does this until a see-through exists.

### Portals, and why the tile is the test

A span's portals are the feature's own two ends. The tile grid cuts a long bridge into pieces, and
an end the tile cut is not a portal: a chord between two cut points dives to whatever the ground
does at the cut.

The classification tests the **tile**, not the clip box. The source clips at its own buffer
(mapbox: 1/64 of a tile), so every cut end lands well inside our 1/8 clip box and would read as a
real portal — which is what drew the Millau viaduct as two 30% ramps and a middle. The same point
is inside the *neighbouring* tile's copy, which is where its portal is seen.

### Joining the pieces

The pieces are matched **by geometry, not by feature id**: `LineSymbolizer` passes
`FeatureCollection::getLocalId`, a layer offset plus an index, so one OSM way gets a different id
in every tile it crosses. Two pieces are one structure when the ends the tile *cut* meet — the
buffer makes neighbouring copies overlap rather than touch, so this is proximity (13–18 m between
the Millau pieces at z15) with a direction test to keep a crossing bridge out of the chain, and a
**lateral** test: each cut end must lie within 25 m of the other piece's line
(`SpanGeometry::MEET_LATERAL_TOLERANCE`). The radius alone is a tenth of a tile, 245 m at z14, and
at the reference zoom it chained every Seine bridge crossing the same tile edge into one group —
parallel, so the direction test passed them — whose diameter no chord could span (measured: 111 of
194 groups, up to 34 pieces). A continuation is on the same line; a neighbour is off it by its
spacing.

Joined, the Millau viaduct resolves as one chord — **2472 m at 3.3%** against 2460 m at 3.025% in
reality when it was first measured, **3440 m** on the tile set measured since (see below). The
portals are sampled at the junctions, where the approach road is draped and so sits
on the DEM — anchoring the deck to the same value is what makes the two meet instead of stepping.

A resolved chord is remembered in a small LRU, because a bridge's portals are a property of the
world and not of what is on screen: zooming into one end drops the far piece from the visible set,
and without the cache the chord shortens to whatever is still loaded and the deck changes angle.
A piece with no portal of its own borrows a cached chord by its own **midpoint**, since a piece in
the middle of a long deck is cut at both ends and has none to offer. Which cached chord matters:
a structure leaves one per feature (bed, deck, rails, road), portals metres apart and heights
decimetres apart, and every one passes the midpoint test. A piece that kept one portal takes the
shortest chord ending there — its own feature's, resolved uncut in a coarser copy — and a piece
cut at both ends the longest (`SpanGeometry::borrowChord`); the first cache hit changed as the
cache moved, and the deck jumped with it. The cache entry also carries the chord's **heights**: a
union is rebuilt from scratch every cull and keyed by the piece's tile, so a piece whose tile had
just entered the view had no previous pair to keep and hid until the DEM under a portal answered —
the flicker on every pan, one tile of a deck drawn and the next not.

**The same deck from two source tiles resolves two chords.** Each tile clips the ring where it
likes, so at Pont Neuf z19 the two copies ended 30 m apart and read 1.3358/1.3148 against
1.4256/1.1267 — the second's ends on the quay slopes — and the deck stepped 38 px where the source
changed. The dual-carriageway merge (middles within 100 m, lengths within 0.8–1.25) rejected them
at ratio 0.73. A chord whose both ends lie ON another (`chordLiesOn`) is the same structure whatever
the lengths; the merge runs longest first so the copy with the better-placed ends is the one kept —
**after** every road chord (`SpanUnion::line`), and among chords the cache already holds with
heights, the one used last cull first (its stamp; only merge winners are remembered). The copies
of one bridge at z18, z19 and z20 give three chords metres apart, which set is present changes with
the zoom, and longest-first crowned a different one each cull — the deck jumped between their
heights on every zoom step (Pont au Double: three chords over one zoom in and out, one after).

**A deck polygon's portals are the centres of its ends, not its farthest corners**
(`SpanGeometry::endCentres`). The corner chord runs diagonally and ends over the bank beside the
road, where the node field is pulled down by the water: at Petit-Pont the corners read 34.6 m against
36.0 m raw and 35.9 m at the road's own ends, and the deck sat 1.3 m under both approaches. The end
centres sit on the road the deck carries. A deck split lengthwise by a tile edge gives two half-width
strips with the same ends, merged by `chordLiesOn`.

**The offset under a deck is metres, not exaggerated metres.** `min-height: -7` puts the base 7 m
under the chord and the shader adds the 7 m of thickness back unexaggerated (`aVertexHeight *
uHeightScale`). Converted with the terrain texture's `metersToInternal`, which carries the
exaggeration, the two cancelled only at exaggeration 1: through the auto-flatten ramp the base sank
with the ground while the thickness did not, and the deck ROSE 7 m as the terrain fell (Petit-Pont,
measured base 1.149 → −0.230 over the ramp with the roof staying put). The offset now uses the
renderer's plain `metersToInternal`.

**A chord is re-read whenever the elevation version moves, and a failed read retries every frame.**
The flatten ramp is an exaggeration ramp, so a pair read once at a cull stayed at its 3D height while
the ground sank; the flat state drops the DEM, so through the rise every read failed and the pair
kept was the flattened zero — the deck stayed on the water and the POIs on it with it, until the
next cull. `CachedChord::baseVersion` drives the re-read in `resolveSpanBases`; a failure leaves the
geometry's version unset so it is asked again next frame; and the pair always comes from the chord
entry, never from the union's copy — the copy is only what the cull saw, and the entry may have been
refreshed by another piece on the same chord in the same frame. The label chords (`_spanChords`) are
rebuilt whenever the version moved and the rebuild re-reads the entries itself: labels anchor before
any deck resolves in the frame, so a rebuild from the entries as the last frame left them kept every
POI one ramp step behind the deck, and there for good after the ramp's last frame. Each record
remembers the chord it resolved on (`TileGeometry::SpanChordRef`): a piece drawn from a tile the cull
no longer holds keeps reading that entry instead of freezing on its last bases.

**Going flat anchors every label at 0.** The flatten ramp ends when the ratio reaches 1, and at
that frame the terrain is no longer active (`TerrainOptions::isActive`), so the SDK withdraws the
label elevation provider. The last anchoring had run a step earlier, on the ramp's previous ratio,
and with no provider nothing anchored the labels again: measured after a tilt-90 flatten at
Petit-Pont z19, 90 of 228 labels off the ground, the highest 0.68 internal units (~17 m) over a
ground drawn at 0 - road names, one-way arrows and crosswalk symbols "in the sky", worst at z21
where a metre is a screen-width. `setLabelElevationProvider` now anchors every label at 0 when the
provider it had is withdrawn. The rise re-anchors through the ramp as before; a symbol on a deck
sits on the chord's last pair until the DEM answers again, as the deck does.

**Past its road's portals the roof is ground.** A deck ring runs on past the road's bridge
segment - at Petit-Pont 10 m onto the south quay, and a skewed north-east corner some 9 m past the
north portal - while the road chord (which the deck adopts through the merge; its own end centres sit
at t = 0.99 and 0.04 of it) ends where the tiler split the way. The roof there covered the crosswalk
and the approach drawn on the ground, at the portal's height (Martin's "deck overlaps the ground
roads"); cutting the deck at the portals instead stopped it short of the structure (his "bridge stops
too early", south end). Each span vertex now carries its UNCLAMPED chord parameter
(`SpanGeometry::chordParamRaw`, `TileGeometry::chordOffset`, written with the base), and outside
[0, 1] the roof wears the GROUND drape (`uGroundDrapeTexture`) in place of the span drape: the
owner's composite for the drape tile holding the render tile, handed over per frame with the
sub-rect it is drawn through (`MapRenderer` → `TileLayer::setGroundDrapeTextures`, next to the span
drapes; the vt renderer's own `_drapeTextures` are not in play when the SDK composites the stack,
which is why a first cut that read them drew the overhang bare). Only where the roof LIES on the
ground - within 1.5 m of it (`vSpanAbove`, `uSpanGroundTolerance`): an outline reaching over the
bank wore the water it hung above - the crosswalk and the quay road appear on the deck's overhang, which sits at ground
level there. A bed fill is simply discarded past the portals: it has no drape to wear and the ground
under it is the surface.

**The ring's ends are what the data says.** The skew cuts both ways: at Petit-Pont's north end
the east corner runs 9 m past the road's portal and the west corner stops 5 m short of it, so the
road runs bare on the ground for those metres before the approach begins. Two answers were tried
and both removed: pulling short vertices out to the portal line at resolve time left wall fins
wherever a later resolve came with another chord, and squaring each end outward to its farthest
corner at build time turned an abutment or a bastion corner into a background-coloured slab the
size of the deck, onto the quay (Pont au Change, Pont d'Arcole). The bare metres stay.

**A deck is flat, whatever the ground does at its abutment.** The two sides of an abutment
are not at one height - the quay slopes to the water - so a level deck shows a wedge of ground
through one corner and a gap under the other. A first answer bent the deck's last 12 m up to the
ground under each vertex; on Pont d'Arcole the lifted end vertices tilted the long roof triangles
and the road on the roof zigzagged the whole length of the deck. Martin's rule: a bridge is always
a full flat deck. The wedge and the gap remain, and the answer if one is wanted is on the
ground's side (grade the terrain to the deck), not the deck's.

**A piece drawn from a retained tile keeps its last bases.** A render tile held on screen while its
replacement loads is not in the cull's tile set, so its pieces have no union that cull; failing them
hid the deck for every hold — 224 such records in one zoom-step frame. `resolveSpanBases` leaves a
previously resolved geometry's vertices as they are for a record it cannot resolve now.

**Two portals are not a resolved chord.** Neighbouring tiles each hold a copy of the same abutment,
so a group whose far portal is off screen still collects two portal points — 45 m apart at z15,
across pieces running 1305 m — and that chord passed every other test here. Measured on the viaduct
at z15.16, all eleven of its pieces resolved to that 45 m chord: the deck sagged onto the DEM and
its labels went with it, which is what read as "the join fails when part of the bridge is off
screen". The rule is `SpanGeometry::chordSpansGroup` — a chord must reach across **the group's own
diameter** (95%, for the buffer overlap) — and a group that fails it is unresolved, so the cache
borrow runs and hands it the 3440 m chord it resolved at z14.

**An unresolved deck is not drawn — and does not cast.** The on-screen draw waits for the whole
piece (`renderGeometry3D`), because a deck with resolved and sentinel vertices mixed fans a wall from
the chord down to the ground. The shadow caster pass runs *first* in the frame and used to take every
visible extrusion regardless: that wall, invisible on screen, was in the shadow map, and its shadow
landed on the roofs and the water beside the bridge until the base resolved — the dark flash on the
Louvre passage and the Pont des Arts at every zoom step (2026-09-05). `renderShadowCasters` now
resolves the bases and applies the same rule.

### Everything else that stands on the deck

- The **bed polygon** (mapbox's `structure`/`class=land`) takes `polygon-elevation-mode`; its
  portals are the two vertices farthest apart, which for a deck-shaped ring are its ends.
- **Labels, POIs and one-way arrows** ask `spanHeightAt` before the terrain: lifted up to 151 m at
  mid-span on the viaduct, tapering to 0 at the abutments. The allowance scales with the span
  (2%, floor 25 m) because a long deck *curves* in plan while its chord is straight — Millau's
  ~20 km radius puts mid-deck some 36 m off its own chord, and a fixed 25 m missed exactly the
  labels standing on the bridge.
- A span is lit **flat**, not by `terrainNdl`. Borrowing the terrain's normal is right for a road
  lying on the ground and wrong for one flying over it: the deck came out shaded by the valley wall
  beneath it and stepped in tone against its own draped approach.
- A layer whose geometry is *all* spans still occupies its place in the drape unit stack. Dropping
  it shifts every later layer's coverage-mask index, which masked the whole road network away.

### The deck as an extrusion

`building-elevation-mode: span` stands a `BuildingSymbolizer`'s prism on the **chord** instead of on
the ground, so `min-height`/`height` become a thickness measured from the deck rather than a height
above the terrain (negative values hang the structure below the road surface). It is the same
mechanism the span lines already use and not a second one: both write a per-vertex `baseOffset`, so
`resolveExtrusionBases` simply hands a geometry carrying span records to `resolveSpanBases`, which
takes the base from each vertex's own position along the chord. A building keeps the old path — one
elevation query per footprint centroid, giving the flat base a building wants.

`Polygon3DStyle` therefore carries an `elevationMode`, and `TileLayerBuilder` splits the batch on it
(a deck and a building resolve their bases differently, so they cannot share one geometry) and emits
the same `SpanVertexInfo` a filled bed does. Both get their two ends from
`SpanGeometry::farthestPair` — a ring has no ends, so its span is its longest axis, found
centroid → farthest → farthest again. Getting that wrong lays the chord across the deck's *width*,
which resolves as a metre-long span and leaves the deck on the ground; `tests/vt/SpanGeometryTest.cpp`
pins it on a 2460 × 32 m ring.

What this buys beyond looks: labels, POIs and one-way arrows on a deck stop being a special case,
because the deck becomes the same "POI on top of a building" query, and the existing extrusion
shadow pass applies to it unchanged.

Two things had to be got out of the way before any of it drew, and both are worth knowing on their
own:

**A style's building height ramp flattens a deck to nothing.** A converted Mapbox Standard carries
`building-height-scale: linear(zoom, (15, 0), (15.3, 1))`, so every extrusion is multiplied by
**zero** below mb zoom 15 — which is every camera a bridge is looked at. A span deck now takes none
of the three building multipliers (the ramp, the tilt drop, grow-on-appear): they all mean "this
building is not there yet", and a bridge is structure.

**A style parameter can only narrow what draws, never widen it.** A rule whose filter is false for
the parameter's *declared* value is pruned when the style compiles and cannot come back at runtime.
`--es buildings 1` works because Standard declares `buildings: 2` and the runtime value only
removes rules; `--es deck3d 1` could never work while `deck3d` was declared `0`, and every test run
through that knob was silently testing nothing. The demo declares `deck3d: 1` for this reason, and
`--es deck3d 0` turns it off — the direction that works.

**The deck hangs under its road, in metres.** `min-height: -7; height: -0.3` is a prism from 7 m
under the chord to 0.3 m under it, and for a while it stood 6.7 m *above* the road instead: the
builder converted the offset with `calculateHeight`, which answers in TILE units, and
`resolveSpanBases` added it to a chord held in INTERNAL units — at z15 the -7 m became a rounding
error and the whole thickness went up. `SpanRecord::baseOffset` is now metres, converted where the
chord is, with the same factor the vertex shader applies to a DEM sample (`metersToInternal` from
any elevation texture, times the mercator stretch at the vertex).

**What the deck wears.** The span drape — the tile's span content baked on its own, sampled by the
extrusion — goes on the ROOF only (`vSpanRoof`): a wall's tile position runs along the deck's edge,
and sampling the drape there smeared whatever the road's edge held down the whole face as vertical
ribbing. And it is baked over the deck's own BOUNDS (`_spanDrapeBounds`, the pieces' ends plus a
margin of 4 % of the tile, never under ~25 m), not the whole tile: a deck stands above the ground
and is that much closer to the camera, so its drape is magnified past the ground's, and a
tile-wide bake of a narrow deck spent most of the texture on empty tile. `bakeSpanDrapeTile`
premultiplies `SpanGeometry::clipZoomToBounds`, `resolveSpanDrape` composes
`drapeTransformInBounds` into the sampling transform, both host-tested. In practice the roof drape
is rarely seen at all: a style whose bridge fills and casings are spans draws them LIVE on the
chord, above the roof, and live lines are sharp at any height — which is the real answer to "the
drape on a high bridge loses resolution".

Three rules that took a round each: the bounds hold the DECK's records, not only the draped road's
(a deck wider than the road's margin sampled the clamped edge texel as dark streaks); the composite
is PREMULTIPLIED like every other bake (mixed as straight alpha, a half-covered texel came out half
black — a fringe down every line); and a span extrusion **discards every fragment outside its own
tile** (`polygon3DFsh`, the `SPAN` flag, drape or no drape). That last one is the seam fix: a roof
triangle is emitted whole into every target tile it touches (`TileLayerBuilder::appendPolygon3D`
keeps a triangle whose bbox reaches the polygon clip box), so past a tile edge a neighbour's copy
of the same deck could win the depth test with a drape that stops at ITS tile — a plain strip of
roof along every cut, which read as a wall between pieces at different heights. It was never a
height step: with the drape forced off the whole roof was exactly the strip's colour. And a copy
drawn before its drape is baked is the same deck uncut with walls only near its own tile, so
wherever it won, the deck's side went missing — hence the cut applies to every span extrusion,
not only the draped ones. mapbox cuts its extrusions at the tile for the same reason. The
trade-off is the one mapbox has: a tile whose deck geometry has not resolved (`resolveSpanBases`
is all-or-nothing per geometry) is a gap in the deck now, where an uncut neighbour used to cover
it. A span extrusion also gets no ground-AO skirt (it hangs from a chord, the skirt was a halo
sliding over ground it never touches) and is skipped by the label occlusion depth pass (it hid
its own road's arrows and name).

**What lights it.** The drape is composited AFTER `applyLighting3D`, not before, and the draped
part takes `uSpanDrapeLight` — the GROUND's light — rather than the extrusion's. A draped pixel is
a finished ground pixel: the bake drew that road exactly as the surface draws it, so lighting it
again with the facade model (an ambient/sun sum in linear space, plus the roof-shade term
`u_verticalGradient.y`) applies a building's light to a piece of road. It is invisible at noon,
where that factor is ~1, and ruinous at a low sun: at Pont Neuf, z17.5, hour 22 the deck measured
**23** against a quay road at ~100 — the bridge darker than the water under it, and the bug behind
every "the bridge is black at night" report. Composited after, the same deck reads **104**.

The light itself is `SpanDrapeLight::resolve`, host-tested, and is `backgroundFsh`'s own
expression: the ambient is the floor and the sun fills the remaining headroom. It is a CPU value
because a deck is FLAT — the ground's per-fragment N·L collapses to the sun's own height, so there
is one value per frame instead of a term in the shader. A `colors-prelit` style resolves to exactly
1 (`TileRenderer::buildTerrainLighting` hands the ground a neutral light rather than none, so the
shadow still lands), which is what every converted Mapbox Standard wants — those colours already
carry their hour. The walls are untouched: they keep the full facade model and darken at night like
any building, which is the whole point of splitting the two by `vSpanRoof` rather than reaching for
`building-emissive-strength` on the deck rule. That style-level workaround does neutralise the
double light, but it neutralises it on the WALLS too, and they go flat-bright at midnight.

The union build is timed in the profile build's `RenderStats: tileSetChange … spanUnionsMs` line:
at Grenoble it is 3.5–6.8 ms per second of panning against 55–100 ms of cull work, most of it
label maps.

**A piece never seen whole.** A map opened in the middle of a long bridge holds none of its
portals, so every piece is unresolved and the deck drapes onto the valley. `buildSpanUnions` now
reports every cut end it could not give a chord, stepped a twentieth of a tile past the cut
(`SpanGeometry::beyondCutEnd`, past the source's buffer), and `TileLayer::collectSpanReferenceTiles`
fetches the tile each point lands in as a preloading tile — cached, never drawn, and handed to the
renderer as its own list (`setVisibleTiles(tiles, spanReferenceTiles)`): it joins the span unions
and nothing else, no render tile, no labels, so a reference that overlaps the view does not double
its geometry. Three levels COARSER than the piece (`SPAN_REFERENCE_ZOOM_DROP`), floored at z14
(`SPAN_REFERENCE_MIN_ZOOM`, where OSM-derived tile sets still carry their bridges; a z11 tile has
no span in it at all) and never past the data source's max zoom: a tile beyond it is the same
source data cut again at the finer grid, with the same stranded ends. A piece already at the floor
walks to its neighbour at the same zoom, one hop per cull. The zoom groups in `buildSpanUnions` run
coarsest first and remember their chords as they go (4096 of them — a city view holds one per
structure per zoom group, and at 64 the cache evicted chords the pieces on screen still borrowed),
so the fine pieces borrow the reference tile's chord in the same pass. Two plumbing faults kept
this from working at all under overzoom: the fetch-only path (`buildFetchTiles(..., fetchOnly)`,
the OOM fix — no draw data, no substitution search) never handed the tile over, and a preloading
tile's arrival triggers no cull, so with the camera still nothing ever read it. A reference
tile's arrival now refreshes like a visible one's (`FetchTaskBase::run`). And they live in their
own map (`VectorTileLayer::_spanReferenceCache`, pruned to the tiles named), not the preloading
LRU: twenty coarse city tiles at a few MB each evicted one another there, every refetch re-culled,
and the map sat in an endless 80-culls-a-second loop with the deck flipping between two chords.
The set is hard-bounded at 32 (a set full of tiles all named this cull takes no new one) and the
ends are named finest zoom first: named coarsest first, a city at z14 with a thousand stranded
ends grew it to 121 tiles, the horizon took every slot, and the union pass — quadratic then —
ran a second per build at 2 fps. The pass is bucketed by end now (a cell per tolerance, 3x3
lookup), 3.5 ms per second of panning at the same view.
Measured at Grenoble,
Pont de la Porte de France, z19.24 over a z16 source: 218 stranded ends to 8, all eight z14/z15
pieces at the horizon; the deck's 68 z19 and 16 z18 pieces share one chord and hold it through a
pan. The log line `TileLayer: N span reference tiles for M stranded span ends` is the
convergence; a piece that can never resolve (an approach ramp classed as a span) keeps one
reference tile alive and flips the count by a few, which is what the line looks like when it is
done.

**What it looks like.** Verified side-on from west of the viaduct
(`--es lat 44.0790 --es lon 2.9960 --es zoom 15.0 --es tilt 12 --es rotation 90`): the deck reads as
a solid band with thickness. From ABOVE it correctly shows only the road surface, and at a 6.5 m
thickness that is ~2 px at z14 — so "the deck is missing" from a top-down camera is the geometry
being thin, not a rendering failure. What makes a bridge read at those zooms is its shadow and its
piers, neither of which this draws yet.

The pure geometry is in `vt/SpanGeometry.h` and tested on the host (`tests/vt/SpanGeometryTest.cpp`);
none of these rules fails loudly when wrong.

## Remaining issues (2026-09-07)

Ordered by what a user sees first. None is measured beyond what its entry says.

1. **The two sides of an abutment are not at one height.** The quay slopes to the water, the deck
   is level across its width at the portal's height, so one corner shows a wedge of ground through
   the roof and the other hangs over a gap. The end band warp that bent the deck to the ground was
   removed - a bridge is always a full flat deck - so the answer, if one is wanted, is on the
   ground's side: grade the terrain to the deck under its footprint at the ends. That is a bake
   into the elevation texture at the mesh's node resolution (~10 m at z16) and it must not feed the
   CPU chord reads, which sample the same texture. Not started.
2. **A skewed corner short of the road's portal leaves bare road.** Petit-Pont's west corner stops
   5 m short; the road runs on the ground for those metres before the approach begins. Pulling the
   vertices out at resolve time made wall fins; squaring the ring's ends at build time made
   background-coloured slabs where an abutment or bastion corner was the ring's extreme. Both
   removed. A rule that knows the road's portal at build time would need the tiler's split point,
   which the ring does not carry.
3. **Deck seams at z20+.** Reported, not reproduced: the example's default auto-flatten goes flat
   over Paris at z21, where decks are skipped. Needs the reporter's thresholds or a site with
   relief. Suspects, in order: the span drape bake per target tile at the tile edge, two source
   tiles' copies of one ring resolving end centres a metre apart, the `SPAN` fragment clip.
4. **The rise after a flatten.** The flat state drops the DEM; through the rise every chord read
   fails and the pair kept is the flat-end value, so a deck and the POIs on it sit low while the
   ground rises, then jump when the DEM answers. Accepted for now; the fix is a read that succeeds
   during the rise (the grid LRU fallback in `ElevationTextureCache` gets part of the way).
5. **Underground is a drape, not a tunnel.** `underground` parses and drapes like the ground,
   which is what mapbox shows. A see-through against the terrain in front of it does not exist.
6. **The converter emits neither property.** `mapbox2css` does not translate `[structure]` into
   `line-elevation-mode`, so a regenerated style loses the annotations.
7. **The chord runs long on the viaduct** (3440 m against a 2460 m deck): the chain reaches into
   the structure past the northern abutment. Not visible at the cameras tested.
