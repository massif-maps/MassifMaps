---
title: Sun exposure along a route
sidebar_position: 22
---

# Sun exposure along a route

`computeSunExposure` answers, for a list of positions and the sun at each, whether the **terrain**
hides the sun: a fraction of the sun's disc that is visible (0 hidden, 1 lit), the horizon angle,
the distance to the ridge that hides it, and flags for what the answer is built on. It runs offline
on the DEM, in milliseconds per thousand points once the tiles are decoded, so a route's sun and
shade can follow a departure-time slider.

The SDK does **no astronomy**. The caller passes the sun direction at each position (for a route,
each point has its own time, so the sun moves along it): azimuth in degrees clockwise from north
and the *apparent* altitude in degrees.

Terrain only. Forests, buildings, walls, tunnels and bridges are not in the DEM: a tunnel is "lit",
a wooded road is optimistic.

## Call it

On `HillshadeRasterTileLayer` (the elevation source of a hillshade, no 3D needed) or on
`TerrainOptions` (over the 3D view's own cache; prefer the layer for a route feature).

```kotlin
val options = SunExposureOptions().apply { eyeHeight = 1.5 }
val result = hillshade.computeSunExposure(positions /* MapPosVector, WGS84 */, sunDirections /* DoubleVector: az, alt, az, alt... */, options)
```

```ts
// facade: positions [lon, lat], sunDirections flat, options a plain object or null
const doubles = await layer.callAsync('computeSunExposure', [positions, sunDirections, { eyeHeight: 1.2 }]);
```

The call **blocks while it loads elevation tiles**. Call it off the UI thread (`callAsync` on the
facade) and in chunks of about 250 consecutive positions: a chunk bounds the wait and the memory,
and a running call cannot be cancelled (the app drops the result and stops sending chunks). Calls on
one layer queue behind each other, so a long chunk delays an elevation-profile call on it.

Space the positions by about a minute of travel, 100 m on a bike, 50 m on foot. Edges of a shade
patch are found to the spacing, so totals are robust (within a point or two against a 12 m
reference) but a ribbon wants those steps.

## Result

A flat array of doubles: 8 header numbers, then 4 per position.

| Header | Meaning |
|---|---|
| 0 | version, 1 |
| 1 | count of positions |
| 2 | stride, 4 |
| 3, 4 | elevation tiles loaded, tiles asked for and not obtained |
| 5, 6 | lowest and highest tile zoom the answers used, -1 when none |
| 7 | flags: 1 the load deadline was hit, 2 some point was truncated at the maximum distance |

| Per position | Meaning |
|---|---|
| `sunFraction` | visible share of the sun's disc, 0 to 1; -1 unknown. Penumbra-aware |
| `horizonDeg` | angle of the highest ground toward the sun; a lower bound where hidden (the march stops once the sun is fully hidden); -90 none |
| `shadowDistanceM` | metres to the first ground that hid the sun's centre, -1 none |
| `status` | bit set, below |

| Status bit | Meaning |
|---|---|
| 1 `COARSE_DEM` | some answer came from a grid more than two levels coarser than wanted, or a tile beyond 500 m was missing |
| 2 `NO_DEM` | no ground at the point or within 500 m: `sunFraction` is -1, never a guess |
| 4 `DARK` | the sun is under `darkBelow`; not marched, `sunFraction` 0 |
| 8 `LOW_SUN` | under `lowSunBelow`: refraction and the far field decide more than the march |
| 16 `TRUNCATED` | lit, but the march stopped at `maxDistance` with the sun under 4 degrees, so a ridge further out may hide it |

The call never throws on missing data: a point that could not be answered says so. Offline, tiles
that are not in a package fail fast and are remembered for 30 s.

## Options

| Property / key | Default | Meaning |
|---|---|---|
| `eyeHeight` | 1.5 | metres above the ground at the point |
| `maxDistance` | 30000 | cap on the march, m |
| `discRadius` | 0.2666 | sun disc radius in degrees, the penumbra's half-width; 0 gives only 0 or 1 |
| `darkBelow` | -0.2666 | apparent altitude under which a point is `DARK` (use -0.833 for a *true* altitude) |
| `lowSunBelow` | 2 | `LOW_SUN` flag |
| `firstStep`, `stepGrowth` | 10 m, 0.003 | step = max(firstStep, distance x stepGrowth) |
| `lodRatio`, `minTexel` | 1/300, 5 m | the DEM level read at distance d has the texel closest to max(minTexel, d x lodRatio) |
| `refraction` | 0.13 | as `calculateHorizon` |
| `loadMissingTiles` (facade `load`) | true (`"allow"`) | false (`"cached"`): never wait, ask for missing tiles in the background and flag them; call again a moment later |
| `loadTimeoutMs` | 20000 | deadline for the load phase of one call; 0 none. Past it the call computes with what it has |
| `cacheMB` | 96 | what one call may hold in decoded grids (about 1 MB per 512-texel tile); past it, tiles are not loaded and their points are flagged |
| `releaseCache` | false | empty the elevation cache of the layer or terrain after the call, for the last chunk of a route |

## How it works

For each position one ray goes toward the sun's azimuth. The terrain's angle at distance `d` is
`(z - d^2 / 2R - eye) / d` with the earth's curvature and refraction, and it is compared to the sun's
altitude (tangents, one `atan` at the end).

- **Exits.** The march stops *shaded* as soon as the running maximum exceeds the sun's upper edge,
  and *lit* once the highest ground of the region cannot reach the sun's lower edge any further
  out. The angle of fixed-height ground only falls with distance, so both exits are exact. A tile
  whose highest ground cannot reach it is crossed in one jump.
- **Level of detail follows distance.** Near the point the DEM is read at 5 m or finer, at 3 km at
  10 m, and so on: the angular error from posting stays under the sun's disc radius.
- **Loading.** Three rounds: the coarse tiles around the route (highest ground, how many texels a
  tile has), then every tile the marches can touch in route order, then the marches over the grids
  the call holds. Tiles load through the elevation manager's exact load (never a coarser stand-in),
  on three threads, so a tile is not decoded twice.

Measured on a prototype over a real 10 m DEM (Mont Blanc, 83 km synthetic route, one desktop
thread, tiles in RAM; **not measured on a device**): 0.6-1.5 ms per 832 points with 99.8-100 %
class agreement with a 0.1 % step reference. Tile loading, not the march, is the cost: 33-41
512-texel tiles for that route. The sun's position moves 60 degrees over a six-hour ride, so a 30
minute error in the departure time moves the sun share by 2 to 12 points.

The model, the tests and the host-side check: `all/native/terrain/SunExposure.h`,
`tests/api/SunExposureTest.cpp`.
