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

Built, syntax-checked and host-tested for the vtable only. Not run on a device: the guard's capture, the slot under a
post-process effect, the GL-state expectations at that slot, a context loss, and removing a layer.
