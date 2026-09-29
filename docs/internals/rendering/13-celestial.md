---
title: Celestial objects
description: Sun, moon, stars or an aircraft placed in the sky, and the free-roam camera that looks up at them.
sidebar_position: 13
---

# Objects in the sky

Scope: `all/native/celestial/`, `all/native/layers/CelestialLayer.*`,
`all/native/renderers/CelestialRenderer.*`. The sky *itself* — gradient, sun disc, fog — is
[08-lighting-sky-fog.md](08-lighting-sky-fog.md); this page is about objects placed **in** it.

Everything the map draws is anchored to a position on the ground. A sun, a moon, a star, an
aircraft overhead are not: they belong to a direction, or to a point in the air. `CelestialLayer`
is the layer for those, and it is deliberately generic — **the SDK has no notion of a sun or a
moon**. The demo's `DemoCelestial` builds both out of two sprites and an arc, and the same API
carries a satellite track or a star catalogue with no SDK change.

## Anchors

An object is placed one of two ways (`CelestialObject`):

- **By direction** — azimuth and altitude in degrees, plus a distance. Distance **0 means
  infinitely far**: the object keeps its direction whatever the camera does, so it never
  parallaxes when the map pans. That is what the sun, the moon and the stars need.
  A finite distance gives real parallax, for something that is merely high up.
- **By geographic position + altitude** — for an object that belongs to a place on the map but is
  above it: an aircraft, a satellite pass.

Directions use the same frame as `LightOptions::getSunDirection` — x east, y north, z up, azimuth
clockwise from north — so an application can hand a computed sun direction straight over. The
renderer converts through `ProjectionSurface::calculateVector` at the focus point, so it is correct
on a sphere as well as on a plane.

## Drawing

`CelestialRenderer` draws with the layer, in layer order. **Add the layer first** and the map and
the terrain then draw over it, which is what a body in the sky wants: a ridge in front of the sun
hides it, for free, with no extra work.

- **Sprites** are camera-facing quads expanded from the view matrix's right/up, sized either by
  **angle** (a real body: the sun and the moon are both about 0.5°) or in **screen pixels** (an
  icon). Batched **per bitmap**, so any number of objects sharing one bitmap — or none — is a
  single draw call. With no bitmap the fragment shader draws a soft disc, which costs no texture
  at all and is enough for a sun, a moon or a star.
- **Arcs** are line strips. A circle about an axis covers the useful case exactly: the daily path
  of a distant body is the circle of constant declination about the rotation axis, so an
  application gives an axis and one angle rather than sampling positions through the day. An
  explicit direction list covers the rest, and `setSegments` reads that list as **disjoint pairs**
  instead of a path — a figure drawn between fixed directions (the demo's constellation lines) is
  then ONE object: one draw call, one clickable thing, one name.
- **Images** are a 16×16-cell mesh per object, each vertex `normalize(M·[u, v, 1])` where `M` maps
  the three anchors' bitmap coordinates to their directions (`CelestialImageGrid.h`, Stellarium's
  art transform). A plane seen from its centre bends straight bitmap lines into great circles, so a
  figure 60° across stays true. Drawn first, with the sprite program (`u_luminanceAlpha` switches
  brightness-as-opacity on); textures live in a `BitmapTextureCache`, unlike sprites, which upload
  per frame — a 512² artwork re-uploaded with mipmaps every frame is not free.
- **Depth**: depth-TESTED, never depth-WRITING. An infinitely distant object is parked just inside
  the far plane, so everything the map draws is nearer and covers it, and it never occludes
  anything itself.
- Objects are sorted **far to near** before drawing so overlapping discs blend in the expected
  order.
- **Over a stylized map**: `setPostProcessed(false)` on the layer draws it after a post-process
  effect resolves, so a daily path keeps its colour over a relief-styled ground and still goes
  behind the ridges (the depth buffer is the same one). See
  [14-post-process.md](14-post-process.md#layers-above-the-effect).

## Clicking, and the gap it exposed

Objects are hit-tested against the touch ray through the standard layer path
(`calculateRayIntersectedElements` / `processClick`), so they sort against every other layer's
content and a click on terrain in front of the sun does not report the sun. The test is **angular**
— how far the ray is from the direction the object sits in — which is the natural measure here; a
sprite drawn half a pixel wide would be unhittable otherwise, hence `CelestialSprite::ClickRadius`.
Arcs are hit the same way, against the nearest point of the curve (`CelestialArc::ClickRadius`,
0 = not clickable). Every arc sits at the same distance, so the reported hit is pushed out by the
angle it was missed by — otherwise two overlapping curves would be picked between by list order,
and a sprite drawn on a curve would lose to it.

This needed a fix in `TouchHandler::handleClick`, and it applies to any sky content, not just this
layer: a touch was **dropped entirely** unless its ray met the ground (`isValidScreenPosition`),
because the click path was built around a map position. A tap aimed at the sky has no map position
but still has a ray, so the layers are now asked with the ray alone in that case (`MapRenderer`
gained the matching overload). Only the map-position callback is skipped — there is nothing to
report.

## Seeing them: free roam

Sky content is normally off the top of the screen, because the map camera points at the ground.
`Options::FreeRoamMode` changes what the gestures do, and there are three:

| Mode | One finger | Two fingers | Camera model |
|------|-----------|-------------|--------------|
| `OFF` (default) | pans the map | pan / pinch / rotate | the map's: tilt and rotation orbit the focus |
| `LOOK` | looks around | pan / pinch / rotate | the map's, except that the heading turns about the camera |
| `FIRST_PERSON` | looks around, **the position never changes** | move: forward/back and strafe | the camera never orbits anything |

`FIRST_PERSON` is a mouse look, and it is a **camera model, not a gesture mapping**: `CameraTiltEvent`
and `CameraRotationEvent` both pivot about the camera whatever asks them to, so `setTilt` and
`setMapRotation` driven by a device's orientation sensor behave exactly like the drag. That is the
point — the same code path serves the finger and the phone being turned.

What each piece is:

- **Turning** pivots about the camera. A map rotation rotates camera *and* focus about the focus,
  which swings the camera around a circle of the focus distance and, at a low tilt, straight
  through the terrain. `CameraRotationEvent` already took a pivot (`setTargetPos`); in
  `FIRST_PERSON` it defaults to the camera's own position, and in `LOOK` the touch handler hands it
  over explicitly. Verified: the camera is unchanged to the metre across a 90° drag.
- **Pitching** in `FIRST_PERSON` writes `ViewState::setViewTilt`, which moves nothing at all — the
  camera keeps the position (and therefore the height) it had, and the difference to
  `getCameraTilt()` is applied as a rotation of the view about the camera. In the other modes the
  camera still orbits the focus, which is what a map tilt is.
- **Moving** (`TouchHandler::dualPointerMove`) translates camera and focus together, forward along
  the view flattened onto the ground and sideways along the right vector, `FreeRoamMoveSpeed` × the
  camera-to-focus distance per inch of drag — so it covers the same part of the view at any zoom,
  and it needs no ground under the touch, which matters when the view is aimed at the sky. Pinch,
  two-finger rotation, two-finger tilt and the kinetic handlers are all off in this mode: they are
  map gestures and this scheme has none of them.
- **`FreeRoamLookSensitivity`** is the turn in degrees per inch of drag (default 90).

### Panning speed on a tilted view

Not a free roam thing, but the same family of problem: on a tilted view a touch near the horizon
corresponds to a map point far away, so the exact grab-the-world pan moves the map by kilometres
for the same finger travel that moves it by metres at the bottom of the screen. Worse, the scale is
re-derived from wherever the finger is NOW, so a drag that starts close and travels up the screen
**accelerates while the finger is down** — measured at z15 tilt 65: the same 1300 px drag moved
2160 m that way against 1186 m at the speed it started with.

`Options::PanningSpeedMode` picks between them: `MAP` is the exact grab-the-world pan, `ANCHORED`
(the default) measures the scale where the gesture starts and keeps it for the whole gesture, and
`CONSTANT` always measures at the centre of the screen, so the speed depends neither on where the
finger started nor on where it goes. The two new modes pan by the SCREEN delta in the ground frame,
which also means they work with the view aimed at the sky, where there is no ground under the touch
for a map pan to hold on to.

A two-finger gesture cannot be synthesized with `adb` (one pointer only, and the emulator's touch
devices are not writable from the shell), so the demo has a panel button that feeds `onTouchEvent`
a real two-pointer `MotionEvent` sequence — same entry point as a finger.

## Looking above the horizon: a negative tilt

The tilt may now go **below 0**, and that is what "look up" is. It is opt-in: `MIN_SUPPORTED_TILT_ANGLE`
is -90 but the default tilt range is still `(3, 90)`, so a map only gets there if it asks —
`setTiltRange(MapRange(-90, 90))`.

The model matters, because the obvious version does not work. Tilting between 90 and 0 rotates the
camera **about the focus**; carrying that on below 0 puts the camera under the ground and the view
comes back inverted (ground below, sky at both edges), because the up vector is derived from a
focus point on the ground. That was tried, and it is why the ceiling stood.

What a negative tilt does instead: the camera **stays exactly where the tilt geometry left it** and
only the view direction pitches up, about the camera (`ViewState::calculateLookatMat`, and
`getCameraTilt()` — the tilt floored at 0 — is what positions the camera). `dist(camera, focus)` is
untouched, so zoom, the visible tile set and the near/far budget all still mean what they meant.
`CameraTiltEvent` spends only the part of the tilt at or above the horizon on moving the camera.

Two consequences had to be handled:

- **All-sky frames have no ground.** `calculateViewDistances` walks rays to the ground for near and
  far; with none of them hitting, it used to leave `far == near` and collapse the depth range onto
  the near plane — taking with it everything drawn into the sky, which parks just inside the far
  plane. It now falls back to the view distance the ground would have been drawn to.
- **The terrain clearance bound goes unsatisfiable.** The camera's height above the focus is
  `dist * sin(tilt)`, so approaching the horizon it rises by almost nothing as it zooms out and
  `ViewState::getTerrainMaxZoom` runs off to minus infinity. Clamping to that threw the map from
  z16 to its minimum zoom in a few frames (and, with the per-frame correction in `MapRenderer`,
  kept walking it out to z-33). `CameraClearance::maxZoom` now answers +infinity for a camera at or
  below the focus height, and both clamps (`ViewState`, `CameraZoomEvent`) take
  `max(getTerrainMaxZoom(), zoom)`, so the bound can stop a zoom-in but never push the map out: no
  zoom clears the terrain at that tilt anyway, and keeping the camera the user asked for beats
  emptying the world.

At tilt ≤ 0 the camera sits at the height of its focus, i.e. on the ground — which is exactly right
for looking at the sky, and means the terrain is seen edge-on at the horizon.

## What lives in the app, not here

The demo is worth reading as the worked example, and it is where all the astronomy lives:
`DemoAstro` (sun, moon and its phase, planets from JPL's Keplerian elements), `DemoStarCatalogue`
(bright stars + constellation figures), `DemoCelestial` (sun and moon with their real paths for the
day, sampled from the same ephemeris — visibly, each disc lands **on** its own arc, which is a free
check on both since they are computed independently) and `DemoStars` (one sprite per star, one
segmented arc per figure, planets). The layer only knows about directions, sizes and colors.

The demo's star sky mode is also the answer to "draw nothing but the sky": the map layers leave the
layer list entirely (not hidden — never built), the terrain is disabled and the clear colour goes
fully transparent.

## A transparent map

Two halves, and both are needed:

1. **A transparent clear colour** — `Options::setClearColor(Color(0, 0, 0, 0))`. The renderer works
   in premultiplied alpha (`glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`), so what is left in the
   framebuffer composites correctly; nothing else in the render path needs to change.
2. **A view that admits it does not cover its pixels** — `setTranslucent(true)`, added on all three
   view classes. The EGL configs already ask for RGBA8888 first, so this is about compositing, not
   about the drawable.

What each view can reveal is NOT the same, and this is the trap:

- **`MapView` (Android, SurfaceView)** — its surface is composited **below the window**, so it can
  only reveal *another surface* under it. Other views of the same layout are drawn *above* it and
  are not affected. `setTranslucent` therefore also calls `setZOrderMediaOverlay`, which is what
  puts the map above a camera preview surface. Changing it after attach recreates the GL surface.
- **`TextureMapView` (Android, TextureView)** — an ordinary view in the hierarchy
  (`setOpaque(false)`), so it blends with whatever is behind it in the layout. This is the one for
  a map over other UI.
- **`MSFMapView` (iOS, GLKView)** — `opaque = NO` on the view and its layer, and a clear background.

The demo wires the first case end to end (`DemoCameraPreview`): a plain `SurfaceView` added at
index 0 runs a Camera2 preview, the map sits over it as a media overlay, and the sky is drawn on
the camera image — verified on the emulator's virtual scene.
