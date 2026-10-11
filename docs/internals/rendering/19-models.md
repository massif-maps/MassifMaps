---
title: 3D models
description: How a model rule becomes instanced draws - the GLB loader, the per-point instances, the model program, terrain, shadows, the spin redraw - and what is still unverified.
sidebar_position: 19
---

# 3D models: GLB instances from a tile layer

Scope: the `model` symbolizer (issue #131) from file to pixel. What a style writes is on the
feature page, [3D models](../../features/3d-models.md).

## Pipeline

| Stage | Where | What happens |
| --- | --- | --- |
| load | `vt/GLBParser.cpp`, `VTModelLoader` | GLB → `vt::Model`: triangles of the default scene, node transforms flattened, glTF Y-up turned Z-up (`x, -z, y`), primitives merged per material, linear colours turned into display values. Cached per file in `vt::ModelManager`, failures too. |
| decode | `mapnikvt/ModelSymbolizer.cpp` | one instance per point; properties reading `[mapnik::seed]` (`random()` without a seed) are evaluated per point with the seed set to the point's world position quantised to 2^-26 of the world |
| build | `TileLayerBuilder::createModelProcessor` | instances grouped by `ModelStyle` (model, opacity, shadows, spin) into `TileModelInstances` on the `TileLayer`; points outside the half-open tile are dropped, the neighbour places them; mapbox's density grid (`model_bucket.ts` `lookupDim`) keeps one instance per cell across all the layer's model rules |
| draw | `GLTileRenderer::renderTileModels` | one `glDrawElementsInstanced` per mesh, inside the 3D pass of `renderGeometry3D` |
| shadow | `renderShadowCasters` | the same vertex shader with `shadowCasterFsh`, walked by `forEachVisibleRenderLayer` like the extrusions |

## The instance

32 bytes, divisor 1: vertex-frame `x, y` (tile units, y up) and the instance index; heading,
uniform scale, z offset (metres) and spin phase; tint RGB + tint amount as RGBA8. The model vertex
is 36 bytes: position and normal (floats, metres), uv, RGBA8 colour.

`modelVsh` spins the vertex about `uModelSpin.xyz` (Rodrigues), turns it by the heading, scales it,
then adds it to the anchor **draped in the vertex shader** with the shared `applyTerrain()`, scaled
from metres by `uHeightScale` (the tile-centre latitude, as `DefaultVertexTransformer::calculateHeight`).
mapbox bakes the DEM height of each instance on the CPU and re-bakes on an exaggeration change;
draping on the GPU follows exaggeration and DEM arrival for free, which is why we diverge.

An overzoomed source tile is drawn once per target tile. The anchor is mapped into the target tile
with `uTileMatrix`; an instance outside it collapses to a point outside the clip volume, so each
instance is drawn exactly once without a per-fragment discard.

## Lighting, shadow, fog

`applyLighting3D` is the extrusions' (`TileRenderer::LIGHTING_SHADER_3D`) with `wallT = sideVertex =
1`, so a model and the wall beside it agree under the same sun. `u_emissive` is forced to 0 (lit by
the scene, mapbox's `model-emissive-strength` default). Receiving uses the extrusion receiver flags
(`SHADOW_SINGLE_TAP`, `SHADOW_RECEIVER_3D`, normal offset). Back faces flip their normal in the
fragment stage and culling is off for the draw: generated foliage is single quads.

## Spin and redraw

`_modelTime` advances in `startFrame(dt)`. A draw of a spinning style sets `_modelsAnimating`, which
the next `startFrame` returns as a refresh — the map keeps drawing only while a spinning model was
drawn in the last frame. `setModelAnimationsEnabled(false)` stops the clock and the refresh.

## Picking and highlight

`findGeometryIntersections` adds `findTileModelIntersections` to its 3D pass: per instance whose
anchor the target tile owns (the same test as `modelVsh`), the anchor is lifted onto the terrain
with the labels' elevation provider and the click ray is taken into model space by
`intersectModelInstance` (`TileModels.h`, host-tested). The slab test is written out there:
`cglib::intersect_bbox` lets a ray parallel to a slab through from outside it, which is every
straight-down click on a flat map. A spinning style is tested against `modelPickBounds`, the cube
around everything its spin sweeps.

The hit reports the tile-local id the decoder re-reads the feature by. The highlight is keyed the
other way, by the SOURCE feature id and point index the app got from the click, which is why
`TileModelInstances` keeps both; `renderTileModels` turns it into the instance index the vertex
shader compares (`uModelHighlight`), so a highlight is a uniform, not a buffer rewrite.

## Caches

`_compiledModelMap` (per `Model`) and `_compiledModelInstancesMap` (per `TileModelInstances`, with one
VAO per program and mesh — see `CompiledGeometry` for why programs never share a VAO) are keyed by
`weak_ptr` and swept in `endFrame` with the other compiled resources. Textures go through
`buildCompiledBitmap`.

## Not done, not verified

- Globe mode skips models (`renderTileModels` returns on a spherical transformer).
- No transparency sort; `BLEND` materials blend in draw order.
- The density grid keys mapbox's table by the DRAWN tile's zoom, as if the source were their z16
  model tiles: the builder does not know the source tile's zoom. On a z14 source that keeps the
  z15 grid at z15-16 where mapbox would use 256 and none.
- A highlight scans the instance list of every drawn tile for its feature id, linearly.
- Verified: host tests (loader, `random()`, instance building, the pick math), NDK syntax gate,
  `glslangValidator` on every model program variant. **Not yet verified on a device**: the render
  itself, shadow casting, picking through the click path, the spin redraw cost.
