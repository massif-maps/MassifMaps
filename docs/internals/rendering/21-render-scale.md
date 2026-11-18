---
title: Render scale
description: How the map is drawn smaller than the view into an offscreen buffer - who decides the scale, how the DPI and the touches follow, when 3D asks for it - and what is not done.
sidebar_position: 21
---

# Render scale: fewer pixels, the same map

Scope: `Options.renderScale` / `renderScale3D` from the policy to the offscreen scene buffer (Android only for now). The app-facing page is
[Render scale](../../features/render-scale.md). The frame is GPU-fill-bound at native resolution on a slow
device (the GL thread waits in `dequeueBuffer`), so the cheapest lever is fewer pixels.

## One rule: the buffer shrinks, the DPI shrinks with it

Rendering to a smaller buffer alone **shows less ground**: the tile size in pixels (`tileDrawSize * dpi / 160`)
and every dp size stay the same, so the same number of pixels covers fewer tiles. The DPI the renderers
see is therefore the device DPI times the buffer's size over the view's:

```
Options::getRenderDPI() = DPI * appliedRenderScale
```

`ViewState`, the tile LOD, `TileRenderer`'s drape resolution, the touch thresholds, the click tolerance and
the clustered layer read it. `getDPI()` stays what the app set. `ViewState::calculateZoom0Distance` is
`height / (tile * dpiScale)`, so height and DPI cancel: the same ground, the same zoom, and `unitToDPCoef`
(dp sizes in world units) does not move. Checked on the emulator: a marker, a line and the ground land on the
same pixels at 1, 0.6 and 1 again, and on a Crosscall at 1, 0.75 and 0.6 ([measurements](../performance-log.md#render-scale-with-the-dpi-lowered-too-2026-11-09)).

## Who decides, who applies

| Piece | Where | Job |
|---|---|---|
| `RenderScalePolicy` | `all/native/ui/RenderScalePolicy.h` | pure: 2D or 3D, the scale, whether a switch is held. Host-tested |
| `BaseMapView::onDrawFrame` | native, GL thread | feeds it the tilt, terrain state and gesture; on a change sets `Options::setAppliedRenderScale` and `MapRenderer::setRenderScale` (Android builds only) |
| `MapRenderer::applyRenderSize` | native, GL thread | the view state's screen size becomes surface size * scale; the screen and scene buffers are dropped |
| `MapRenderer::onDrawFrame` | native, GL thread | below 1, binds the scene buffer for the whole frame, then draws it to the surface with a linear-filtered quad |
| `Options::setAppliedRenderScale` | native | the value the renderers use; notifies `DPI` so the view state recomputes |

The platform surface is never resized: it keeps the view's size, and `onSurfaceChanged` always reports it.
A surface resize (`SurfaceView.setFixedSize`) was tried first and dropped: on most 2D/3D switches one frame showed
the map at 0.75 in a corner of the view, black elsewhere. It went away with `renderScale3D = 1`. The viewport matched the
buffer in the log, so the cause is on the system's side, not pinned down.

The scene buffer is RGBA with depth 24 and stencil 8, sized to the view state. The frame's own post-process and
drape passes nest inside it: they save and restore the bound framebuffer. The framebuffer bound when the frame
starts is kept and drawn to last, because it is not 0 on every platform. A screenshot capture reads the scene
buffer, so it has the buffer's size, not the view's.

3D is `terrainOptions->isActive() && !isFlattened()` or tilt under 85 degrees (see the feature page for
the gap and the hold). The switch is never made while a finger is down: touches in flight were mapped with
the old scale. While it is held for the one-second minimum the map keeps asking for frames, because nothing else wakes
a map at rest once the hold is over.

## View pixels in, buffer pixels inside

`BaseMapView` converts at its edge by the applied scale: `onInputEvent` (the -1 "no coordinate" is kept),
`onWheelEvent`, `onWheelZoom`, `screenToMap`, `mapToScreen` (divides), `moveToFitBounds`' `ScreenBounds`,
`calculateOrbitPivot`, and `FocusPointOffset` in the projection. Everything inside, and so the platform
touch code, is unchanged. Checked on the emulator: a 300 px drag moves the marker 257 px at 1 and at 0.6.

Still in buffer pixels: `VectorElementDragInfo` and `PopupClickInfo` screen positions, and what an
`OnTouchListener` registered on the touch handler receives.

## What the scale does not shrink

Sizes in raw **pixels** (a marker or line with `scaleWithDPI` off) are buffer pixels, so they appear `1/scale`
larger on screen. Sizes in dp, and everything a vector style draws, keep their size. The glyph raster size the
vector decoder picks is set once from the device DPI, so labels are drawn from a raster made for native and
minified.

## Not done

- **iOS, web, desktop.** The offscreen path is platform-neutral, but `BaseMapView` applies the policy only in
  Android builds (`__ANDROID__`): a device check per platform comes first, in particular that the framebuffer
  bound at frame start is the one to draw the scaled scene to.
- **`TextureMapView`** takes the same path as `MapView` and has not been run.
- A switch re-bakes the drape (its resolution follows the DPI) and reallocates the scene buffer: a hitch the hold only spaces out.
- One extra full-screen pass per frame below 1; not measured against the surface resize it replaces.
