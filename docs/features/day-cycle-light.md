---
sidebar_position: 25
---

# Lighting a map by the hour

One palette, no night theme. The scene light is read off a curve, and every colour on the map — 2D
fills and lines, labels and their halos, the map background, 3D walls and roofs — is derived from
it. Move the sun and the whole map follows, with no re-decode: the tiles are untouched, so it is a
redraw.

This page is the two halves of that: what a **style** has to say for its colours to be lit at all,
and how an **app** replaces the curve that decides what the light is at a given hour.

## The short version

```java
map.light(Spec.of("light")
    .set("dayCycleLightsEnabled", true)   // the hour drives the light
    .set("sunOverridingStyle", true)      // the app's sun beats the style's
    .set("terrainLightingEnabled", true)  // needed for cast shadows - see below
    .set("shadowStrength", 0.35));

// An hour is a sun POSITION; the curve turns that into a light.
map.light().apply(Spec.object()
    .set("sunAzimuth", 90 + (hour - 6) * 15)
    .set("sunAltitude", 62 * Math.sin(Math.PI * (hour - 6) / 12)));
```

With `dayCycleLightsEnabled` off nothing is derived and the style's own values stand, which is what
every map did before this existed.

## Half one: the style says how much light each colour takes

MapBox's model, and the SDK's: `*-emissive-strength` is how much of a colour is **emitted** rather
than lit.

```
shown = authored × (emissive + (1 − emissive) × radiance)
```

`radiance` is what the scene light does to a flat, upward-facing surface — a per-channel value, so a
light's colour moves the hue and not only the brightness. At emissive **1** a colour is drawn as
authored under any light; at **0** the scene owns it entirely.

| Property | Default | Applies to |
|---|---|---|
| `polygon-emissive-strength`, `line-emissive-strength` | 1 | fills and lines (and their `-pattern-` forms) |
| `text-emissive-strength`, `shield-…`, `marker-…` | 1 | the label's ink |
| `text-halo-emissive-strength`, `shield-…`, `marker-…` | the label's own | the halo alone |
| `building-emissive-strength` (Map) | 0 | 3D extrusions |
| `background-emissive-strength` (Map) | 1 | the map background |

Every default is **1** except the buildings, so **a style that says nothing renders exactly as it
did before any of this existed**. A style that wants to be lit has to ask:

```css
Map { background-color: #f4f1ec; background-emissive-strength: 0; }
#water   { polygon-fill: #8fb8d8; polygon-emissive-strength: 0; }
#road    { line-color: #ffffff;   line-emissive-strength: 0.25; }
/* Bright ink, dark outline: the name stays readable while the map goes down around it. */
#poi     { text-fill: #ffffff; text-emissive-strength: 0.8; text-halo-emissive-strength: 0; }
```

:::tip Why a road usually wants 0.15–0.3 rather than 0
MapBox defaults geometry to 0, and Standard then states a higher value on nearly everything it
wants to hold up at night. A style with **no** emissive anywhere takes the 0 everywhere and
collapses to a few per cent of its colour the moment the sun is down — far darker than Standard at
the same hour. That is not the SDK being wrong; it is a style with no light model being lit as if
it had one. The converter's `--geometry-emissive` puts a floor under it.
:::

### `view::brightness`, for a colour that should change rather than dim

The grade only ever **darkens** — `radiance` is at most 1. MapBox Standard's labels do not get
darker at night, they get *lighter*: it ramps `text-color` over `["measure-light", "brightness"]`.

The SDK exposes the same scalar as a view variable, so a style can do it too:

```css
#poi_label {
  text-fill: linear([view::brightness], (0.05, #ffffff), (0.4, #333333));
  text-halo-emissive-strength: 0;
}
```

`view::brightness` is MapBox's own `calculateLightsBrightness` and lands on their numbers: day
0.478, dawn 0.396, dusk 0.027. It is resolved per frame, so a ramp over it follows the hour with no
re-decode.

## Half two: the app replaces the curve

`LightOptions.dayCycleLightStops` is a list of `LightStop`s — a light anchored on a sun height. The
SDK holds below the first and above the last, and smoothsteps between two in linear colour space.

Through the facade it is one JSON property. Each stop is an **object**, because five fields of
three different kinds have no order anybody would guess:

```json
[
  { "sunAltitude": -9, "ambientColor": "#001438", "ambientIntensity": 0.5,
    "sunColor": "#3f4455", "sunIntensity": 0.5 },
  { "sunAltitude":  3, "ambientColor": "#363e5e", "ambientIntensity": 0.8,
    "sunColor": "#fec286", "sunIntensity": 0.2 },
  { "sunAltitude": 12, "ambientColor": "#363e5e", "ambientIntensity": 0.8,
    "sunColor": "#fec286", "sunIntensity": 0.2 },
  { "sunAltitude": 38, "ambientColor": "#ffffff", "ambientIntensity": 0.8,
    "sunColor": "#ffffff", "sunIntensity": 0.2 }
]
```

That **is** the built-in curve — MapBox Standard's four light setups at the sun heights it states
them for. Passing an empty list selects exactly it.

- `sunAltitude` is required; a stop with no height has no place on a curve and is refused.
- Colours read `#rgb`, `#rrggbb`, `#aarrggbb`, or the plain ARGB number every other colour property
  carries. They are written back as `#aarrggbb`.
- The doubled twilight stop is deliberate: it holds the light flat from 3° to 12°, so the sun passes
  **through** dusk instead of crossing it.
- `dayCycleRisingLightStops` gives a rising sun its own curve, which is how dawn differs from dusk
  at the same height. Left empty, the one curve is used all day.

Set it and everything downstream follows — the 2D grade, the sun and ambient the buildings and the
terrain are lit with, and `view::brightness`. There is no second theme to keep in step.

## Cast shadows need a terrain

`shadowStrength` alone is not enough. Cast shadows are drawn from the **drape pass** and land on the
**terrain surface**: with no terrain there is no surface to receive them, and nothing casts at all
however high the strength goes. The renderer says so if you read its log:

```
MapRenderer: shadows off    (strength 0.35, terrain lighting 1, cover tiles 0)
MapRenderer: shadows ACTIVE (strength 0.35, terrain lighting 1, cover tiles 1)
```

A flat city still wants one — it is there for the light, not the relief:

```java
map.terrain(Spec.of("terrain").set("source", dem))
   .apply(Spec.object().set("exaggeration", 1).set("cameraClearance", 40));
```

`terrainLightingEnabled` must also be on, because the shadow multiply lives in the same block that
lights the ground. A style that says `colors-prelit` is **not** lit twice by this: that flag states
the 2D colours already carry the light, so turning terrain lighting on only lets the shadows land.

## Known gaps

- **The grade only darkens.** A colour that should get *lighter* at night needs a `view::brightness`
  ramp in the style, as above; a bare emissive cannot do it.
- **One curve drives everything.** There is no separate curve for buildings. What a style can vary
  independently is `building-ambient`, `building-light-intensity` and `building-emissive-strength`.
- **No building shadows without a terrain.** Only the terrain surface samples the shadow map.
- **A translucent extrusion has no path** — see
  [the rendering notes](../internals/rendering/08-lighting-sky-fog.md).
