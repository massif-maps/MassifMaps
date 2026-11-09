---
title: Render scale
description: "Draw the map to a surface smaller than the view and let the system scale it up - the same ground, zoom and label sizes for fewer pixels - on its own in 3D."
sidebar_position: 1.6
---

# Render scale

A 3D frame on a slow GPU costs mostly the pixels it fills. The render scale draws the map to a
surface that is a fraction of the view's pixels, and the system scales it up to the view: fewer
pixels to shade, a slightly softer picture.

The scale **does not change what the map shows**. The DPI is lowered by the same factor, so the view
keeps its ground, zoom, tile choice, label and icon sizes, and a touch lands where the finger is.

| Option | Default | Used while |
|---|---|---|
| `renderScale` | 1 | the map is flat (2D) |
| `renderScale3D` | 0.75 | the map is in 3D |
| `appliedRenderScale` | read only | the surface's size over the view's right now |

```java
MassifMap map = MassifMap.attach(mapView);
map.set("renderScale3D", 0.6);   // softer, faster in 3D
map.set("renderScale3D", 1);     // 3D at native resolution
map.set("renderScale", 0.8);     // lower the flat map too
```

A value outside (0, 1] means 1, and none is taken below 0.25. The options are live: no recreation, the
surface is resized on the next frame.

On a Crosscall (Adreno 610), panning a tilted terrain map with hillshade, contours and drape at 0.75 took the GPU
from 36 to 26 ms a frame; the frame rate rose from 17 to 19.4 fps, and from 26.4 to 29.6 fps once the CPU floor was lower. 0.6 is no faster in that scene, and softer. Numbers and method in
[the performance log](../internals/performance-log.md#render-scale-with-the-dpi-lowered-too-2026-11-09).

## When the map is in 3D

3D is **terrain that is drawing and not flattened** (see [2D, 3D and everything between](terrain-2d-3d-states.md)),
or a **tilted view**: tilt below 85 degrees, where 90 looks straight down. Three things keep the surface
from flickering between the two sizes:

- once in 3D the map leaves it only above 88 degrees of tilt (a gap of three degrees);
- nothing switches while a finger is on the map: the gesture's touches were mapped with the old scale;
- a switch is not followed by another within a second.

A switch resizes the surface, which costs a frame, and changes the DPI, so the drape is re-baked at the
new size. Set both scales equal to never switch.

## What it speaks in

The API keeps speaking in **view pixels**: `screenToMap`, `mapToScreen`, `moveToFitBounds`, touch and wheel
input. The SDK converts to the surface's. Two things still arrive in surface pixels: the screen position in a
vector element drag and the screen positions an app-registered touch listener sees.

## Platforms

| Platform | Status |
|---|---|
| Android `MapView` | supported |
| Android `TextureMapView` | not done: its surface is the view's size |
| iOS | not done, see [the internals](../internals/rendering/21-render-scale.md#ios) |
| web, desktop | not done |
