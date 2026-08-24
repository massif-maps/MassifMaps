---
title: Sky, Sun & Shadows
sidebar_position: 7
---

# Sky, Sun and Shadows

One directional light and one shader sky, shared by the ground, the terrain, the buildings and the
fog — so the map, the horizon and the haze all agree on where the sun is.


<figure class="docs-figure">

![3D terrain lit by a low sun, under the shader sky](/img/features/sun-lighting.jpg)

<figcaption>Terrain lit by a sun 10° above the horizon (azimuth 100°), under the built-in sky gradient. Captured from the <code>scripts/android-dev</code> demo.</figcaption>

</figure>

## The sun

```kotlin
import com.massifmaps.components.LightOptions

val light = LightOptions().apply {
    sunAzimuth = 315f          // degrees clockwise from north; default 315 (NW)
    sunAltitude = 45f          // degrees above the horizon; default 45
    sunIntensity = 1.0f
    ambientIntensity = 0.35f
    ambientColor = Color(0xFFB8C8E0.toInt())   // cool sky tint in the shadows; default white
    isTerrainLightingEnabled = true   // shade the terrain surface with N·L
}
mapView.options.lightOptions = light
```

Instead of setting the angles, ask for a real sun position — NOAA's low-accuracy solar formula,
good to about 0.1°:

```kotlin
light.setSunPositionFromTime(2026, 8, 14, 7, 30, 45.188, 5.719)   // UTC + lat/lon
```

| Property | Default | Notes |
|---|---|---|
| `SunAzimuth` | `315` | Degrees clockwise from north. The classic cartographic light is NW. |
| `SunAltitude` | `45` | Degrees above the horizon, clamped `-90..90`. |
| `SunColor` / `SunIntensity` | white / `1.0` | Direct light. |
| `AmbientIntensity` | `0.35` | Light in the shadow, and the brightness floor everywhere. |
| `AmbientColor` | white | Tint of that shadow light. A cool blue is what makes dusk read as sky-lit rather than just darker. Applies to the terrain surface and to 3D buildings alike. |
| `TerrainLightingEnabled` | `false` | Shade the terrain surface from its geometric normal. |
| `ShadowStrength` | `0.0` | `0` = no shadows. |
| `ShadowMapSize` / `ShadowCascades` | `1024` / `3` | Cascaded shadow map, up to 4 cascades. |
| `ShadowBias` / `ShadowSoftness` / `ShadowDistance` | `0.25` / `1.0` / `0` | `ShadowDistance` 0 = derived from the view. |
| `ShadowCasterMargin` | `3` | Ring of off-screen tiles that may still cast into the view. |

:::caution Terrain cast shadows are wired but off
The cascaded shadow map, the caster pass and the light boxes all exist, but casting onto the shared
ground is currently disabled in `MapRenderer::applyTerrainShadows` — with the pass on, the map reads
as shadow acne instead of shadows. `ShadowStrength` therefore affects 3D objects (buildings), not the
terrain surface. Sun *lighting* of the terrain (`TerrainLightingEnabled`) is unaffected.
:::

Two details worth knowing about the shadow model: the shadow pass floors the sun altitude at 15°
(a 9° sun throws a 4.4 km shadow off a 700 m hill and the cascade ladder goes useless), while N·L
lighting keeps the true sun; and the shadow map is snapped and cached, so a still camera re-renders
nothing.

## The sky

`SkyOptions` draws a single full-screen pass before everything else — one quad whatever the camera
does.

```kotlin
import com.massifmaps.components.SkyOptions
import com.massifmaps.graphics.Color

val sky = SkyOptions().apply {
    isEnabled = true
    skyColor = Color(0xFF3A74C4.toInt())      // zenith
    horizonColor = Color(0xFFABCEEC.toInt())
    horizonBlend = 12f                        // degrees of gradient around the horizon
    isSunDiscEnabled = true
}
mapView.options.skyOptions = sky
```

| Property | Default | Notes |
|---|---|---|
| `Enabled` | `true` | Off → the legacy sky bitmap band (`Options.setSkyColor`) is drawn instead. |
| `SkyColor` / `HorizonColor` | blue / pale blue | Zenith and horizon of the built-in gradient. |
| `GroundColor` | horizon color | Only shows in the wedge between the drawn map and the mathematical horizon. Transparent leaves the clear color. |
| `HorizonBlend` | `12` | Degrees of blend between horizon and sky. |
| `SunDiscEnabled` | `true` | Draw the sun disc and its glow. |
| `ShaderSource` | — | Replace the whole appearance (below). |

### A custom sky shader

`setShaderSource` takes GLSL ES 1.00 that defines one function:

```glsl
vec4 skyColor(vec3 rayDir) {          // rayDir: world-space view ray, x east, y north, z up
    float t = clamp(rayDir.z, 0.0, 1.0);
    vec4 c = mix(u_horizonColor, u_skyColor, t);
    return mix(c, vec4(u_fogColor.rgb, 1.0), fogAmount(rayDir));
}
```

The wrapper already declares the uniforms and the `fogAmount(rayDir)` helper — **redeclaring any of
them is a compile error and the renderer silently falls back to the built-in sky**, which is the
usual reason a custom sky "does nothing".

| Uniform | Meaning |
|---|---|
| `vec3 u_sunDir` | unit vector towards the sun, world space |
| `vec4 u_sunColor`, `float u_sunIntensity` | from `LightOptions` |
| `vec4 u_skyColor`, `u_horizonColor`, `u_groundColor` | the configured colours |
| `float u_horizonBlend`, `u_sunDisc` | the configured blend / sun-disc switch |
| `vec4 u_fogColor`, `float u_fogBlend`, `u_fogHorizon` | the resolved fog, already lit by the sun |
| `vec4 u_fogHighColor`, `u_fogSpaceColor`, `float u_starIntensity` | the atmosphere colours and the star field |
| `float u_time`, `u_zoom`, `u_cameraHeight` | seconds since the view was created, fractional zoom, metres |
| `vec2 u_resolution` | viewport size in pixels |

## Fog (the atmosphere)

`FogOptions` is modelled on the Mapbox `fog` style property, so a value tuned for a Mapbox style
transfers directly. It is resolved once per frame from the options and the style's `Map` block,
**lit by the same sun** (dark at night, warm at a low sun), and handed to the tile content, the
background plane and the sky. That single resolution point is why the ground and the horizon match.

Fog does **not** need 3D terrain: it fogs a plain 2D map, and it fogs the background plane past the
loaded tiles, which is what fills the far distance.

```kotlin
val fog = FogOptions().apply {
    color = Color(0xFFDC9F9F.toInt())  // alpha = how opaque the fog gets; transparent = no fog
    rangeStart = 0.8f                  // multiples of the camera-to-focus distance
    rangeEnd = 8.0f                    // where it reaches full strength
    highColor = Color(0xFF245BDE.toInt())
    spaceColor = Color(0xFF000000.toInt())
    horizonBlend = 0.5f
    starIntensity = 0.15f
}
mapView.options.fogOptions = fog
terrain.viewDistanceFactor = 1.0f      // where the ground ends — pair it with fog
```

| Property | Default | Mapbox | Notes |
|---|---|---|---|
| `Enabled` | `true` | — | The switch. Off keeps every value configured, so a UI toggle never has to drive a colour or a range through zero. |
| `Color` | transparent | `color` | Alpha = strength at full distance. Transparent = no fog. |
| `RangeStart` / `RangeEnd` | `0.8` / `8` | `range` | **Multiples of the camera-to-focus distance**, not metres. That distance is a function of the zoom alone, so one setting holds at every zoom. |
| `HighColor` | transparent | `high-color` | The lit upper atmosphere. Transparent leaves the `SkyOptions` gradient alone. |
| `SpaceColor` | transparent | `space-color` | The zenith, beyond the atmosphere. |
| `HorizonBlend` | `0.133` | `horizon-blend` | Fraction of a quarter turn the haze fades out over. |
| `HorizonAngle` | `-1` | — | Degrees the haze is still at full strength at. `-1` derives it from the terrain in view. |
| `StarIntensity` | `0` | `star-intensity` | Drawn by the built-in sky shader only. |
| `ShaderSource` | — | — | Replace the blend (below). |

A style sets the same from its `Map` block — `fog-enabled`, `fog-color`, `fog-range-start`,
`fog-range-end`, `fog-high-color`, `fog-space-color`, `fog-horizon-blend`, `fog-star-intensity` —
and the style wins where it has an opinion. Only the style can make any of them zoom-dependent:

```css
Map {
  fog-color: #dc9f9f;
  fog-range-start: 0.8;
  fog-range-end: linear([view::zoom], (11, 8), (15, 4));
  fog-high-color: #245bde;
  fog-space-color: #000000;
  fog-horizon-blend: 0.5;
  fog-star-intensity: 0.15;
}
```

Every `Map` block setting on this page survives compilation to Mapnik XML: `css2xml` writes them
as attributes of the `<Map>` element under the same names, and only the ones the style actually set
— an absent attribute is what tells the SDK the application's own `LightOptions` / `FogOptions`
value stands.

`ViewDistanceFactor` ends the ground (tangram's rule: `2 × camera height / cos(pitch + fovy/2)`,
capped at 127 tile widths; `1` = their rule verbatim). Without fog it ends on a hard edge.

### A custom fog shader

`FogOptions.setShaderSource` takes GLSL ES 1.00 defining one function, and it is compiled into
**every** pass that fogs — the tile content, the background plane and the sky — so one function
covers the whole frame in 2D and in 3D:

```glsl
vec4 applyFog(vec4 color, float amount, float dist) {
    // color is PREMULTIPLIED; amount is the built-in factor for this fragment, already scaled by
    // uFogColor.a; dist is in the same units as the range.
    vec3 tint = mix(uFogColor.rgb, uFogHighColor.rgb, clamp(dist / uFogParams.w, 0.0, 1.0));
    return vec4(mix(color.rgb, tint * color.a, amount), color.a);
}
```

| Uniform | Meaning |
|---|---|
| `vec4 uFogColor` | the resolved fog colour, already lit by the sun |
| `vec4 uFogHighColor`, `uFogSpaceColor` | the atmosphere colours |
| `vec4 uFogParams` | range start, `1 / (end - start)`, internal → range units, range end |

The sky calls it too, with its angular haze as `amount` and the range end as `dist`. A source that
fails to compile falls back to the built-in blend and logs the error. Changing it rebuilds every
shader program, so set it once rather than per frame.

For an effect that needs the whole composited frame instead — including the layers drawn above the
ground — see [Post-processing](/docs/features/post-processing).

## See also

- [3D Terrain](/docs/features/3d-terrain) — the surface all of this lights.
- [Objects in the Sky](/docs/features/celestial-objects) — a sun disc, a moon, stars, an aircraft.
- [Post-processing Effects](/docs/features/post-processing) — full-screen shaders over the lit scene.
