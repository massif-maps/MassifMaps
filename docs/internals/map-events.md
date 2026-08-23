---
title: Map events
description: "How onMapMoved, onMapInteraction, onMapIdle and onMapStable are raised, which thread each comes from, and the state that decides whether they fire at all."
sidebar_position: 5
---

# Map events

Scope: the camera and touch events an app receives through `MapEventListener` (and their facade
mirrors `map.moved`, `map.interaction`, `map.idle`, `map.stable`). Not the click/selection path,
and not the render loop itself — that is [the frame](rendering/01-frame.md).

## The four events

| Event | Raised from | Means |
|---|---|---|
| `onMapMoved` | `TouchHandler::checkCameraEvents`, and `MapRendererListener::onMapChanged` for everything else | the camera changed, whatever the source |
| `onMapInteraction` | the touch pipeline only, with pan/zoom/rotate/tilt flags | the **user** changed the camera |
| `onMapIdle` | `MapRenderer::onDrawFrame`, when a frame ended with no redraw pending | the renderer has nothing more to draw |
| `onMapStable` | `TouchHandler::checkMapStable` | idle **and** no fingers down **and** no kinetic animation running |

`onMapMoved` and `onMapInteraction` are not disjoint: a gesture raises both, back to back, and
`onMapChanged` suppresses its own `onMapMoved` when camera events are already pending so it is not
sent twice.

**`onMapStable` is about the camera, not the data.** Tiles may still be loading when it fires; it
does not wait for them. `onMapIdle` does not either — it only means this frame was the last one
requested.

## Threads

`onMapIdle` comes from the GL thread. `onMapStable` comes from the GL thread when it follows an
idle, and from the touch thread when it follows a finger lift. `onMapMoved` comes from whichever
thread changed the camera. **Do not mutate map state from any of them** — `MapEventListener.h` says
so, and it is a deadlock, not a race.

## What decides whether onMapStable fires

`checkMapStable` is **polled**, from two call sites only — `TouchHandler::onTouchEvent` and
`MapRendererListener::onMapIdle` — and reads three pieces of state:

- `_idling`, set by `onMapIdle` and cleared by `onMapChanged`;
- the three `KineticEventHandler` flags (`isPanning`, `isRotating`, `isZooming`);
- `_pointersDown`.

Two consequences an app has to know about: it fires for a **tap that moved nothing** (the map is at
rest, so the condition holds), and it can fire **repeatedly** while at rest, once per idle, whenever
something else keeps requesting redraws — a label fade, a tile arrival.

### Why it used to stop firing for good

Every one of the three inputs was reachable in a state nothing recovered from
([#162](https://github.com/massif-maps/MassifMaps/issues/162)). The failure was always the same
shape: `onMapStable` silently never fires again until the app is restarted.

- **Latched kinetic flags.** `startPan/startRotation/startZoom` set their flag unconditionally, but
  only `calculatePan/Rotation/Zoom` cleared it — and those return early when the matching
  `Options::isKinetic*` is off. With kinetic zoom or rotation disabled, the pinch-release path
  (which starts both) latched the flag forever. The starts are now guarded, and `calculate` clears
  a flag whose option was switched off mid-flight.
- **A dropped UP.** `_pointersDown` was incremented and decremented per delivered event, and only
  `ACTION_CANCEL` reset it, so any UP the platform never delivered stuck it above zero permanently.
  It is now **assigned** on DOWN (1 for the first pointer, 2 for the second), which makes every
  gesture start a resync point.
- **A consumed touch event.** `onTouchEvent` returned early when an `OnTouchListener` claimed the
  gesture, skipping the pointer accounting *and* both event checks. The listener chain now decides
  who handles the gesture, not whether the handler's own bookkeeping runs.

Two platform-side holes fed the second one, both fixed in `MapView.java` / `TextureMapView.java`:
the `isClickable() || isLongClickable()` test ran per event rather than per gesture, so a host
framework toggling it mid-touch delivered the DOWN and dropped the UP; and the `catch
(IllegalArgumentException)` around the dispatch swallowed the UP without telling native, which now
gets an `ACTION_CANCEL` instead.

Ruled out along the way, so nobody re-investigates them: the min-zoom and terrain-clearance clamps
do not stall kinetic (`_zoomDelta *= factor` runs whether or not the camera actually moved), and the
frame loop always draws at least one more frame after the last change (`_redrawExtraFrames`), so the
terminal kinetic frame's own `onMapIdle` sees the flags already cleared.

## Compared to maplibre / mapbox

They ship one phased camera stream, not two parallel ones: `movestart` / `move` / `moveend` (plus
per-axis variants) in MapLibre GL JS, each carrying `originalEvent` — present for a gesture, absent
for a programmatic move — and `onCameraMoveStarted(int reason)` / `onCameraMove` / `onCameraIdle`
with `REASON_GESTURE` / `REASON_API_ANIMATION` / `REASON_DEVELOPER_ANIMATION` on Mapbox and Google
Maps for Android.

We have no `movestart`, and `onMapStable` is our de-facto `moveend`. `onMapInteraction` carries the
information that belongs on the move event itself.

## Known gaps

- **No reason on the event.** Bindings reconstruct "was this the user" by watching touches
  themselves — the NativeScript plugin subclasses the map view on both platforms to keep a
  `userAction` flag. [#163](https://github.com/massif-maps/MassifMaps/issues/163).
- **`onMapStable` is polled, not edge-triggered**, hence the tap-fires-stable and repeated-stable
  behaviour above. Same issue.
- **No `movestart`.**
- **No native debounce.** Apps that refresh data on stable throttle it themselves. The facade's
  subscription options (`mm_on`'s `opts_json`) already carry `delivery` and `coalesce` and are
  designed to take new keys without a signature change, so a `debounce` belongs there rather than on
  `Options`.
