---
title: Custom layers
description: How an app-supplied draw callback runs inside the frame - the overlay slot, GL state and threads, context loss - and what is still unverified.
sidebar_position: 20
---

# Custom layers: an app drawing its own GL

Scope: `mm_layer_create_custom` (issue #147) from the C struct to the frame. The app-facing page is
[Extensions](../../guides/extensions.mdx#a-layer-you-draw-yourself); the registration is in
[the facade](../api-facade.md#a-layer-the-caller-draws).

## Pieces

| | |
|---|---|
| `api/CustomLayerHost` | the caller's vtable and its lifetime; no renderer, so `tests/api/CustomLayerTest.cpp` covers it |
| `api/CustomLayer` | the `Layer` that builds `mm_render_params` from the `ViewState` and calls the host |
| `api/CustomLayerApi.cpp` | the two C functions |
| `Layer::onDrawOverlay` | the one hook added to `Layer` |

## The overlay slot

`MapRenderer::drawLayers` calls `onDrawOverlay` on every layer after the billboards, so the layer
paints over everything, with the depth test off. A layer opted out of post-processing
(`setPostProcessed(false)`) goes through `drawOverlayLayers` instead and is called after that pass's
billboards, onto the effect's secondary colour attachment. Without an effect the two are the same.

There is no slot after the post-process resolve. The screen framebuffer's depth holds nothing the
frame drew there (the scene depth lives in the offscreen FBO), so only a depth-free overlay would fit,
and the opt-out already gives that without being stylised.

The params carry `framebuffer` read from `GL_FRAMEBUFFER_BINDING` at the call: it is the offscreen
FBO under an effect, so a plugin binding 0 draws to nowhere.

## The scene slot

`MM_CUSTOM_LAYER_SCENE` draws from `onDrawFrame3D`, at the layer's place in the stack, with the depth
test on (`LEQUAL`, restored afterwards). It is the one slot that has to follow the
[depth model](05-depth-model.md): content carries `(proxy − layer) × (2⁻¹⁹·w + depth_shift)`, and a
plugin drawing at raw depth z-fights the terrain or punches through it.

- `MapRenderer::drawLayers` hands each non-tile layer that asks (`Layer::getDepthOrdinalCount`) an
  ordinal **above every tile layer's**, in stack order, inside the shared-ground block where the tile
  layers get theirs. The tile layers' own ordinals and span are untouched, so a map without a scene
  layer renders as before. A scene layer under a tile layer in the stack therefore still wins
  coplanar contests against it; against real geometry the depth test decides.
- The plugin gets `depth_layer_offset = −ordinal` (the proxy is 0 for a live layer) and `depth_shift`,
  the same 0.02 the tiles use (`TileRenderer::TERRAIN_TANGRAM_DEPTH_SHIFT`, overridable by
  `debug.massif.depthshift`). `MM_CUSTOM_LAYER_GLSL_DEPTH` is `applyDepthBias`'s layer term and
  nothing else: the other terms are per-tile constants a plugin has no use for.
- Each ordinal spends 0.02 of the 0.2 total that was measured as the largest with no see-through
  (9 ordinals). A scene layer is one more step, so several of them eat the margin; they are cheap
  to count, expensive to leak through a ridge.
- With no terrain ground this frame both are 0, and the plugin's draw is plain depth-tested.

## Picking

`hit_test` and `on_click` are on the vtable and not on the event bus: a custom layer is native code
that already holds a callback table, and a `custom.clicked` event would need a payload class in a
new `.i`, a bridge, and regenerated wrappers for every binding. Revisit if an RN or NativeScript
host wants it.

`CustomLayer::calculateRayIntersectedElements` calls `hit_test` with the click ray (world units,
camera origin) and pushes a `RayIntersectedElement` at `origin + t·dir`, so the normal sort by
distance across layers applies and the nearest hit among all layers is clicked first. `processClick`
hands the feature to `on_click` and returns its answer, so a non-zero return stops the click
reaching the layers below and the map listener. It runs on the thread that handles input, **not** the
GL thread, concurrently with `render`: the header says the callbacks must be thread-safe. A layer
without `hit_test` is never asked, and without `on_click` a hit still lets the click through.

## Matrices

`mvp` is `ViewState::getModelviewProjectionMat()` (double). `mvp_local` is
`getRTEModelviewProjectionMat()`: the same with the camera subtracted, in float, and `origin` is the
camera. World units are the renderer's internal ones, not metres, and on the globe a position is 3D.

## Context loss

`GLResourceManager` is created per GL context. The layer holds a `GLResource` made from the current
manager and compares managers at each call, because `MapRenderer::onSurfaceDestroyed` may never run
on Android:

- a **different manager** than last frame is a lost context: `on_context_lost` runs before the next
  `render`, and the stale resource is dropped without its `destroy`, since its objects are gone;
- **removing the layer** drops the resource while the context lives, and the manager runs its
  `destroy` on the GL thread: that is `on_removed`, where the plugin deletes its GL objects.

## The GL state guard

A plugin that leaks state corrupts the layer after it, which reads as an SDK bug. In a build without
`NDEBUG`, `CustomLayer` snapshots program, VAO, array and element buffers, active texture and its 2D
binding, framebuffer, blend and its func, depth test/mask/func, cull face and mode, scissor and
viewport right before `render`, compares after, and logs one `Log::Errorf` naming what changed,
until the leak changes or stops. It reports; it does not repair. Release builds skip it, so the
cost is a debug-only ~25 `glGet*` calls per layer per frame. `GLStateSnapshot::describeChanges` has
no GL and is host-tested; the capture is not.

## Not verified

Built, syntax-checked and host-tested for the vtable only. Not run on a device: a real click through `TouchHandler`, the guard's capture, the scene slot's depth (z-fight, ridges, tilt 60), the slot under a
post-process effect, the GL-state expectations at that slot, a context loss, and removing a layer.
