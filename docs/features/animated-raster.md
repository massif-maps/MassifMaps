---
title: Animated Raster Tiles
sidebar_position: 5.5
---

# Animated Raster Tiles

`AnimatedRasterTileLayer` plays a loop of time steps, such as a weather radar, where **each step is a
tile of its own**. Two consecutive steps are drawn in **one** pass and mixed in the shader, so the
cross-fade mixes values rather than finished images: no pop at a step boundary, no double-blend
darkening, and one draw instead of one per step.

It is a [`CustomRasterTileLayer`](/docs/features/custom-raster-shaders) with two sources: the step and
the next one. A step is uploaded once even though it is the second raster of one tile and the first of
the next.

## Usage

The URL carries the `{time}` tag of `HTTPTileDataSource`; the layer hands it the timestamps. Put a
`MemoryCacheTileDataSource` in front, and give it room: a step is the second raster of one tile and the first of
the next, so without it every step is fetched twice, and its default 6 MB holds about a third of a loop (a
64 MB `capacity` holds all of it). The layer looks through cache wrappers for the HTTP source and
logs a warning when it finds none.

```java
HTTPTileDataSource radar = new HTTPTileDataSource(0, 12,
    "https://api.librewxr.net/v2/radar/{time}/256/{z}/{x}/{y}/2/1_1.png");
AnimatedRasterTileLayer layer = new AnimatedRasterTileLayer(new MemoryCacheTileDataSource(radar));
layer.setFrameTimestamps(timestamps);          // oldest first: the epoch seconds of /public/weather-maps.json
MassifInterop.getLayerListByHandle(mapView.getLayersHandle()).add(layer);
```

With the [surface API](/docs/api/):

```java
map.addLayer("radar", Spec.of("animated-raster")
    .set("source", Spec.of("http").set("url", radarUrl).set("maxZoom", 12))
    .set("frameTimestamps", timestamps));
```

### The clock is yours

`setAnimationPosition(float)` takes a position in steps: the whole part picks the step, the fraction
is the mix towards the next. Drive it once per frame from your own clock, so the fade runs at the
display rate while the steps advance at the rate you want:

```java
// 4 steps a second, eased at the display rate
final long start = System.nanoTime();
Choreographer.getInstance().postFrameCallback(new Choreographer.FrameCallback() {
    @Override public void doFrame(long frameTimeNanos) {
        double seconds = (frameTimeNanos - start) / 1e9;
        layer.setAnimationPosition((float) ((seconds * 4.0) % timestamps.size()));
        Choreographer.getInstance().postFrameCallback(this);
    }
});
```

The position is clamped to the first and last step, so wrapping the loop is the clock's job. The last
step has no successor and shows alone.

| Member | Purpose |
|---|---|
| `setFrameTimestamps(List<String>)` | The steps, oldest first, at most 255. The strings `{time}` takes. |
| `setAnimationPosition(float)` | Where the loop is, in steps. |
| `setPrefetchFrames(int)` | How many steps ahead are fetched before they are shown, default 2. A loop that must not stall on its first pass wants all of them. |
| `setPrefetchBudget(int)` | The memory in MB the prefetched steps of the visible tiles may take, default 192 (16 to 1024). A view that needs more prefetches fewer steps. |
| `setShaderSource(String)` | Replace the default `mix(stepFrom(), stepTo(), stepMix())`, e.g. to colour-map the values before they are mixed. |

### In your own shader

Three helpers come ahead of your source: `stepFrom()` is the shown step, `stepTo()` the next one, and
`stepMix()` how far the position is between them. Colour-map each before mixing, so the ramp sees real values:

```glsl
vec4 applyLighting(lowp vec4 color, mediump vec3 normal, mediump vec3 surfaceNormal, mediump float intensity) {
    return mix(ramp(stepFrom()), ramp(stepTo()), stepMix());
}
```

`stepMix()` is computed from the step the *tile* was built for, which rides along as a 1x1 raster, not from a
global blend. A tile that is swapped in a few frames late, or a stand-in shown while the next one loads, therefore
always shows its own step at the right strength: nothing jumps back at a step boundary.

## How a step is cached

A step is identified by a **frame number** derived from its timestamp, never by its place in the list:
epoch seconds give minutes modulo 65 536 (about 45 days), anything else a hash. A list that rolls on
every ten minutes keeps the steps it still has, in the memory cache and the persistent one, and a
new step can never be served an old image. Two timestamps of one list that would share a number (steps under a
minute apart, or a hash collision) get the next free one in list order, so keep the list sorted the same way.

Frame numbers must stay under about 98 000 to fit a tile id, which 65 536 does with room to spare.

### Persistent cache

Off unless you put a `PersistentCacheTileDataSource` between the HTTP source and the layer. If you do:

- enable `setMaxAgeHeaderCheck(true)` on the HTTP source, or a step never expires,
- give the cache **its own small database**, never the basemap's,
- `setFrameTimestamps` calls `purgeExpired()` on it, so a step that rolled off the list and is never asked
  for again does not hold its capacity. The purge scans the table, which is why it is not done at open.

## Loading

A time-stepped service renders each tile on demand, so most of the wait is the server: LibreWXR answers a cold
tile in 1.5 to 4 s and does not go faster when asked in parallel, so a loop of 18 steps over a 40-tile view is
well over ten minutes of requests. What you control is how many tiles a loop needs: a source in front that is
asked once per tile, a coarser `zoomLevelBias` (radar is 1 to 2 km data, so `-1` loses little and asks for a
quarter of the tiles) and a persistent cache, which keeps a fetched tile for the age the server gives it.

The layer fetches the next `prefetchFrames` steps of every visible tile (wrapping round to the first) at
preloading priority, asks the source for a step once however many tiles want it at that moment, and sizes its
own tile cache to hold them, so a step is not evicted and fetched again when the loop comes round. The window
is capped by `prefetchBudget` (192 MB of decoded tiles by default): on a large view fewer steps are prefetched,
and the rest load from the memory cache. The default of 2 suits a slow clock; a loop that plays at once wants all of them, which
costs the memory of a tile per step and visible tile. A step that is not loaded yet keeps showing the previous
one; a tile whose next step is missing is fetched again later, after 1 s, then 2 s, 4 s... up to a minute, and
shows its own step until then.

## Draped on 3D terrain

On terrain the layer is baked into the terrain texture, and a changed position bakes the stack again. A bake is
a whole tile, so a change reaches the drape at most 10 times a second (the blend of a baked tile is the value at the
bake, so a draped fade runs at that rate), whatever the clock. A flat map has no such cost and fades at the display rate.

## Known gaps

- Both steps of a tile must come from the same tile of the pyramid; per-raster offsets are not ported.
- `raster-particle` style wind particles are a different renderer and not part of this.
- A loop of more than two simultaneous rasters is a [`CustomRasterTileLayer`](/docs/features/custom-raster-shaders#several-sources-in-one-draw) job; the cap is 4.
