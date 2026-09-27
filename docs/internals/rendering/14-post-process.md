---
title: Post-processing
description: Full-screen effects with access to the terrain depth, and the layers drawn above them.
sidebar_position: 14
---

# Post-processing: full-screen effects and the layers that sit above them

Scope: `MapRenderer::setPostProcessEffect`, what a fragment shader gets to work with, how the
terrain depth reaches it, and how a layer opts out of being stylized. The relief (peak-finder) look
is the worked example; the shaded terrain surface it draws over is in
[04-terrain.md](04-terrain.md#the-surface-shader).

## The pipeline

With an effect attached, the frame is redirected (`MapRenderer::onDrawFrame`):

1. `clearAndBindScreenFBO` — sky, background and all layers render into an offscreen colour
   texture with a real depth buffer, instead of the screen.
2. `applyPostProcessEffect` — optionally renders the **terrain depth texture** first, then draws
   one full-screen quad with the effect's fragment shader.
3. With no opted-out layer, that quad goes straight to the screen and the frame is done.
   With one, it goes to the framebuffer's **secondary colour texture** (`FrameBuffer::
   attachSecondaryColorTex`) — same FBO, same depth attachment — the overlay layers are drawn on
   top of it by `drawOverlayLayers`, and `blendAndUnbindScreenFBO` blits the result out.

Step 3 is the only reason for the second texture: GL cannot read and write one texture in a pass,
and re-rendering the terrain depth into the default framebuffer to get overlays depth-tested would
cost a second terrain pass (~20 ms). Swapping the colour attachment keeps the depth buffer the
scene was drawn with, so an overlay is still occluded by the ridge in front of it.

Nothing above runs when no effect is set: the split in `drawLayers` is behind the
`postProcessing` flag, and the secondary texture is allocated on first use.

## What a shader gets

`PostProcessEffect(name, fragmentShader)` takes GLSL ES 1.00 source. Uniforms the renderer sets
when the shader declares them (queried with `glGetUniformLocation` + a `>= 0` guard — see
[03-vt-renderer.md](03-vt-renderer.md)):

| Uniform | Meaning |
|---|---|
| `sampler2D uColorTex` | the rendered frame, premultiplied alpha |
| `sampler2D uTerrainDepthTex` | packed terrain depth, only with `setTerrainDepthRequired(true)`; repacked with the surface normal by `setTerrainNormalsRequired(true)` |
| `vec2 uInvScreenSize` | 1/width, 1/height; screen uv is `gl_FragCoord.xy * uInvScreenSize` |
| `float uNear`, `uFar` | frustum distances, internal units |
| `vec2 uProjInvScale` | `tan(fovy/2)·aspect, tan(fovy/2)` |
| `float uTime` | seconds since the effect was attached |
| float / colour parameters | every `setFloatParameter` / `setColorParameter`, by name |

The depth texture is RGB = 24-bit linear eye depth relative to the far plane
(`dot(rgb, vec3(1, 1/255, 1/65025))`), A = terrain coverage (0 = sky). Eye position of a pixel:

```glsl
vec3 eyePos = vec3((uv * 2.0 - 1.0) * uProjInvScale, -1.0) * depth * uFar;
```

### ...or the normal buffer

`setTerrainNormalsRequired(true)` repacks the **same texture** — there is no second target and no
MRT — as 16-bit **sqrt** depth in RG and the surface normal in BA:

```glsl
float enc    = dot(texel.rg, vec2(1.0, 1.0 / 255.0));
float depth  = enc * enc;            // 0..1 over the far plane, as above
bool  sky    = enc >= 1.0;           // no coverage channel: 1 IS the sky
vec2  oct    = texel.ba * 2.0 - 1.0;
vec3  normal = normalize(vec3(oct, 1.0 - abs(oct.x) - abs(oct.y)));
```

Two decisions worth the words:

- **The depth is curved, not just shorter.** An effect reads this buffer for its gradient, and a
  linear 16-bit depth over a 300 km far plane leaves the near field a handful of steps wide. The
  square root spends the bits where the picture is — sub-metre at a hundred metres, ten-odd metres
  at the far plane — and a threshold expressed against it is relative for free. PeakFinder's own
  panorama buffer does the same thing (an 8-bit `sqrt(a·d + b)` channel), for the same reason.
- **The normal is the mesh's, not the depth's.** Which is the whole point: a normal reconstructed
  from a half-resolution depth cannot tell a ridge from the LOD kink where two tiles of different
  mesh resolution meet, and an interior-line effect built on it inks both. This one is the
  per-vertex attribute the surface shader gets, so an LOD change moves it by whatever the two
  levels disagree about the ground and a crest turns it through tens of degrees.

Octahedral with no hemisphere fold, because a height field's normal always points up, so `z` is
recovered from `x` and `y` without a sign to store.

The buffer layout is part of the depth texture's cache key, so an app that switches the flag pays
one extra terrain pass on the frame it switches and nothing after.

This also fixed a latent bug in `ensureSurfaceAttribs`: the central difference at a tile edge used
to clamp to the tile's own edge node, which halves the gradient there. Nothing read normals across
a pixel before, so it never showed; an interior-line effect draws it as a line along every tile
boundary. The out-of-range node now comes from `ElevationManager::getDisplayHeightCached`, with the
old clamp as the fallback where the neighbour's DEM is not resident.

**That fixed the clamp, not the seam.** The edge fix makes the two sides sample the same *heights*;
it cannot make them sample over the same *baseline*. A tile carries `meshResolution` cells whatever
ground it covers, so the differentiation step halves with every zoom level, and where a z9 tile meets
a z11 one the same hillside is smoothed by four times as much on one side. The normals then differ
along the whole shared edge — visible as a shading step across the boundary even before anything
differentiates them, and as a drawn line the moment something does.

`TerrainOptions::setNormalSampleDistance` is the way out: a gradient taken from the DEM at a fixed
ground distance is a property of the terrain rather than of the tiling, so both sides return the same
normal and there is nothing to draw. This is what peakfinder.com gets for free — its panorama is one
mesh over a DEM texture array, with no tiles in the geometry and therefore one sampling rate
everywhere. The default of 0 keeps the mesh gradient, which is what a draped map wants: there the
normals are only ever shaded, and the mesh step is both cheaper and sharper.

The distance is stored on the mesh (`TileMesh::surfaceAttribSampleDistance`) so that changing it
re-bakes the attribs instead of being ignored until the mesh is evicted. Planar surfaces only — on
the globe the sample offsets are not the local frame's, so it falls back to the mesh gradient.

**And none of it was the seam.** Measured on device with `TERRAIN_MESH_TRACE`: the fixed-scale
gradient took for 738 of 789 meshes at a full `9409 of 9409` vertices, and the lines along the tile
boundaries did not move. The cause was the **skirts**.

`buildTileMesh` hangs a wall off each tile edge to cover cracks between neighbours of different
resolution. Its depth was `minLocalZ - 0.05` — and local z is the tile's own frame, where `1.0` is
the tile's **width**. So the skirt scaled with the tile:

| zoom | tile width | skirt depth |
|---|---|---|
| z13 | 4.9 km | 244 m |
| z11 | 19.5 km | 976 m |
| z9 | 78 km | 3.9 km |
| z6 | 625 km | **31 km** |

Looking down at a map, a skirt is never visible. Looking *along* the ground, every tile edge on the
horizon carries a vertical cliff kilometres deep, seen nearly edge-on — and it is in the depth
texture, so an effect drawing lines from that depth inks a straight line along every tile boundary.
That is a seam between tiles of **equal zoom**, on any DEM, which no normal can smooth: the skirt
vertices copy their edge's normal, so the shading is continuous while the geometry is a cliff. It is
also why turning `uCreaseStrength` (the demo's "ridge lines" slider) down hid the seams and every other interior ridge with them —
that term is what was drawing them.

The drop is now `SKIRT_DEPTH_METERS` (500 m) converted through the tile's own display scale. What a
skirt has to cover is the crack between a coarse sampling of a hillside and a fine one, which is
bounded by the local relief.

It is rendered by `TerrainRenderer::renderDepthTexture` at **half resolution** by default
(`TerrainOptions::PostProcessDownscale` = 2; the occlusion read-back has its own `BUFFER_DOWNSCALE`)
with nearest filtering, and — for the effect path only — at the terrain's **full mesh resolution**. The occlusion read-back keeps the cheap 32-cell cap because it samples
points; an effect that draws *lines* from this depth would otherwise draw the depth mesh's own
triangulation, which is what the first attempt did (bright facets all over the near field).

## The relief outline effect

The SDK carries **no** effect of its own: `PostProcessEffect(name, fragmentShader)` takes the
shader as a string, the way `SkyOptions::setShaderSource` and `TerrainOptions::setSurfaceShaderSource`
do, and everything an effect can read is a documented uniform (see the header) or a named parameter
the app sets. The relief look — ink lines on paper over the shaded surface — therefore lives in the
app: `DemoStyles.reliefOutlineShader()` in the demo. There was a built-in
`CreateReliefOutlineEffect()` factory; it was removed, because a peak-finder look is not something
an SDK should have an opinion about.

Three findings from making that shader match the reference (PeakFinder, and farfromrefug/geo-three)
— they are about the depth texture, so they apply to any effect drawing from it:

- **Sample at least one depth texel apart.** With a step below `PostProcessDownscale` pixels the four
  neighbour samples land on the same texel, the tangent vectors come out zero, and
  `normalize(vec3(0))` is undefined — it painted the entire near field flat grey. `uDepthTexelSize`
  is the floor.
- **A silhouette belongs to the nearer side.** Testing `abs(neighbour - depth)` draws every ridge
  twice, once on each side, and at the horizon the pairs merge into a black band. Only a neighbour
  *further away* counts.
- **Do not widen terrain-against-terrain lines with distance.** The obvious reading of "the horizon
  is bolder" smears the far ranges solid: up there ridges are a pixel apart, so a 4 px line covers
  everything. What is bold in a panorama is the **sky silhouette**, so only that test uses the wide
  radius (`uHorizonBoost`); the ridge and crease lines keep one width everywhere.

Ridge/valley lines come from the two tangent directions away from a pixel: opposite on a flat
surface (`dot = -1`), folded together over a crest. Computed on eye positions, not on depth, so a
merely oblique slope — which is most of a panorama — does not read as a fold.

## Layers above the effect

`Layer::setPostProcessed(false)` holds a layer back from the stylized pass. It is drawn after the
effect, into the same depth buffer, so annotations and sky-anchored objects
([13-celestial.md](13-celestial.md)) keep their own appearance while still going behind ridges.
Such a layer takes no part in the terrain prelude (depth-write assignment, cover, draping) — it is
an overlay, not a layer that paints the ground.

## What it costs (measured)

Crosscall HLTE556N (Adreno 610), Grenoble panorama z13.2 tilt 25, 8 pan swipes, `-PprofileRender`:

| Config | CPU frame | GPU total | notes |
|---|---|---|---|
| ordinary map | 38.7 ms | 29.8 ms | the CPU number is mostly the swap wait (`sky` 24.7) |
| ordinary map + relief effect | 24.0 ms | 32.9 ms | the effect is **~3 ms of GPU**; the CPU drop is the swap wait moving |
| peak-finder mode (no tile layers) | 19.3 ms | 13.0 ms | `prelude` 9.5 ms — the depth texture pass |
| peak-finder, `meshResolution 32` | 12.7 ms | 10.1 ms | `prelude` 3.3 ms |

The effect's terrain depth texture is drawn at the terrain's **own** mesh resolution (see above:
a coarser depth mesh draws its own triangulation as fold lines), from CPU meshes, on **every frame
the camera or the elevation changes** — a still camera reuses the last texture — so it scales with
`TerrainOptions.MeshResolution`, and that is the knob to trade line quality for frames.

## Known limits

- The depth texture is half resolution by default, so lines are quantised at 2 px and slopes show
  occasional dotted artefacts. `TerrainOptions::setPostProcessDownscale(1)` gives a full-resolution
  pass, at a larger depth pass cost (not measured).
- The effect resolves once per frame over the whole screen; layer-level effects do not exist.
- Verified on the emulator (Grenoble panorama, z13.2 tilt 25). Line quality on a device at high DPI
  has not been measured.
