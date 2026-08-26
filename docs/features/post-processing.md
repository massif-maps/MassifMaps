---
title: Post-processing Effects
sidebar_position: 9
---

# Post-processing Effects

Run a full-screen fragment shader over the rendered frame — with access to the **terrain depth** —
and keep chosen layers above it. This is what a peak-finder / relief look is made of.

:::info Fork feature
Added in PR [#56](https://github.com/massif-maps/MassifMaps/pull/56).
`MapRenderer.setPostProcessEffect()` + `PostProcessEffect`. The SDK ships **no** effect of its own:
the shader is a string the app supplies. Technical notes:
[`docs/internals/rendering/14-post-process.md`](/docs/internals/rendering/post-process).
:::

<figure class="docs-figure">

![Ink outlines over a shaded relief surface](/img/features/peakfinder.jpg)

<figcaption>The demo's relief look: the terrain drawn as a shaded surface (<code>TerrainOptions.setSurfaceShaderSource</code>) with an ink-outline post-process effect on top, no tile layers. Camera z13.2, tilt 25.</figcaption>

</figure>

## Attaching an effect

```kotlin
import com.massifmaps.renderers.PostProcessEffect

val effect = PostProcessEffect("relief-outline", myFragmentShaderSource).apply {
    isTerrainDepthRequired = true          // render the terrain depth texture for the shader
    setFloatParameter("uLineWidth", 1.6f)
    setFloatParameter("uHorizonBoost", 4f)
    setColorParameter("uInkColor", Color(0xFF202020.toInt()))
}
mapView.mapRenderer.postProcessEffect = effect
```

:::note Surface API
`PostProcessEffect` has no spec type and is attached to the `MapRenderer` rather than to a
registry object, so this stays object-API for now — see
[value types](/docs/api/reference/types#postprocesseffect) for what is readable through the
table. What every layer *does* expose is `postProcessed`, so keeping a layer out of the effect is
one path:

```java
map.layer("labels").set("postProcessed", false);
```
:::

With an effect attached, the sky, background and all layers render into an offscreen colour texture
with a real depth buffer; the effect then draws one full-screen quad. Nothing of this path runs when
no effect is set.

## What the shader gets

GLSL ES 1.00. The renderer sets each uniform only if the shader declares it:

| Uniform | Meaning |
|---|---|
| `sampler2D uColorTex` | the rendered frame, premultiplied alpha |
| `sampler2D uTerrainDepthTex` | packed terrain depth — only with `TerrainDepthRequired` |
| `vec2 uInvScreenSize` | `1/width, 1/height`; screen uv is `gl_FragCoord.xy * uInvScreenSize` |
| `float uNear`, `uFar` | frustum distances |
| `vec2 uProjInvScale` | `tan(fovy/2)·aspect, tan(fovy/2)` |
| `float uTime` | seconds since the effect was attached |
| named parameters | every `setFloatParameter` / `setColorParameter`, by name |

The depth texture is `RGB` = 24-bit linear eye depth relative to the far plane, `A` = terrain
coverage (0 = sky). Eye position of a pixel:

```glsl
float depth = dot(texture2D(uTerrainDepthTex, uv).rgb, vec3(1.0, 1.0/255.0, 1.0/65025.0));
vec3  eyePos = vec3((uv * 2.0 - 1.0) * uProjInvScale, -1.0) * depth * uFar;
```

Three rules that any effect drawing lines from that depth will otherwise re-derive the hard way:

- **Sample at least one depth texel apart** — the depth texture is rendered at **half resolution**,
  so the floor is `2 * uInvScreenSize`. A smaller step lands all four neighbours on the same texel,
  the tangents come out zero and `normalize(vec3(0))` paints the near field flat grey. Pass that
  floor in as a float parameter if the shader needs it as one.
- **A silhouette belongs to the nearer side.** Testing `abs(neighbour - depth)` draws every ridge
  twice and merges the pairs into a black band at the horizon. Only a neighbour *further away*
  counts.
- **Do not widen terrain-against-terrain lines with distance** — far ridges are a pixel apart, so a
  wide line smears the range solid. Only the sky silhouette takes the wide radius.

## Layers above the effect

```kotlin
annotationLayer.setPostProcessed(false)
```

A layer held back from the stylized pass is drawn **after** the effect, into the same depth buffer —
so annotations and [sky-anchored objects](/docs/features/celestial-objects) keep their own appearance
and still go behind ridges. Such a layer takes no part in the terrain prelude (depth-write
assignment, cover, draping): it is an overlay, not a layer that paints the ground.

## The terrain surface underneath

The relief look is two halves. The effect draws the lines; the shaded paper-white surface comes from
the terrain itself:

```kotlin
terrainOptions.surfaceShaderSource = myTerrainSurfaceShader
```

That shader is what paints the ground where no tile layer does — pair it with `--es map false`-style
configuration (no base map, no hillshade) for a pure panorama.

## Cost (measured, emulator)

Crosscall HLTE556N (Adreno 610), Grenoble panorama z13.2 tilt 25, 8 pan swipes:

| Config | CPU frame | GPU total |
|---|---|---|
| ordinary map | 38.7 ms | 29.8 ms |
| ordinary map + relief effect | 24.0 ms | 32.9 ms |
| peak-finder mode (no tile layers) | 19.3 ms | 13.0 ms |
| peak-finder, `MeshResolution 32` | 12.7 ms | 10.1 ms |

The effect itself is ~3 ms of GPU. The rest is the terrain depth texture, drawn every frame at the
terrain's **own** mesh resolution (a coarser depth mesh draws its own triangulation as fold lines),
so `TerrainOptions.MeshResolution` is the knob that trades line quality for frames.

Known limits: the depth texture is half resolution, so lines quantise at 2 px; the effect resolves
once per frame over the whole screen — there are no layer-level effects.
