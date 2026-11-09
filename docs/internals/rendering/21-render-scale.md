---
title: Render scale
description: How the surface is drawn smaller than the view - who decides the scale, how the DPI and the touches follow, when 3D asks for it - and what is not done.
sidebar_position: 21
---

# Render scale: fewer pixels, the same map

Scope: `Options.renderScale` / `renderScale3D` from the policy to the Android surface. The app-facing page is
[Render scale](../../features/render-scale.md). The frame is GPU-fill-bound at native resolution on a slow
device (the GL thread waits in `dequeueBuffer`), so the cheapest lever is fewer pixels.

## One rule: the surface shrinks, the DPI shrinks with it

Rendering to a smaller surface alone **shows less ground**: the tile size in pixels (`tileDrawSize * dpi / 160`)
and every dp size stay the same, so the same number of pixels covers fewer tiles. The DPI the renderers
see is therefore the device DPI times the surface's size over the view's:

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
| `BaseMapView::onDrawFrame` | native, GL thread | feeds it the tilt, terrain state and gesture; keeps `getWantedRenderScale()` |
| `MapView.onDrawFrame` | Java, GL thread | reads the wanted scale after each frame; when it differs from the last request, posts to the UI thread |
| `MapView.applyRenderScale` | Java, UI thread | `setFixedSize(w * s, h * s)`, or `setSizeFromLayout()` at 1; again from `onSizeChanged` |
| `MapView.onSurfaceChanged` | Java, GL thread | reports the surface's real size over the view's with `BaseMapView::onSurfaceChanged(w, h, scale)` |
| `Options::setAppliedRenderScale` | native | the value the renderers use; notifies `DPI` so the view state recomputes |

The scale the renderers use is **measured from the surface** (width over view width), never the requested
one, so the DPI changes in the same call as the size and a frame never has the new DPI on the old size.
A request the system rounds is applied as rounded.

3D is `terrainOptions->isActive() && !isFlattened()` or tilt under 85 degrees (see the feature page for
the gap and the hold). The switch is never made while a finger is down: touches in flight were mapped with
the old scale. While it is held for the one-second minimum the map keeps asking for frames, because nothing else wakes
a map at rest once the hold is over.

## View pixels in, surface pixels inside

`BaseMapView` converts at its edge by the applied scale: `onInputEvent` (the -1 "no coordinate" is kept),
`onWheelEvent`, `onWheelZoom`, `screenToMap`, `mapToScreen` (divides), `moveToFitBounds`' `ScreenBounds`,
`calculateOrbitPivot`, and `FocusPointOffset` in the projection. Everything inside, and so the platform
touch code, is unchanged. Checked on the emulator: a 300 px drag moves the marker 257 px at 1 and at 0.6.

Still in surface pixels: `VectorElementDragInfo` and `PopupClickInfo` screen positions, and what an
`OnTouchListener` registered on the touch handler receives.

## What the scale does not shrink

Sizes in raw **pixels** (a marker or line with `scaleWithDPI` off) are surface pixels, so they appear `1/scale`
larger on screen. Sizes in dp, and everything a vector style draws, keep their size. The glyph raster size the
vector decoder picks is set once from the device DPI, so labels are drawn from a raster made for native and
minified.

## Not done

- **iOS.** `MSFMapView` would set `contentScaleFactor = scale * nativeScale`, report
  `drawableWidth / (viewWidth * nativeScale)` as the scale in `onSurfaceChanged`, and read
  `getWantedRenderScale` after `onDrawFrame`. `BaseMapView` is ready for it; what is missing is the
  `BaseMapView.i` wrapper (`getWantedRenderScale`, the three-argument `onSurfaceChanged`) and a device to check
  a MetalANGLE layer resize on. No iOS build was run.
- **`TextureMapView`.** Its surface is the view's size; scaling it means `SurfaceTexture.setDefaultBufferSize`.
- Web, desktop.
- A switch re-bakes the drape (its resolution follows the DPI) and resizes the surface: a hitch the hold only spaces out.
