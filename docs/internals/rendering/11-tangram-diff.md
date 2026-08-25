---
title: vs tangram-ng
description: Where this renderer differs from the reference implementation, and why.
sidebar_position: 11
---

# Us vs tangram-ng: what differs, and why

Scope: a single place to answer "why don't we just do what tangram does?". Everything here was read
in their source, not assumed. Their paths are relative to a [tangram-ng](https://github.com/farfromrefug/tangram-ng) checkout.

The standing rule is in [the render-pipeline index](index.mdx#two-rules-that-shaped-the-render-code): **where they do
something differently, we adopt their way.** This page therefore has two kinds of entries — *ported*
and *still different*, the latter with the reason it is not simply copied.

## Side by side

| | tangram-ng | this fork | state |
|---|---|---|---|
| terrain surface | ONE shared static 64-grid VBO for every tile, per-tile uniforms (`core/src/style/rasterStyle.cpp:61`) | same shared grid (`buildCompiledTerrainGridSurfaces`), resolution = `MeshResolution` | **ported** |
| content on terrain | displaced per vertex, one `texture2D` fetch (`res/scenes/terrain-3d.yaml`) | same | **ported** |
| content depth | `gl_Position.z += (proxy − layer)·(2⁻¹⁹·w + depth_shift)`, `depth_shift` a flat 0.02, `proxy *= 48` for the raster | same, with the shift derived from the stack's ordinal span so the *budget* matches | **ported** ([05](05-depth-model.md)) |
| near plane | `m_pos.z / 50` (`core/src/view/view.cpp:452`), with the camera held a distance from the TERRAIN | `min(focus distance, height over the terrain under the camera) / 50` — their rule against our clearance model ([04](04-terrain.md#near-and-far-planes)) | **ported with a difference** |
| pinch / rotate gesture | scale and angle from the platform gesture detector, i.e. the SCREEN (`core/src/util/inputHandler.cpp`) | same; the pan stays world-anchored and is capped below tilt 15 by their `getTranslation` guard ([04](04-terrain.md#the-camera-against-the-terrain)) | **ported** |
| camera against the terrain | zoom-out push from the depth at the screen centre, eye lifted to elevation + 2 m (`core/src/view/view.cpp:404`) | clearance over the ground under the camera, as a zoom bound plus a per-frame correction | **different** |
| what the zoom measures | distance to the terrain at the screen centre (`m_zoom`, `view.cpp:403`); the view height is derived from the zoom, never free | distance to the focus, whose height the application owns (free roam, panorama) | **different — see below** |
| zoom pivot | 2D ground translate (`View::translate`, `view.cpp:258`) | same: the pivot shifts the map along the surface only ([04](04-terrain.md#the-zoom-pivot-sank-the-focus-and-everything-was-drawn-at-the-wrong-scale-fixed-2026-08-13)) | **ported** |
| per-layer depth pre-pass, stencil tile masks | none anywhere in `core/src` | none (shared ground) | **ported** |
| map background | the framebuffer clear colour (`core/src/map.cpp`) | global terrain base fill before all layers; no per-tile background meshes | **ported** |
| contour labels | generated from the elevation texture, no contour geometry (`core/src/style/contourTextStyle.cpp`) | label stubs in `ContourTileDataSource`, same algorithm | **ported** ([07](07-hillshade-contours.md)) |
| hillshade / contours / hypsometric | fragment blocks on the terrain raster draw (`res/scenes/hillshade.yaml`) | hillshade and contours are a paint/shader block; hypsometric is still its own layer | **partly** |
| line width | extrude in model space, displace per vertex, no ceiling (`core/shaders/polyline.vs`) | same, capped at the nominal width so a near line cannot grow into a blob | **ported with a bound** ([03](03-vt-renderer.md#lines-over-terrain)) |
| arrow-ended lines | none — an arrow is a sprite in the scene | `line-end-arrow`, built into the line tesselation so shaft and head are one shape | **ours** ([03](03-vt-renderer.md#line-end-arrows)) |
| line antialias | none — hard-edged quads (`core/shaders/polyline.fs`) | ramp over one device pixel (`uAntialiasScale`) | **different — we antialias** |
| content subdivision | none at all | area fills to two surface cells; lines cut at the lattice | **different — see below** |
| elevation texture | source raster bound directly, ancestors via uv sub-rects, edges extrapolated in-shader (`res/scenes/elevation.yaml`) | per-tile CPU re-encode with a 1-texel border from up to 8 neighbours | **different — see below** |
| tile LOD | subdivide while screen area > `(2·pixelScale·256)²` (`core/src/tile/tileManager.cpp:214`) | same rule, `Options::TileLODFactor` scaling it | ported whole |
| LOD tile height | terrain depth at the screen centre, one value per frame (`View::getTileScreenArea`) | each tile's own elevation band midpoint | **different — see below** |
| tile decode threads | 2 (`SceneOptions::numTileWorkers`) | 1 (`Options::setTileThreadPoolSize`) | **different — measured not to matter** |
| terrain depth read-back | worker thread, shared context, half res, never waited on | worker thread, **unshared** context, submit-interval limited | ported with a difference |
| terrain shadows | none | cascaded shadow maps (currently off on the shared ground) | **we are ahead** ([08](08-lighting-sky-fog.md)) |
| style system | YAML scenes, one global ordered style list | CartoCSS + a composite layer that buys back a single ordered list ([09](09-composite-layer.md)) | structural |

## The differences that are deliberate

### Area fills are still subdivided

Tangram does not subdivide anything, and for lines neither do we (they are cut exactly at the
surface lattice, which is cheaper *and* exact). Fills are the one place their model cannot be copied
verbatim: **their terrain base map is a raster inside the ground draw**, so they have no large flat
polygons draped over relief to begin with. Ours does — a landcover polygon can span a whole valley —
and an un-subdivided one chords far enough below the displaced surface that no affordable
`depth_shift` covers it.

Two surface cells is the measured compromise (20.6 fps against 16.6 for one cell, with the artifacts
that source density shows still absent). See [02-tiles.md](02-tiles.md#geometry-density-what-gets-subdivided-and-why).

### The elevation texture is re-encoded

Their scheme is cheaper: upload the tile's own raster once, address ancestors through uv offsets,
extrapolate edges in the shader. Ours re-encodes a padded texture per tile with borders taken from up
to eight neighbour grids, including a **cross-level box filter** along shared edges.

That border machinery is a seam feature they do not have: it is what makes DEM tiles from different
zoom levels meet without a visible ridge. The port that keeps it is to upload the grid's own samples
and patch the borders as small `glTexSubImage2D` strips — not to drop the feature.

### An icon and its name are one label, not two

Tangram builds a `SpriteLabel` for the icon and a `TextLabel` for the name and links them
(`setRelative`, `pointStyleBuilder.cpp`): the pair collides as two objects, and `optional` decides
whether the parent survives its child being occluded. A shield here has always been ONE label whose
glyph run holds the icon and the text together, and every shield property (dx/dy, halo, plate, the
placement rules) is written for that one object.

So the anchor mechanism was ported and the object model was not: `TextLabelStyle::anchors` is their
`Label::Options::anchors`, in their order, tried by their retry loop — but a side is a text layout
inside one label rather than a second label moved by `anchorDirection() * dim * 0.5`. What that
buys is that all the shield properties keep working unchanged; what it costs is that the icon and
the name cannot win or lose a slot independently — "icon placed, name dropped" is the
`shield-text-optional` variant, not a separate object. Their three pre-built alignment ranges have
no equivalent because CartoCSS centres every line within the block already, so alignment only moves
the block and one glyph run covers all sides. See
[06-labels.mdx](06-labels.mdx#anchored-shields-the-name-takes-a-free-side-fork-specific).

### The LOD height is per-tile

Their `getTileScreenArea` projects every tile at the terrain depth under the screen centre. That is
right for their camera, whose zoom is *defined* by that same depth (see the next section), and wrong
for ours, which lets the focus keep its own height: at a low tilt the focus can sit a kilometre above
the near ground, and every near tile is then projected a kilometre too high. We use the tile's own
elevation band midpoint instead, falling back to their value where the DEM is not decoded yet.

Maplibre is not a precedent for this either — its `coveringTiles` uses the per-tile elevation AABB
for the **frustum test** only, and its LOD distance is horizontal (`Aabb.distanceX`/`distanceY`).

Cost, gain and the case it does **not** fix (a mountain face given the incidence angle of flat
ground, because the area is taken on the tile's flat footprint) are in
[02-tiles.md](02-tiles.md#the-lod-height-is-per-tile-not-per-frame). Note there that maplibre's
`calculateTileZoom` reduces to the *same* rule as tangram's area test at its default fov, so it is
not an alternative worth porting for that case.

### The zoom is calibrated on the focus, not on the terrain

Their view has no free viewpoint height: `m_pos.z` is derived from the zoom
(`m_pos.z = exp2(-m_baseZoom) · worldToCameraHeight`), and the zoom the tiles and styles actually
see is taken from the **depth to the terrain at the screen centre** —
`m_zoom = clamp(-log2(viewZ / worldToCameraHeight), m_baseZoom, m_maxZoom)` with `viewZ` from the
elevation manager's depth read-back (`core/src/view/view.cpp:398-415`). So their zoom always means
"how far away is what I am looking at", whatever the terrain under the camera does.

Ours calibrates on `dist(camera, focus)` and lets the focus keep its own height — deliberately, so
an application can lift the viewpoint (free roam, the peak finder panorama). Over a ridge with the
focus on the z=0 plane, that distance overstates the distance to the visible ground, and content is
drawn slightly too coarse and too large.

One half of the gap is already closed: their pivot correction is a **2D ground translate**
(`View::translate`, `view.cpp:258`), and ours now shifts the pivot along the surface only for the
same reason — taking the full 3D offset let a pinch on a slope sink the focus hundreds of metres and
turned the mild error into a gross one
([04-terrain.md](04-terrain.md#the-zoom-pivot-sank-the-focus-and-everything-was-drawn-at-the-wrong-scale-fixed-2026-08-13)).
The other half — deriving the render zoom from the terrain depth — is not ported: it changes what
`getZoom()` means for the tile walk, every zoom-dependent style function and every label size at
once, and it needs the screen-centre depth every frame (we have the terrain depth buffer, but it is
read back for billboard occlusion on its own schedule).

### A translucent layer is NOT forced to paint each pixel once

Tangram has no equivalent of the single-blend stencil pass this fork briefly carried, and neither do
we any more: scoped to a style layer, "one blend per pixel" punches a second symbolizer
(`back/line-...`) out of the layer that contains it and turns every antialias join edge into a seam.
The non-overlapping join geometry we took from them is what removes the common case; a line genuinely
crossing itself blends twice for them too.
[03-vt-renderer.md](03-vt-renderer.md#translucent-layers-no-single-blend-pass-removed) has the
measurement and the alternative (`opacity` + `comp-op`).

### Draped fills (the old path) are being removed, not maintained

Not documented here on purpose; see [the render-pipeline index](index.mdx#two-rules-that-shaped-the-render-code).

### Coincident extrusion walls are deduped; neither tangram nor mapbox does that

`Builders::buildPolygonExtrusion` extrudes each polygon on its own, and mapbox's
`fill_extrusion_bucket` does the same. Both rely on the tiles not containing a `building` and its
`building:part` on the same footprint edge — true of Mapbox Streets, not true of every OSM pipeline,
and the two coincident walls z-fight into a stipple that reads as shadow acne. We drop the covered
range at tesselation instead. Mechanism, discriminating test and known gaps in
[08-lighting-sky-fog.md](08-lighting-sky-fog.md#coincident-walls).

### The view distance can be pinned in metres, and the terrain flattens itself

Tangram has no absolute view distance at all: `view.cpp` computes `far` from `m_pos.z` alone, so
their drawn ground is scale-invariant and a zoomed-out view can never end short. Ours can be pinned
in metres (`TerrainOptions::ViewDistance`), which a panorama along the ground wants and a zoomed-out
map does not — hence the `max()` in
[04-terrain.md](04-terrain.md#an-absolute-view-distance-only-extends-the-rule).

Their zoom-dependent lever is `View::setMaxPitchStops` — a max-pitch ramp over zoom, the mapbox
arrangement, and unused by any scene in the tree. We took the other route and flatten the terrain
instead of forbidding the camera, on a parallax criterion rather than a zoom one
([04-terrain.md](04-terrain.md#auto-flattening-when-3d-stops-earning-its-cost)). Tangram has nothing
equivalent: their terrain is on or off for the whole session.

## Measuring against them

`PROF` is ours only and is **not comparable** to anything they report — it read 20–27 fps for a
config a cross-app instrument put at 11–13. Use SurfaceFlinger for both:

```sh
adb shell dumpsys SurfaceFlinger --timestats -disable ; --timestats -clear ; --timestats -enable
# drive the motion, then
adb shell dumpsys SurfaceFlinger --timestats -dump --maxlayers 8
```

Read `averageFPS` of the `SurfaceView[<pkg>/...](BLAST)` layer. Two traps: our app also reports an
activity-window layer that reads ~30–50 fps and means nothing, and `dumpsys gfxinfo` counts only the
UI layer because the map renders on its own thread.

Their demo APK is prebuilt at `platforms/android/demo/build/outputs/apk/release/demo-release.apk`.
**Tap the "3D" chip after every launch** — it sets `global.terrain_3d` *and* tilts to 1.0 rad, and it
does not survive a restart. 1.0 rad ≈ **tilt 33** in our convention.

**The last published head-to-head is not valid.** It compared their release APK against ours built at
`-O0` ([10-performance.md](10-performance.md#build)). Re-run it before quoting any gap.

## GeoJSON tiling

Tangram's `ClientDataSource` is a thin wrapper over mapbox **geojson-vt**, and we now use the same
library — but not its `GeoJSONVT` class, whose root, per-node tile materialisation and stop
condition are tuned for batch tiling rather than for tiles cut on demand. What we kept, what we
changed and the device numbers are in [02-tiles.md](02-tiles.md#geojson-tiles-the-on-demand-pyramid).

One thing they do that we also do now: properties live outside the tiler (`m_store->properties[id]`
there, `MBVTLayerData::infos` here), so clipping never copies one.

## Fog and sky: mapbox, not tangram

Tangram has neither a distance fog nor a sky beyond a flat colour band, so this subsystem is
modelled on mapbox-gl-js instead — the model and the property names, not their code (mapbox v2+ is
under their own terms; the scattering comes from the public-domain `glsl-atmosphere` and Bruneton's
paper). Full description in [08-lighting-sky-fog.md](08-lighting-sky-fog.md). Two places where we
knowingly differ from mapbox:

- **The fog colour's alpha is applied once, not twice.** Mapbox multiplies by `u_fog_color.a` inside
  both `fog_opacity` and `fog_horizon_blending`, so their ground carries **a²** and their sky
  carries **a**. That is invisible at their default `a = 1` and wrong for any translucent fog. Here
  `fogHorizonBlend` returns the pure geometric factor.
- **No pitch gating.** Mapbox fades its fog in over `smoothstep(45°, 65°, pitch)`, i.e. a top-down
  map has no fog at all. A peak-finder camera in this SDK sits at tilt 25 and wants haze, so the
  gate is deliberately not ported.

One thing we do that mapbox does not: **the fog colour is lit by the sun** before it is used
(`resolveFog`), so a haze tuned for daylight darkens through the night instead of floating bright
white over a dark map.
