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
| `onMapMoved(reason)` | `TouchHandler::checkCameraEvents`, and `MapRendererListener::onMapChanged` for everything else | the camera changed, and what changed it |
| `onMapInteraction` | the touch pipeline only, with pan/zoom/rotate/tilt flags | the **user** changed the camera; always `GESTURE` |
| `onMapIdle` | `MapRenderer::onDrawFrame`, when a frame ended with no redraw pending | the renderer has nothing more to draw |
| `onMapStable(reason)` | `TouchHandler::checkMapStable` | a movement **ended**: at rest, no fingers down, no kinetic running |

## The reason

Every camera event carries a `MapMoveReason`, so an app can tell its own moves from the user's
without watching touches itself:

| Reason | Raised by |
|---|---|
| `MAP_MOVE_REASON_GESTURE` | a gesture, the wheel, or the kinetic inertia that follows one |
| `MAP_MOVE_REASON_ANIMATION` | a frame of an animation the SDK is stepping - a flight, or any call given a duration |
| `MAP_MOVE_REASON_API` | a call that took effect immediately, and the SDK's own camera corrections (the terrain clearance clamp) |

It is decided where the movement starts and travels with the event:
`MapRenderer::calculateCameraEvent(..., reason)` -> `viewChanged(delay, reason)` ->
`OnChangeListener::onMapChanged(reason)`. `KineticEventHandler` passes `GESTURE`,
`AnimationHandler` passes `ANIMATION`, `BaseMapView` passes `API`, and `TouchHandler`'s own
gesture path passes `GESTURE`.

`onMapStable` reports the reason of the movement that just ended - the last one, if several
sources moved the camera before it came to rest.

`onMapMoved` and `onMapInteraction` are not disjoint: a gesture raises both, back to back, and
`onMapChanged` suppresses its own `onMapMoved` when camera events are already pending so it is not
sent twice.

**`onMapStable` is about the camera, not the data.** Tiles may still be loading when it fires; it
does not wait for them. `onMapIdle` does not either — it only means this frame was the last one
requested. For "the camera settled, refresh my data", `onMapStable` is the event; for "everything
that was going to be drawn has been drawn", `onMapIdle` is.

## Threads

`onMapIdle` comes from the GL thread. `onMapStable` comes from the GL thread when it follows an
idle, and from the touch thread when it follows a finger lift. `onMapMoved` comes from whichever
thread changed the camera. **Do not mutate map state from any of them** — `MapEventListener.h` says
so, and it is a deadlock, not a race.

## What decides whether onMapStable fires

`checkMapStable` is called from two places — `TouchHandler::onTouchEvent` and
`MapRendererListener::onMapIdle` — and fires only on the **edge** from moving to at rest. It reads
four pieces of state:

- `_pendingMoveReason`, set by every reported movement and **taken** by the event. Taking it IS the
  edge: a second at-rest check finds nothing pending and stays quiet, and a touch that never moved
  the camera never sets one;
- `_idling`, set by `onMapIdle` and cleared by `onMapChanged`;
- the three `KineticEventHandler` flags (`isPanning`, `isRotating`, `isZooming`);
- `_pointersDown`.

So it is once per movement, and a tap that moved nothing raises nothing. Before 6.1 it was a poll
over the last three only, which fired on such a tap and could fire repeatedly while at rest — once
per idle, whenever a label fade or a tile arrival kept requesting redraws. Apps worked around that
with an app-side "did it actually move" flag; that flag is now the SDK's job.

### Why it used to stop firing for good

Before the edge trigger, every one of the three at-rest inputs was reachable in a state nothing
recovered from
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

`onMapStable(reason)` is our `moveend` with their `reason`, under a different name. We still have no
`movestart`, and `onMapInteraction` carries flags that belong on the move event itself.

## Known gaps

- **No `movestart`.** MapLibre and Mapbox both raise one; we have the change and the end, not the
  start. An app that wants "the user began moving" watches the first `onMapMoved` after a stable.
- **`onMapInteraction` is redundant.** Its pan/zoom/rotate/tilt flags belong on the move event, and
  its "this was the user" meaning is now `reason == GESTURE`. Kept for compatibility; the next
  major should fold it in.
- **No native debounce.** `throttle` — the leading edge — is a facade subscription option
  (`mm_on`'s `opts_json`, `MassifMap.onMove(handler, ms)`, `-onMove:throttle:`): a window on the
  subscription, checked in `EventBus::due` under the same lock as the lookup, dropping events that
  arrive too soon. Dropping rather than queueing is forced by the payload, which does not outlive
  the emit.

  The **trailing** edge is not there. It needs a scheduled wakeup, and the facade has a worker pool
  for async calls but no timer — sleeping a pool worker would block a call slot. The NativeScript
  plugin implements its own in JavaScript, delivering a snapshot read at emit time. A C++ version
  belongs beside `throttle` in the same options object.
