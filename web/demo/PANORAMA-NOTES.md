# The panorama bench: how to run it, and the two problems still open

For whoever picks up the peak-finder render next. Read the "How not to waste a day" section
before you form a theory — most of the time lost on this has gone to measuring the wrong thing.

## Running it

Serve the SDK's `web/` directory on 8099 (any static server; it is already running in most
sessions). The page is `demo/panorama.html`.

Rebuild after touching any C++:

```
cd /Volumes/dev/carto/mobile-sdk
EMSDK=/Volumes/dev/carto/emsdk python3 scripts/build-web.py --profile full --build-demo \
  --cmake-options "CMAKE_CXX_FLAGS=-DMASSIF_VT_RENDER_STATS=1"
```

Incremental builds are about 2.5 minutes. `MASSIF_VT_RENDER_STATS` gates the `RenderStats`
counters and their once-a-second `Log::Infof`, which the page mirrors into the DOM (`?log=1`).

The shaders are GENERATED from alpimaps, not copied. After editing
`app/mapModules/terrain/reliefShaders.ts`:

```
node web/demo/gen-relief-shaders.mjs
```

No rebuild needed for that — it writes `web/demo/relief-shaders.js`, which the page imports.
**Do not put a backtick in a GLSL comment**: the generator scans the template literal and a
backtick silently truncates the shader (it took a "EOF while in a comment" compile error to find).

### Capturing headlessly

`demo/bench.mjs` drives Chromium over CDP with no dependencies:

```
node demo/bench.mjs --url "<url>" --width 1000 --height 470 --wait 15000 --out /tmp/a.png
```

`--step` runs a script: `wait:<ms>`, `shot:<path>`, `eval:<expr>`. One page load can shoot, pan
and shoot again. Console output, exceptions and the network log come back on stdout. Terrain
needs 15-20 s to settle at most viewpoints; anything shorter captures a load frame.

Useful globals on the page: `camera`, `look(rotation, tilt)`, `fov(deg)`, `ink(name, value)`,
`surface(name, value)`, `terrain(name, value)`, `debug(view)`.

## The looks

`?look=` selects a whole configuration. They are not variations on a theme; they are different
renderers.

- **`drape`** — the render from before per-fragment normals, and the one that was approved. A
  draped raster layer paints the terrain, so the surface shader contributes nothing and the
  outline operator's slope term IS the shading. Continuous, no seams, but it cannot light, haze
  or sun-shade anything because there is no surface in it. **Use it as the control**: if a change
  is meant to fix seams, it has to close the gap to this.
- **`geothree`** — geo-three's webapp ported term for term, and now matching it to a mean
  absolute difference of 1.6-2.5 grey levels at three viewpoints (was 6-20). See "Parity with
  geo-three" below for every term and its toggle.
- **`peakfinder`** — peakfinder.com's picture (`cfg=es`), see "Toward peakfinder" below.
- **(default)** — the older pipeline: per-fragment DEM normals in the surface pass plus the
  depth outline.

`?split=1` puts geo-three's own build in a frame beside ours on one camera. It is served from
this origin (`web/geo-three` -> `geo-three/example`) because the reference takes its camera from
`window.setPosition`/`setAzimuth` and cross-origin it can be shown but not steered.
The split is synchronised: both panes fit the skyline raymarched off the DEM at the same heading,
pitch and field of view to 0.1 degrees. What it took: its altitude is ABOVE THE SEA (ours is above
the ground), its azimuth is NOT negated, its `stickToGround` (eye at least 60 m up) is off, labels
are off (`?labels=1`), and the `split` class goes on before the wasm boots or our canvas buffer
stays full-width, squashed into half the page.

Its world is Web Mercator sideways and true metres up, so at `exageration` 1 its relief is
flattened by cos(lat). The split passes `1 / cos(lat)`: both render true terrain.

## Parity with geo-three (`?look=geothree`)

Source: `alpimaps/geo-three/webapp` (`app.ts`, `MaterialHeightShader.ts`, `source/lod/LODFrustum.ts`).
Each term, and what reverts it for comparison:

| term | geo-three | how we match | toggle |
|---|---|---|---|
| ink | terrain TRANSPARENT (generateColor off writes `vec4(0)`), outline mixed in alpha included, AVERAGE blend: ink = `min(d*d/2, 1)` | power 0.46, `uIntensity` 0.5, `uOutlineCeiling` 2 | - |
| skyline | sky is depth 1, operator inks both sides, saturates black | `uInkSky` 1, plus our terrain-side stroke (`uHorizonBoost` 1, `uHorizonWidth` 3) | `?skyline=ref` reference alone, `?skyline=heavy` stroke alone |
| palette | white page, pure black ink | constants rewritten in the page | `?palette=ours` |
| depth | DEPTH_COMPONENT24 perspective, linearised in float | `uDepthBits` 24, `uDepthUnit` 1e-5/cos | `?depth=linear` |
| depth range | 10 m .. 173 km MERCATOR | ×cos(lat), `viewDistance` too | - |
| resolution | full | `postProcessDownscale` 1 | - |
| terrain cut | LODFrustum: subdivide while centre < `70·2^(20-z)` m, to z17 | `TerrainOptions::setSubdivideDistance(70)`, `setMaxZoom(17)` | `?lod=ours` |
| mesh | `512/3` cells to z12, halved above, min 16; bilinear heights; no stitching | same option; `meshResolution` 171 | `?lod=ours` |

The terrain cut is what the 1.8 px "pitch" offset was: our coarse far tiles and box-averaged
heights shaved the ridges down. With the cut ported the skylines coincide to 0 px.

`uOutlineWidth` DILATES the one-pixel operator (max over a cross of that radius); it used to space
the taps wider, which measured the slope over more ground and greyed the whole picture. Width 3 now
moves the slope grey by 1 level. `uDepthFar` no longer clips: ground past it keeps its lines, so
the reach is `viewDistance` alone (0 = the frame's far plane). The depth outline has no distance
fade and the surface no haze any more; the SDK fog (`?fog=1`) is the one distance effect.

## Toward peakfinder (`?look=peakfinder`)

Their shader is in `alpimaps/app/mapModules/terrain/peakfinder-reference-shader.md`. Their runtime
values, read off the live page's WebGL state (`gl.getUniform` on the programs it binds):
`u_fragmentParams0 [1.297, 1, 0, 1]`, `params1 [0.6, 0, 0, 8]` (ridge 0.6, slope 0, silhouette 8),
`params2 [0.3, 0, 0, 0.05]` (cap 0.3, sun 0.05), `params3 [0.05, 1, 0, 0]`, `u_fragmentColors
[0, 0.97]`. So their shading is FAINT: ink is capped by the light, a sunlit face stays paper and the
shadow side carries a few percent of ridge texture. Their black lines are a separate LINE pass
(`u_linewidth` 6 @ 0.1 grey, 2 @ 0.2 grey, in device pixels), not the shading.

Ours: the surface shader is their model (slope + ridge ADDED, capped by `uAmbient + uShadeStrength *
max(-0.2, -dot(n, sun))` and `uInkCap`), plus a hillshade after the cap - `uHillshade * max(sun.z -
dot(n, sun), 0)`, zero on flat ground - which is what the sun azimuth moves (their sun only caps, and
flipping it at their strength moved the picture 0.11 grey levels); the ridge is the DEM's curvature over `normalSampleDistance`,
scaled to one pixel's ground (`uPixelAngle`, set from the field of view); the lines are the depth
outline's `uOperator` 2 - the laplacian of inverse depth, near side only, which is zero on any plane
and so inks occlusions and nothing else. DEM is mapterhorn z16 (the app's). The camera for their
capture: `?lat=45.17173&lon=5.72455&elevation=234&tilt=4.62&rotation=-11.3&fov=24`; the panel links
the current view on peakfinder.com (their `azi` is minus our rotation, `fov` is horizontal, and
their `alt` was 0.8 deg less than our tilt at this viewpoint).

Measured against their capture (mountain crops, mean grey): 243.1 vs 243.8 and 238.7 vs 239.3.
Their sky is their paper, 247, and sunlit flat ground 250 (their ink goes negative under the sun);
ours stays on paper. Paper is `?paper=<grey>` (panel slider), sky and terrain background follow it:
1 by default, 0.97 is theirs. Their texture is streakier - their
cast-shadow term (`+0.05 * (0.5 - z)`) has no input here; `uAmbient` 0.06 stands in for it.

Two DEM precision faults found on the way, both of which drew contour-like stripes:

- `v_worldPos` is an absolute internal position in float; its ulp at Grenoble is 0.0625 units, half
  a z15 texel and twice a z17 one. The shaders now get `v_demUv`, computed per vertex from a
  per-tile matrix built in double (`u_demUvMat`), and `terrainNormal` taps in uv.
- The texture's own bilinear filter is NOT exact on a packed height: terrarium's R channel carries
  every 256 m, and the filtered result is ~1 m off there. Invisible in a height, a spike in any
  derivative - dotted bands along every 256 m contour. The ridge term interpolates by hand
  (`exactHeightUv`, four texel-centre reads). The SDK's `terrainNormal` still reads the filter; at a
  90 m span the error is a 0.5% slope, at one texel it is visible.

## Debug views

`&debug=N`, on the surface shader. They only work when the surface is actually visible — see
below.

| N | shows |
|---|---|
| 9 | mesh density per tile, one colour per bucket |
| 10 | **DEM zoom per tile**, one hue per level. The most useful view here. |
| 13 | tile boundaries over the real shading |
| 20 | the DEM uv a fragment resolves to; blue means outside the texture |
| 21 | metres per texel |
| 22 | which normal path a fragment took |

## How not to waste a day

Four failure modes have each cost hours. All of them look like "the change had no effect".

1. **A tile layer hides the surface entirely.** With a draped raster or vector layer in the
   scene the surface shader's output never reaches the screen, and every surface parameter reads
   as a no-op — `uShadeStrength` at 0 and at 1.5 both changed 33 pixels (the HUD). The panorama
   page defaults to `source=none` for this reason. Every debug view is dead in that state too.
2. **Many `TerrainOptions` setters do not invalidate what is already built or culled.** Measured
   as 0 pixels changed at runtime: `meshResolution`, `tileEdgeStitching`, `viewDistance`,
   `meshCacheSize`. The page passes these through `MASSIF_DEFAULTS` so `main()` applies them at
   init. If a terrain knob "does nothing", check that before concluding anything.
3. **`setMeshResolution` used to clamp at 256** (now 1024). Asking for 512 silently became no
   change.
4. **Verify the instrument before trusting it.** A whole round of conclusions came from
   `debug=22` while `uDebugView` was not reaching the shader at all. Diff two captures and count
   changed pixels; do not read screenshots for anything quantitative.

Measuring: compare captures with PIL and count non-identical pixels. A settled frame should be
bit-identical across runs — if it is not, something is load-order dependent and that is its own
bug. Luminance statistics (`mean`, `stdev`, fraction below mid-grey) are how the render gap is
quantified; see below.

## Open problem 1: seams between tiles

**Symptom.** Creases and steps along tile boundaries. Reproduce at:

```
http://localhost:8099/demo/panorama.html?lat=45.17173&lon=5.72455&elevation=790&tilt=4&rotation=11.22&fov=24&zoom=13&t.postProcessDownscale=1&look=geothree
```

`&debug=13` confirms they follow tile boundaries; `&debug=10` shows why.

**What is already known, with measurements.**

- Edge stitching used to key on TILE zoom. Neighbours are usually at the same tile zoom, so the
  mask was almost always zero and stitching on against off changed 97 pixels of 470000.
- A tile's heights come from the DEM zoom it RESOLVED, which moves independently of its tile
  zoom: one tile lands on its own grid, the neighbour on a cached ancestor covering sixteen times
  the ground. `calculateEdgeMask` now takes the cut's resolved DEM zooms and uses the coarser of
  the two differences per side. Instrumented: the mask now fires on **6150 of 8250 tiles**, DEM
  zooms spanning **6..15**.
- **That fix does not close the visible seams.** The render is bit-identical before and after,
  and stitching on against off still moves 162 pixels. So the geometry was never what disagreed.
- The mask is also widened from two bits a side to three (`EDGE_MAX_LEVELS` 3 -> 7), since
  neighbours are measurably more than three levels apart.

**Where to look next.** The seam is in the SHADING, not the geometry. Each tile binds its own
elevation texture and `terrainNormal` (in the surface fragment prefix, `TerrainRenderer.cpp`)
samples it per fragment. Two tiles on DEM z15 and z6 compute different normals along a shared
edge however well their heights are stitched. Continuity of the per-fragment normal across a
tile edge is the thing to fix — either by sampling a common level near edges, by making the
neighbour's texture reachable, or by bounding how far apart adjacent tiles' DEM levels may be.

## Solved: the render was much darker than the reference

It was the ink formula, not the depth or the normals: geo-three mixes its outline into a
transparent terrain, alpha included, which squares it. See the parity table. Mean luminance at
the Grenoble split is now 241 against its 243 (was 177).

## Things deliberately not done

- The reference's `exageration` of 1.6225 is not matched, by request: both sides render true.
- The app runs `?look=peakfinder` (`PEAKFINDER_LOOK` in `reliefShaders.ts`); `geothree` stays here to compare.
- The surface-pass ridge ink is only on under `?look=peakfinder`. The old laplacian drew blobs; the
  blobs were mostly the two precision faults above.
- Nothing in any of this is verified on device. The gesture changes in `TouchHandler` and
  `Options` affect the app on device as well as this bench.
