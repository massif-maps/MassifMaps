# Changelog

All notable changes to this project will be documented in this file. See [standard-version](https://github.com/conventional-changelog/standard-version) for commit guidelines.

## [v6.1.2] - 2026-10-03
### BREAKING CHANGES
- due to [`333351a`](https://github.com/massif-maps/MassifMaps/commit/333351a3a4c3dab9541f4f1514fb8c4558c2e996) - show peak icons and keep flat buildings past z15 in the Massif styles *(PR [#312](https://github.com/massif-maps/MassifMaps/pull/312) by [@farfromrefug](https://github.com/farfromrefug))*:

  the poi-rank-r1/r7/r20 layers are replaced by poi-rank-r10(-late), poi-rank-r30  
  (-icon, -shop), poi-rank-r70 and poi-rank-all; a child rule naming the old zooms moves with them.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * fix(styles): place Massif summits before every POI and road name, so a peak stays when zooming in  
  The peak layers were among the labels that give way to everything (16.5M), so a hut or a ruin  
  beside a summit won the collision. They are now their own part, after the POIs and the road and  
  trail names and before the place names, as MapTiler outdoor orders them: 23.5M-23.7M, the boost  
  term kept. The prominent summits are placed before the minor ones.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  ---------  
  Co-authored-by: Claude Opus 5.5 <noreply@anthropic.com>  
  ---------  
  Co-authored-by: Claude Opus 5.5 <noreply@anthropic.com>  
  ---------  
  Co-authored-by: Claude Opus 5.5 <noreply@anthropic.com>  
  ---------

- due to [`23e2e70`](https://github.com/massif-maps/MassifMaps/commit/23e2e70998abbf5d2b8d650abba2aec7edea0ecd) - draw trees and point barriers in the Massif styles *(PR [#318](https://github.com/massif-maps/MassifMaps/pull/318) by [@farfromrefug](https://github.com/farfromrefug))*:

  draw trees and point barriers in the Massif styles (#318)


### New Features
- [`23e2e70`](https://github.com/massif-maps/MassifMaps/commit/23e2e70998abbf5d2b8d650abba2aec7edea0ecd) - **styles**: draw trees and point barriers in the Massif styles *(PR [#318](https://github.com/massif-maps/MassifMaps/pull/318) by [@farfromrefug](https://github.com/farfromrefug))*

### Bug Fixes
- [`333351a`](https://github.com/massif-maps/MassifMaps/commit/333351a3a4c3dab9541f4f1514fb8c4558c2e996) - **styles**: show peak icons and keep flat buildings past z15 in the Massif styles *(PR [#312](https://github.com/massif-maps/MassifMaps/pull/312) by [@farfromrefug](https://github.com/farfromrefug))*
- [`f1d2d2a`](https://github.com/massif-maps/MassifMaps/commit/f1d2d2ac024693cd54e7c05d8931b252dee7df47) - **labels**: keep each point of a merged MultiPoint its own label past the source's max zoom *(PR [#317](https://github.com/massif-maps/MassifMaps/pull/317) by [@farfromrefug](https://github.com/farfromrefug))*
- [`319526b`](https://github.com/massif-maps/MassifMaps/commit/319526bd4f16b38f429131c4a15dbbc7a45887ed) - **datasources**: read a DEM archive's encoding from its metadata and stop a merged source at its overzoom *(PR [#316](https://github.com/massif-maps/MassifMaps/pull/316) by [@farfromrefug](https://github.com/farfromrefug))*


## [v6.1.1] - 2026-10-02
### BREAKING CHANGES
- due to [`aafed6f`](https://github.com/massif-maps/MassifMaps/commit/aafed6f52a0d8cf4a49e61d6734a718f599e2b51) - bring Massif POIs in by rank and let a child style widen, hide or re-zoom them *(PR [#303](https://github.com/massif-maps/MassifMaps/pull/303) by [@farfromrefug](https://github.com/farfromrefug))*:

  the poiRanking style parameter is removed; POIs start by rank (z15-17) instead of  
  by category.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  ---------

- due to [`7a17c20`](https://github.com/massif-maps/MassifMaps/commit/7a17c20ced779be3034c9a567d02bfa8e34f7c9e) - draw POIs and house numbers on their building's roof by default in the Massif styles *(PR [#305](https://github.com/massif-maps/MassifMaps/pull/305) by [@farfromrefug](https://github.com/farfromrefug))*:

  the Massif style parameter poi_on_roof now defaults to 1 and also lifts house  
  numbers; set it to 0 to keep both on the ground.


### New Features
- [`aafed6f`](https://github.com/massif-maps/MassifMaps/commit/aafed6f52a0d8cf4a49e61d6734a718f599e2b51) - **styles**: bring Massif POIs in by rank and let a child style widen, hide or re-zoom them *(PR [#303](https://github.com/massif-maps/MassifMaps/pull/303) by [@farfromrefug](https://github.com/farfromrefug))*
- [`7a17c20`](https://github.com/massif-maps/MassifMaps/commit/7a17c20ced779be3034c9a567d02bfa8e34f7c9e) - **styles**: draw POIs and house numbers on their building's roof by default in the Massif styles *(PR [#305](https://github.com/massif-maps/MassifMaps/pull/305) by [@farfromrefug](https://github.com/farfromrefug))*

### Bug Fixes
- [`670e35f`](https://github.com/massif-maps/MassifMaps/commit/670e35f157e31a549cadf795f223af54ea80c799) - **examples**: stop the day-cycle example drawing buildings out to the horizon *(PR [#302](https://github.com/massif-maps/MassifMaps/pull/302) by [@farfromrefug](https://github.com/farfromrefug))*
- [`d9d46cf`](https://github.com/massif-maps/MassifMaps/commit/d9d46cf90ef14d8fbe1ba7fba8b78bd9558164de) - **labels**: stop POIs vanishing mid-screen while you rotate or pan a 3D map *(PR [#304](https://github.com/massif-maps/MassifMaps/pull/304) by [@farfromrefug](https://github.com/farfromrefug))*
- [`c8613a4`](https://github.com/massif-maps/MassifMaps/commit/c8613a43cfc1633bde326e0798de29f6c787ca1e) - **renderers**: stop POIs blinking when the app moves a 3D map with moveTo *(PR [#306](https://github.com/massif-maps/MassifMaps/pull/306) by [@farfromrefug](https://github.com/farfromrefug))*
- [`b624efd`](https://github.com/massif-maps/MassifMaps/commit/b624efdd805efcb96b8f39cc8c74846a88bf35d9) - **renderers**: draw map elements at their own size again, and stop a tap moving the map *(PR [#307](https://github.com/massif-maps/MassifMaps/pull/307) by [@farfromrefug](https://github.com/farfromrefug))*
- [`5bcadca`](https://github.com/massif-maps/MassifMaps/commit/5bcadcabe6df0cfeb7240928fcc45eaf7a01b4da) - **labels**: stop a label group with no minimum distance hiding all but its first label *(PR [#308](https://github.com/massif-maps/MassifMaps/pull/308) by [@farfromrefug](https://github.com/farfromrefug))*
- [`5b273a8`](https://github.com/massif-maps/MassifMaps/commit/5b273a812ffbcbdce08b844b2ea004b811642a68) - **labels**: stop the placement worker spinning when there is nothing to place *(PR [#309](https://github.com/massif-maps/MassifMaps/pull/309) by [@farfromrefug](https://github.com/farfromrefug))*
- [`5e94133`](https://github.com/massif-maps/MassifMaps/commit/5e941339f66241bed706ccd2cc8d099ffc3dc601) - **ios**: merge MetalANGLE into the Mac Catalyst library *(PR [#310](https://github.com/massif-maps/MassifMaps/pull/310) by [@farfromrefug](https://github.com/farfromrefug))*
- [`22f1aef`](https://github.com/massif-maps/MassifMaps/commit/22f1aefafe716d672ea10e2e4a8dfdca4504fcb6) - **ios**: Mac Catalyst map touches, and two MBTiles crashes *(PR [#311](https://github.com/massif-maps/MassifMaps/pull/311) by [@farfromrefug](https://github.com/farfromrefug))*


## [v6.1.0] - 2026-10-01
### BREAKING CHANGES
- due to [`c56c62c`](https://github.com/massif-maps/MassifMaps/commit/c56c62c04fbc0249348b0600e6e6b48f622c33c4) - require OpenGL ES 3.0 and drop the ES 2.0 fallbacks *(PR [#142](https://github.com/massif-maps/MassifMaps/pull/142) by [@farfromrefug](https://github.com/farfromrefug))*:

  the SDK requires an OpenGL ES 3.0 context and will not run on  
  ES 2.0. No API changed - this is a device break. Apps shipping their own  
  AndroidManifest.xml with a glEsVersion line should raise it to 0x00030000.  
  Devices lost are pre-2013 GPUs (Mali-400, Adreno 200/305, Tegra 3, PowerVR SGX);  
  at minSdk 21 and an iOS 13 floor that is a rounding error. See docs/migration.md.  
  Refs #138  
  Part of #126  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  ---------

- due to [`7fbd68e`](https://github.com/massif-maps/MassifMaps/commit/7fbd68e566a9960c71bb7c0354a1ba19a572be5a) - compile the shaders as GLSL ES 3.00, keeping app shaders unchanged *(PR [#143](https://github.com/massif-maps/MassifMaps/pull/143) by [@farfromrefug](https://github.com/farfromrefug))*:

  shaders are compiled as '#version 300 es'. Application GLSL  
  needs no migration, but an app shader must not declare its own #version line,  
  nor a fragment output named TANGRAM_FragColor, which the preamble provides at  
  location 0. See docs/migration.md.  
  Refs #139  
  Part of #126  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(renderers): record the Adreno 610 verification of the ES 3.0 phases  
  Both phases now have a device behind them: a Crosscall HLTE556N, Adreno 610,  
  OpenGL ES 3.2. Zero shader compile or link failures, zero ESSL 3.00 -> 1.00  
  fallbacks, zero GL errors, and the map draws.  
  The device also exercised GLContext::VERSION against a real vendor string for the  
  first time - 'OpenGL ES 3.2 V@0502.0 (GIT@...)' parses to 320, which ANGLE's tidy  
  'OpenGL ES 3.0.0' never tested.  
  Record the control with the A/B, because the number is meaningless without it:  
  the same build run twice differs by 0.19% of sampled pixels, since labels are  
  placed as tiles arrive. ESSL 1.00 vs 3.00 differs by 0.37%, same band profile,  
  and the excess is confined to glyph pixels - black text against landcover green  
  in both directions, i.e. sub-pixel label antialiasing. Cropped and compared by  
  eye the frames are indistinguishable.  
  Also record what the A/B settled and inspection would not: a route line drawing  
  in broken chunks over terrain appears identically in BOTH builds, so it is the  
  known open terrain line-following issue rather than a regression from this work.  
  Refs #138  
  Refs #139  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  ---------

- due to [`a959a1c`](https://github.com/massif-maps/MassifMaps/commit/a959a1c31dad8371df18138aaa7bb2d493d62049) - close Phase 4 after three measured ES 3.0 experiments came back negative *(PR [#145](https://github.com/massif-maps/MassifMaps/pull/145) by [@farfromrefug](https://github.com/farfromrefug))*:

  shaders are compiled as '#version 300 es'. Application GLSL  
  needs no migration, but an app shader must not declare its own #version line,  
  nor a fragment output named TANGRAM_FragColor, which the preamble provides at  
  location 0. See docs/migration.md.  
  Refs #139  
  Part of #126  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(renderers): record the Adreno 610 verification of the ES 3.0 phases  
  Both phases now have a device behind them: a Crosscall HLTE556N, Adreno 610,  
  OpenGL ES 3.2. Zero shader compile or link failures, zero ESSL 3.00 -> 1.00  
  fallbacks, zero GL errors, and the map draws.  
  The device also exercised GLContext::VERSION against a real vendor string for the  
  first time - 'OpenGL ES 3.2 V@0502.0 (GIT@...)' parses to 320, which ANGLE's tidy  
  'OpenGL ES 3.0.0' never tested.  
  Record the control with the A/B, because the number is meaningless without it:  
  the same build run twice differs by 0.19% of sampled pixels, since labels are  
  placed as tiles arrive. ESSL 1.00 vs 3.00 differs by 0.37%, same band profile,  
  and the excess is confined to glyph pixels - black text against landcover green  
  in both directions, i.e. sub-pixel label antialiasing. Cropped and compared by  
  eye the frames are indistinguishable.  
  Also record what the A/B settled and inspection would not: a route line drawing  
  in broken chunks over terrain appears identically in BOTH builds, so it is the  
  known open terrain line-following issue rather than a regression from this work.  
  Refs #138  
  Refs #139  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(renderers): measure Phase 4 before implementing it, and kill its first two items  
  The five ES 3.0 harvests were ordered by "expected payoff" with nothing  
  measured. Measuring first, on the Crosscall HLTE556N (Adreno 610) at the  
  bench/city2d.sh camera, removes two of them.  
  Item 1, instancing billboards and markers, costs 0.1 ms CPU and 0.0 ms GPU at  
  this camera - there is nothing to win. Its premise is also already satisfied:  
  BuildAndDrawBuffers issues one glDrawElements per buffer-full, so billboards are  
  already one draw per batch rather than one per quad. What instancing would  
  really change is that the corners are rebuilt per frame and passed as  
  client-side arrays, which only matters in a scene that has billboards; this one  
  has none, so it would need a marker-heavy bench built first.  
  Item 5, glMapBufferRange for label streaming, measures as a no-op. Orphaning the  
  six per-batch label buffers behind debug.massif.labelorphan, interleaved over  
  two rounds with 41 windows per arm: 19.80 vs 20.00 fps. The same arm differs  
  more between rounds than the arms differ from each other - 18.90 then 19.80 for  
  'off', 4.5x the gap, against a stdev of 5.2. The buffers are a few KB each, so  
  orphaning solves a stall that does not happen. Reverted rather than shipped.  
  Record the unit trap that nearly picked the wrong item: RenderStats lines ending  
  '(per interval)' cover the whole ~1 s window, so labelBuild attribMs 17.4 is  
  0.76 ms/frame - and that clock covers CPU filling of the VertexArrays, which  
  buffer mapping does not touch.  
  What the numbers point at is not on the list. The frame is CPU-bound on draw  
  submission at 320 geometry draws/frame (63 tiles x ~5 style layers), so the  
  lever is merging geometry across tiles per style layer. Of the remaining items  
  only UBOs aim at the bottleneck, worth ~0.67 ms/frame, and it must be judged on  
  'layers' rather than fps.  
  Refs #140  
  Part of #126  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(renderers): find the shadow pass at 55% of the terrain GPU frame  
  The city bench has shadows off, which is why nothing in Phase 4 had found them.  
  Re-measured at the mountain camera with terrain, shadows and 3D buildings on,  
  same device and profiler build: GPU frame 27.5 ms, of which shadowCast 8.3-9.0  
  and shadowMask 6.9 - 15.2 ms, 55%.  
  That is the largest single cost measured anywhere in this phase and it gives  
  item 2, shadow cascades as a texture array, the only measured case of the five.  
  An array will not touch shadowCast, whose cost is casters x cascades; it can  
  touch shadowMask, which samples the _size * _cascades wide atlas with manual  
  slice offsetting and clamping; and it lifts the texture-size cap regardless,  
  since 4 x 2048 cascades would be an 8192-wide texture.  
  The phase now has two cameras with two different bottlenecks and a list written  
  for neither: the city is CPU-bound on 320 draws/frame and wants cross-tile  
  geometry merging, which is not on the list at all.  
  Refs #140  
  Part of #126  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(renderers): shadow cascades as a texture array cost 28% GPU - reverted  
  Phase 4 item 2, the only one of the five with a measured case behind it,  
  implemented and measured. Each cascade became a layer of a GL_TEXTURE_2D_ARRAY  
  (glTexStorage3D, glFramebufferTextureLayer per cascade) instead of a page of an  
  _size * _cascades wide atlas, the receiver sampled sampler2DArrayShadow, and the  
  atlas scale/offset in shadowFactorSlope went away. It rendered correctly:  
  shadows ACTIVE, no GL errors, no shader failures, cast shadows visually right.  
  Two APKs interleaved over two rounds at the mountain camera, 25 windows per arm:  
  shadowCast 7.80 -> 8.80, shadowMask 6.70 -> 10.30, GPU total 20.00 -> 25.70.  
  +28.5%, consistent across rounds, with the atlas arm the stable one (mask  
  6.70/6.70 against the array's 8.70/11.30).  
  shadowMask is where it goes, +53.7%: the receiver does four PCF taps per  
  fragment and sampler2DArrayShadow is evidently off the fast path that  
  sampler2DShadow is on, so the one multiply-add per tap the array removed is far  
  cheaper than the fetch it costs instead. shadowCast pays another 12.8% for  
  re-attaching the target three times a pass where the atlas set a viewport.  
  Reverted. The change is correct and it does lift the size cap - setSize no  
  longer divides GL_MAX_TEXTURE_SIZE by the cascade count, so 4 x 2048 becomes  
  possible - but not for 28% of the GPU frame, for a cap nothing currently hits.  
  Recorded as a per-GPU result: re-measure before assuming it travels to Apple or  
  desktop hardware.  
  Three Phase 4 items are now measured and all three are negative. That is the  
  phase working rather than failing: the list was written from what ES 3.0 offers  
  rather than from what this renderer spends.  
  Refs #140  
  Part of #126  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(renderers): close Phase 4 - three items measured, three negative  
  Phase 4 is closed with nothing shipped (#140). Three of five items were  
  implemented and measured on an Adreno 610 and all three were negative:  
  instancing 0.1 ms CPU / 0.0 ms GPU and already batched, shadow cascades as a  
  texture array +28.5% GPU, label buffer orphaning a no-op. Of the two not  
  measured, packed attributes cannot cut draws - they come from tile x style  
  layer, not the 16-bit index cap - and UBOs are worth ~0.46 ms/frame once the  
  profiler's own per-bucket overhead is subtracted.  
  The list was written from what ES 3.0 offers rather than from what this renderer  
  spends. There are two cameras with two bottlenecks and it addresses neither: the  
  2D city is CPU-bound on 320 geometry draws/frame, and the terrain frame is  
  GPU-bound with 55% of it in the shadow pass, a cost that is casters x cascades  
  and so beyond any texture layout.  
  What replaces it needs no ES 3.0 at all: #144 hoists the per-layer uniforms out  
  of the per-tile loop (~1.5 ms/frame, and the loop is already grouped by style  
  layer), and instancing the shared-mesh per-tile draws targets the tile masks  
  measured at ~2.4 ms CPU - which is where item 1 should have pointed, and is  
  available as an ES 2.0 extension anyway.  
  State plainly what this does and does not say about the migration. Phases 2 and  
  3 stand: they were never justified by frame rate, they are what makes  
  ANGLE-on-Metal and the desktop phase possible. On Android, Phase 2 bought  
  simplicity rather than capability - the bench device already exposed  
  GL_OES_vertex_array_object, GL_EXT_discard_framebuffer, GL_OES_depth_texture and  
  GL_OES_texture_npot, so every capability made core was already reachable through  
  the probes that were deleted. The payoff is in Phases 1 and 5.  
  Closes #140  
  Part of #126  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  ---------

- due to [`c728005`](https://github.com/massif-maps/MassifMaps/commit/c728005aac50def22efdf7a05d09177a2945078c) - repeat a road shield along its road without turning it with the road *(PR [#150](https://github.com/massif-maps/MassifMaps/pull/150) by [@farfromrefug](https://github.com/farfromrefug))*:

  shield-placement defaults to billboard-line-repeat instead of point - see  
  docs/migration.md.

- due to [`4d579e3`](https://github.com/massif-maps/MassifMaps/commit/4d579e3ce3cee26e47c1657a0e01bb4f80ff13ba) - replace the generated Java enums with @IntDef int constants *(PR [#152](https://github.com/massif-maps/MassifMaps/pull/152) by [@farfromrefug](https://github.com/farfromrefug))*:

  Java code that holds a value in an enum-typed variable, or calls valueOf,  
  values, swigValue or swigToEnum, no longer compiles. Call sites passing a constant -  
  PanningMode.PANNING_MODE_STICKY - are unaffected, as are switch case labels. The Android  
  artifact gains an androidx.annotation dependency. See docs/migration.md.  
  Relates to #146.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  ---------

- due to [`eecd5ed`](https://github.com/massif-maps/MassifMaps/commit/eecd5ed237a8458c01bed1687ae57467c6c6a1f1) - the facade API — property table, specs, events, camera, C ABI and generated bindings *(PR [#153](https://github.com/massif-maps/MassifMaps/pull/153) by [@farfromrefug](https://github.com/farfromrefug))*:

  TileLayer gains a Projection attribute, so every binding regenerates.  
  Refs #146  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(api): adopt a layer or source built with the object API  
  registerLayer and registerSource give an existing object an id, and with it properties, methods and  
  events. That is how an app moves to the facade a piece at a time instead of rebuilding its map, and  
  it is what the first device run needed: the demo builds its layers with the object API, so the  
  sugar had nothing to look up and reported layer=null.  
  The CONCRETE class is recovered rather than assumed, so an adopted VectorTileLayer answers to a  
  vector tile layer's properties and not only to Layer's. Every Swig-wrapped class already registers  
  its short name in ClassRegistry at static-init and the table keys on that plus massif::, so nothing  
  new has to be maintained. The names are interned because a slot keeps the const char* while  
  GetClassName returns by value.  
  In the sugar: Massif.adopt for a standalone object, and map.adoptLayer(id, index) /  
  map.adoptFirst(id, VectorTileLayer.class) for one already on a map, which is the shape an app  
  migrating actually has.  
  Refs #146  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(api): stop a null string default crashing a sugar getter  
  MassifApi.getString(handle, path, null) throws NullPointerException: null string - Swig's  
  std::string typemap will not take a null Java String, and nil is no better in Objective-C. That is  
  exactly what a nullable getter wants to pass, so e.property("name") on a feature without a name  
  killed the handler thread rather than returning null.  
  Both sugars pass a sentinel containing a NUL and map it back on the way out. No real value can  
  equal it.  
  Only a device run found this: the host suite cannot link Java or Objective-C, and the C++ under it  
  is correct - getString takes a std::string and never sees the null.  
  Also documents the sugar layer, which the previous commit claimed to and did not: the script that  
  wrote that section asserted on a stale anchor and threw before writing, and a successful site build  
  on an unchanged file read as confirmation. The section now covers what it is for, adoption, the  
  Swift and Kotlin interop story, and the four things the device turned up.  
  Refs #146  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(api): keep the Objective-C event listener alive for its subscription  
  An ObjC handler silently never ran. The C++ side keeps the director as a raw pointer, so with no  
  strong reference on the Objective-C side ARC collected the block the moment subscribe: returned -  
  no crash, no warning, nothing in the log, the event simply went nowhere.  
  MSFSubscription holds it now, and drops it on invalidate. Java does not have the problem because  
  MassifApi::on already stores the listener in a map keyed by subscription.  
  Found on the simulator: the attach line logged, the taps logged the demo's own probe, and the sugar  
  handler produced nothing at all.  
  Refs #146  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(api): deliver events on the UI thread, as the sugar always claimed  
  Every handler in the sugar is documented as main-thread and every one of them was running inline on  
  the GL or tile thread that produced the event. The facade said so - one line of  
  "Context: no UI dispatcher set, delivering inline" - and nothing registered one, so the fallback  
  was the only path anyone ever took.  
  MassifApi::setUiDispatcher takes a UiDispatcher director whose post() is called from the producing  
  thread and must reach the UI thread and call drain(). The sugar installs one on first attach: on  
  Android a director posting to the main Looper, on iOS a plain C function calling dispatch_async,  
  which needs no director at all because the sugar is Objective-C++ and can hand the context a  
  function pointer directly.  
  Proof is the thread id in the log: Android moved from 5398/5433 to 5398/5398, iOS from 9889014 to  
  9919040, and the warning is gone on both.  
  A director module needs std_string.i even with no strings in its own API -  
  polymorphic_shared_ptr generates a swigGetClassName returning std::string, and without the typemap  
  it comes back as a pointer and the generated Java does not compile.  
  Refs #146  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(demo): exercise the Objective-C sugar on the simulator  
  -apiSugar true attaches MSFMassifMap, adopts the first vector tile layer and logs both event  
  kinds - the counterpart of Android's --es apiSugar true, so the two can be compared line for line.  
  Adds a per-read projection to an event on both sugars: getPos:projection: reads one position in a  
  different projection from the subscription's. The iOS demo's base projection is EPSG4326, so it  
  asks for EPSG:3857 and gets metres beside the degrees - both conversions in one handler, which is  
  the per-read-wins rule running for real rather than only in a host test.  
  The knob reads DemoCfg, not DemoConfig: the latter only knows keys its own table registers.  
  Refs #146  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(api): do not let an SDK setter that refuses a value kill the process  
  Options::setBaseProjection throws NullArgumentException on null. Context called the generated  
  thunk unguarded, so the exception crossed into Java and took the app with it - no stack, no log,  
  nothing. Found by doing exactly that on a device.  
  It was never specific to one setter. setZoomRange and every other validating accessor had the same  
  hole, and a scripting binding sending a value the SDK will not take would have found it the hard  
  way. Every call into the SDK - getter, setter, object setter, method - is wrapped now, and a  
  refusal is RESULT_REJECTED with the reason logged.  
  RESULT_REJECTED is appended rather than inserted, so the C codes it mirrors keep their numbers.  
  Refs #146  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(api): write object properties, and build a projection from a spec  
  Pointing a property at another object was the last verb missing: a layer's style, a decoder's  
  projection, a cache's inner source. The generator emits an objectSetter for every writable OBJECT  
  row (58) and records the class the property points at; Context checks the value's REGISTERED class  
  against it with isSubclassOf before calling the thunk, which casts from a shared_ptr<void> and  
  would be undefined for the wrong type. A class the table does not know is not a subclass of  
  anything, so the check fails closed. Handle 0 clears the property where the SDK allows it.  
  A projection is buildable from a spec now for the same reason - without a way to build one there  
  was nothing to point baseProjection at. It resolves through the same name registry the per-read  
  projection argument uses, so a plugin's projection is buildable the moment it registers.  
  Reached from both sugars by passing an object to set(), and from the ABI as mm_set_object.  
  Device-verified, including the failure: creating projection:wgs84, writing it into  
  options.baseProjection (result 0), then writing a handle that does not resolve, which the SDK  
  refuses and which used to be fatal (result 13, app alive).  
  The tests needed EPSG3857 in the reduced table to cover subclass acceptance at all - registering  
  under a class the table does not know is exactly the closed-failure case, which is what the first  
  run hit.  
  Refs #146  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(api): address a method through a path  
  set and get walk dotted paths and call did not, so a method on a nested object was unreachable  
  unless the app registered that object under an id of its own - which for a layer's tile decoder  
  means registering something it never asked to name.  
      layer.call("tileDecoder.setStyleParameter", "buildings", "true")  
  Everything before the last dot traverses object properties, the last segment is the method, and the  
  failure modes are the ones the property verbs already give: a scalar on the way is  
  NOT_TRAVERSABLE, a missing segment UNKNOWN_PROPERTY, a null intermediate NULL_OBJECT. callAsync  
  validates the same way before queueing.  
  Found by trying the call the app actually makes and getting UNKNOWN_METHOD.  
  Refs #146  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(api): expose the methods a real app actually calls  
  Picked by counting rather than guessing. The NativeScript app this API is measured against calls  
  setStyleParameter 14 times, getElevation 11, moveToFitBounds 9, loadTile 5, screenToMap 4, then  
  clearTileCaches and refresh - and only getElevation and loadTile were reachable.  
  Adds setStyleParameter, getStyleParameter and getStyleParameters on the vector tile decoder, which  
  is how a live theme switch is done without a full re-decode, plus clearTileCaches on a tile layer  
  and refresh on a layer.  
  moveToFitBounds and screenToMap are camera and view calls, so they went into the sugar -  
  camera().fitBounds(bounds) and map.screenToMap(x, y) - rather than the method table. The object API  
  already has them and the wrapper only has to reach them; fitBounds over the whole view is the  
  overload an app writes.  
  The style-parameter thunks are registered on VectorTileDecoder and downcast to MBVectorTileDecoder  
  themselves, because traversal records the class a property DECLARES rather than the concrete one.  
  That limitation is now written down.  
  Device-verified: tileDecoder.getStyleParameters and clearTileCaches both run through the path form.  
  setStyleParameter itself was NOT exercised against a style that declares parameters - the demo's  
  returns an empty list - so only its dispatch is proven, not its effect.  
  Refs #146  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(api): search a vector tile layer, and read the collection it returns  
  findFeatures was the one capability an app could not rebuild on top of the facade, and it needed a  
  way to read a collection at all. Adds both, plus the three general channels the search turned out  
  to need - none of them specific to search.  
  - getFeature(i) on FeatureCollection, registered again on VectorTileFeatureCollection so a search  
    result keeps layerName and distance. featureCount was already a property, so a caller loops it.  
  - CallArgs::getHandle + Context::getObject(handle, requiredClass): an object argument, resolved  
    against the class chain, so a method handed the wrong handle refuses it instead of casting it.  
  - A "geometry" spec factory from GeoJSON, taking either a JSON string or the document inline. One  
    factory rather than one per shape - the SDK already reads every type from it.  
  - A codec for std::vector<std::string>, which had none: the "layers" filter the real app passes was  
    unreachable, a capability regression. Covers every vector<std::string> attribute, not just that.  
  - A "search" factory for the request and the vectortile service (from a layer, or a source+style).  
  A result now inherits the projection of the object that produced it, for a method addressed  
  directly - an intermediate reached by a path has no handle to read one from. For it to have  
  anything to inherit, VectorTileSearchService gained getProjection() returning its data source's:  
  the recurring pattern where a value a binding would compute is an SDK gap.  
  childOf now takes the class the caller is about to cast to, closing the same hole for the existing  
  layer/source/style references.  
  GeometryMethods.cpp is split out of MethodImpls.cpp so the host tests can link the collection  
  methods without a tile source or a CartoCSS decoder.  
  Device-verified on the Adreno 610 phone through --es apiSearch: 4 hits for "Grenoble" at z14 with  
  positions in lon/lat, 1 hit with --es apiSearch 'Grenoble:place'. Two things that cost a round and  
  are now in the doc: findFeatures blocks hard enough to ANR through call, so the demo runs it on  
  callAsync; and a request with NO geometry searches every tile in the world at its zoom.  
  345 host checks pass. The search services themselves are not in the host tests - linking one pulls  
  in a source and a decoder - so only the phone run covers them.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(api): name the class a traversal actually found, not the one it was declared as  
  layer.tileDecoder resolved as a VectorTileDecoder even when it was an MBVectorTileDecoder, so  
  everything the subclass adds was unreachable by name and the style thunks worked around it with a  
  dynamic_cast. The generator now emits a &typeid(X) table beside the thunks and every object getter  
  resolves through it, so out.cppClass is the runtime class. Property lookup and method lookup both  
  start from the reported class, so both gain the subclass in one change.  
  concreteClass hashes the table on first use and falls back to the declared name for anything it  
  does not know. typeid needs a complete type, so an object property pointing at a class the profile  
  only forward-declares (VectorTileClickInfo.layer without Layer.i) keeps the declared name - 14 of  
  116 getters in the reduced test table, none in the full profile.  
  Not ClassRegistry: it is keyed by the binding's class name, it is populated only by the Swig  
  wrappers so a host or C-ABI build has nothing in it, and it logs an error per unknown class.  
  Consequences:  
  - setStyleParameter/getStyleParameter move to MBVectorTileDecoder, dynamic_cast gone.  
  - getStyleParameters() the METHOD is removed. styleParameters is an %attributeval on the same  
    class, and with the vector<std::string> codec the property covers it - two ways to read one  
    value is worse than one.  
  Device-verified on the Adreno 610. Reading the parameter list through a path, with nothing  
  registered but the layer:  
    apiSet layer:demoBase:tileDecoder.styleParameters  
      json=["_fontscale","building_min_zoom","buildings","contours","lang", ...]  
  and the effect of a write, which the previous round could only prove dispatch for:  
    getStyleParameter ["buildings"] -> 1  
    setStyleParameter ["buildings","0"]  
    getStyleParameter ["buildings"] -> 0  
  348 host checks. The two new ones were confirmed to discriminate by making concreteClass return  
  the declared name and watching them fail.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(api): calculate a route, and read its path without turning it into JSON  
  Routing through the facade, and a demo that drives the SDK's OWN Valhalla service - the demo's  
  existing routing test drives routing-lib, a separate library with its own JNI, so it proved nothing  
  about massif::RoutingService.  
  It needed no new machinery. The object-argument channel, the count-plus-element pattern and  
  callAsync all came from the search slice:  
  - calculateRoute([requestHandle]) on RoutingService, blocking, so the demo runs it on callAsync.  
  - getInstruction(i) on RoutingResult. An instruction is a VALUE type, so the element is copied onto  
    the heap to have a handle; the property thunks only need an address, so a !value_type class reads  
    by name like any other.  
  - setCustomParameter(name, value) on RoutingRequest - free-form JSON is not a property shape.  
  - A "routing" factory: request (points + projection are constructor arguments), valhalla-online,  
    valhalla-offline. profile, customServiceURL and timeout are already attributes.  
  - RoutingResult gained getInstructionCount/getPointCount in the SDK, so the counts are properties.  
  getPoints() returns the path FLAT, x0,y0,x1,y1,..., through the same getDoubles channel  
  getElevations uses. A 9 km cycling route is 562 positions: as JSON that is ~15 KB to build, cross  
  and parse, and per-element it is 562 crossings. StructCodec can encode a vector<MapPos> because a  
  spec's via points need it, but that type is kept OUT of the generator's CODEC_TYPES on purpose, so  
  no property accessor exists and there is exactly one way to read a path.  
  A generator bug this found: %attribute(..., RoutingAction::RoutingAction, Action, getAction) spells  
  the enum unqualified. Swig resolves that from the %import; the generator's test wanted massif::X::X,  
  so it classified as STRUCT and SILENTLY emitted no accessor - the maneuver's action was unreadable  
  and nothing reported it. stripArgMacro now qualifies the bare form. Two SDK properties affected  
  (RoutingInstruction.action, RouteMatchingPoint.type); ENUM 45 -> 47.  
  RoutingMethods.cpp is split out like GeometryMethods.cpp so the host tests link the whole read path  
  - a request, a result and the RoutingService base need only a projection. The tests now compile with  
  _MASSIF_ROUTING_SUPPORT and a stub service, so calculateRoute's argument checks, the instruction  
  copy, the flat path and points-is-not-a-property are all covered. 368 checks, up from 348.  
  Device-verified against the public OSM Valhalla endpoint, --es apiRoute:  
    apiRoute 1032.0 m, 758.63 s, 72 points, 21 instructions, in 681 ms (EPSG:4326)  
       1 action=6 at=2 96.0m street=Cours Lafontaine : Tournez a gauche dans Cours Lafontaine.  
      20 action=1 at=71 0.0m : Vous etes arrive a votre destination.  
       path 72 positions, first=[5.724944,45.187755] last=[5.714818,45.191561]  
  and a 9 km bicycle route: 562 positions read in one crossing.  
  French proves setCustomParameter reached the service; the action constants prove the enum fix.  
  Not covered: matchRoute has no thunk, and valhalla-offline has a factory but no tile database on  
  the demo device, so only valhalla-online was actually run.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(api): reach a class with no instance, and say what is still out of reach  
  A property with no accessor is SILENTLY unreadable, and that is how two real bugs hid: last round's  
  unqualified enum, and PackageInfo.size - a std::uint64_t, a spelling INT_TYPES did not list, so a  
  plain integer classified as a STRUCT and got no thunk. Nothing reported either.  
  So the generator now names what it cannot reach, by type, on every run:  
    39 properties have no accessor:  
      massif::BalloonPopupMargins   12  e.g. massif::BalloonPopupButtonStyle.textMargins  
      massif::ClickInfo              7  e.g. massif::BalloonPopupButtonClickInfo.clickInfo  
      std::vector<massif::MapPos>    7  e.g. massif::MapEnvelope.convexHull  
  That list is the to-do, and a new unreachable type shows up when it is added rather than when  
  someone tries to read it. It immediately surfaced the two below.  
  INT_TYPES gains the fixed-width spellings, so std::uint64_t and friends are integers.  
  STATIC CLASSES. Log's properties are all %staticattribute, so it has no instance - and every verb  
  here is addressed by a handle. The context now gives such a class one at construction, under kind  
  "static" and its short name:  
    int log = MassifApi.findObject("static", "Log");  
    MassifApi.setString(log, "tag", "probe");  
  Derived from the table, not named in code, so a new static class is covered without the facade  
  knowing about it. ALL of a class's properties must be static, not some: a mixed class would hand an  
  instance thunk the sentinel that exists only to be a non-null address. The generated static thunks  
  take no obj at all, and accessible() no longer refuses them.  
  CODEC_TYPES gains MapTile (the tile a click or a feature came from, 4 properties) and the two  
  string-keyed maps (httpHeaders, Layer.metaData). vector<MapPos> keeps its codec functions - a  
  routing spec's via points need them - but stays OUT of CODEC_TYPES, so no accessor is emitted and  
  the flat channel remains the one way to read a path.  
  Value accessors 556 -> 570. 388 host checks, up from 368.  
  Device-verified on the Adreno 610:  
    apiSet static:Log:showDebug 1.0 -> 1.0 (handle=1048577, result=0)  
    apiSet static:Log:tag       json=probe (handle=1048577, result=0)  
    apiSearch  0 id=0 layer=place tile=[8453,10502,14] name=Grenoble  
    apiSet layer:demoBase:metaData  json={"level":3,"tint":"warm"}  
  The last one matters twice: it round-trips a map, and `level` comes back a number rather than "3".  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(api): stop a slow async call blocking an unrelated one  
  callAsync ran on ONE worker, and a device made the cost obvious: a cold findFeatures takes ~20 s,  
  and a route queued behind it waited the whole time for work that shares nothing with it.  
  A free-for-all pool is not the answer either. Five loadTiles on one source would finish in an order  
  the caller cannot predict, and the event carries the RESULT, not the call id, so there is nothing to  
  tell them apart with. So: calls on one object run in order, calls on different objects run in  
  parallel. A worker claims the first queued call whose target has no call running.  
  The pool is grown on demand up to four, and the measure for growing it is DISTINCT TARGETS rather  
  than queued calls - three loads on one source are serialised, so a second worker for them would only  
  idle. That distinction was a bug first: counting busy workers instead spawned nothing, because at  
  submit time the previous call is usually still queued rather than running.  
  The single _runningCall/_runningTarget/_runningCancelled scalars become a vector, which is what  
  cancelCall, cancelCalls, getPendingCallCount and waitForCalls now consult.  
  Device-verified on the Adreno 610, cold caches, started a second apart:  
    22:15:55.629  apiSearch 'Grenoble' at z14 queued as 1  
    22:15:56.798  apiRoute  queued as 2  
    22:15:57.342  apiRoute  1032.0 m, 21 instructions, in 544 ms    (thread 24758)  
    22:16:17.382  apiSearch 'Grenoble' -> 4 in 21753 ms             (thread 24751)  
  The route finished while the search still had 20 s to run.  
  393 host checks. The new test asserts both halves - two objects overlap, one object does not - and  
  waits with a TIMEOUT rather than indefinitely, because a single-worker regression would otherwise  
  hang the suite instead of reporting it. Confirmed to discriminate by pinning MAX_WORKERS to 1 and  
  watching it fail.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(api): generate a spec factory from the class' own constructor  
  A factory was a hand-written branch per class - the one place the facade grew when the SDK did, and  
  the biggest violation of "adding a feature never adds code here". The signature already carries the  
  names, the types and the order, so it is read instead:  
    HTTPTileDataSource(int minZoom, int maxZoom, const std::string& baseURL);  
    !spec(massif::HTTPTileDataSource, source, http, alias(url, baseURL),  
          default(minZoom, 0), default(maxZoom, 24))  
  One line per class in its .i. alias is a NAMING tool, not a compatibility one - it exists so `url`  
  beats `baseURL`, and so `style` covers a parameter two classes spell differently (`decoder` on  
  VectorTileLayer, `tileDecoder` on VectorTileSearchService). default carries what a signature cannot:  
  the 0/24 zoom bounds are a convention, not a C++ default argument.  
  A shared_ptr<X> parameter resolves as a child - an id or an inline spec - and the kind is found by  
  walking the declarations of X's subclasses, so a parameter typed as the base TileDataSource resolves  
  against "source". The longest constructor the spec fully satisfies wins, which makes two overloads  
  reachable that no hand-written factory ever exposed: MBTilesTileDataSource's `scheme` and  
  HillshadeRasterTileLayer's `elevationDecoder`.  
  17 classes over 4 kinds. SpecFactories.cpp 486 -> 326 lines, and everything left is genuinely  
  adaptive: style parses a dir:// prefix and wraps a style set; projection is a name registry;  
  geometry is a GeoJSON reader; a search built FROM A LAYER takes both halves from it; a routing  
  request has a projection by name and a list of positions.  
  The generator reports the overloads it could not build, the same way it reports unreachable  
  properties - a parameter type no kind builds is the next thing to declare, not an error.  
  swigpp-{java,objc,csharp}.py drop !spec lines the way they already drop %attribute: it is facade  
  metadata, not Swig input.  
  NOT COVERED BY THE HOST TESTS: the reduced table has no !spec class and SpecFactories.cpp needs  
  every source, layer and service to link. Verified on the Adreno 610 instead - seven builds across  
  all four kinds, an unknown type answering RESULT_UNKNOWN_TYPE, maxZoom reading back 14 from the spec  
  while minZoom reads 0 from the declared default, and search and routing still working end to end.  
  Moving the value helpers and the generated include into a light TU is the follow-up.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(api)!: build a style project from a spec, and stop flattening the style chain  
  buildStyle collapsed three objects into one spec and supported only half of them: it took a `css`  
  string and a `dir://` prefix, built a CartoCSSStyleSet and wrapped it in a decoder. CompiledStyleSet  
  had NO spec form at all, so a style project - an asset package with several named styles in it - was  
  unreachable, and so was styleName. That is the mode the demo runs as --es style project.  
  Each of the three is its own class with its own constructor, so each is now its own declaration and  
  the generated factory builds them:  
    !spec(massif::DirAssetPackage,  assets,   dir,      alias(path, dirPath))  
    !spec(massif::CartoCSSStyleSet, styleset, cartocss, alias(css, cartoCSS), alias(assets, assetPackage))  
    !spec(massif::CompiledStyleSet, styleset, project,  alias(assets, assetPackage), alias(name, styleName))  
    !spec(massif::MBVectorTileDecoder, style,  mbvt,    alias(cartocss, cartoCSSStyleSet),  
                                                        alias(project,  compiledStyleSet))  
  The decoder's two constructors take differently-named parameters, which is what makes the choice  
  unambiguous: `project` selects one, `cartocss` the other, neither is RESULT_BAD_SPEC rather than a  
  silent default.  
  a "style" spec is now {"type":"mbvt","cartocss":{"type":"cartocss","css":"..."}}  
  rather than {"type":"cartocss","css":"..."}, and an asset package is a nested {"type":"dir","path":  
  "..."} rather than a "dir://" string prefix. The old form supported one of the two style set types;  
  this one supports both and composes.  
  buildStyle is deleted. 21 classes over 7 kinds now build from their constructors, and  
  SpecFactories.cpp is 313 lines - projection, geometry, the search-from-a-layer shortcut and the  
  routing request are all that is left.  
  Device-verified on the Adreno 610 against a real style project pushed to the phone:  
    apiCreate layer:full -> handle=1048583          (a four-level inline spec, one call)  
    apiSet layer:full:tileDecoder.comp…

- due to [`0870eca`](https://github.com/massif-maps/MassifMaps/commit/0870eca705e2493d942f3dd8b4f6058f87882020) - tell an app what moved the map, and end a movement exactly once *(PR [#165](https://github.com/massif-maps/MassifMaps/pull/165) by [@farfromrefug](https://github.com/farfromrefug))*:

  MapEventListener.onMapMoved and onMapStable take a MapMoveReason argument.  
  Every app that overrides them must change the signature; the C++ compiles either way, so a  
  missed override silently stops being called. onMapStable also changes behaviour - it no  
  longer fires for a tap that moved nothing, and no longer repeats while the map sits still.  
  Migration, per language, in docs/migration.md.  
  Verified: host tests pass, including new coverage that the reason is an enum property on  
  the payload, is read-only, and that the three values are distinct. Every touched .cpp  
  syntax-checks clean, the java and objc wrappers regenerate, and the docs build has no  
  broken links. NOT device-verified, and the C# and UWP wrappers were not regenerated.  
  Closes #163  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(api): read the move reason from the facade, and show both event patterns  
  The reason #163 put on the SDK's events now reaches the two facade binding layers, so an  
  app on the facade gets it without dropping to the object API:  
  - MapEvents.Move / MSFMapMoveEvent, carrying reason()/cause and byUser, raised by onMove  
    and onStable. Java compares interned constants, Obj-C an NS_ENUM.  
  - onStable's doc corrected on both: it is the END of a movement, once, not a poll - and  
    onIdle no longer claims that every tile has settled, which was never true.  
  Two examples, each the Android file and its Obj-C twin:  
  - map-events (new): the refresh-on-settle pattern. Counts moves against stables so the  
    difference is visible, and a "fly to" button proves a programmatic move reports  
    animation rather than gesture - the case an app must not refresh for.  
  - feature-click: now uses consumeFeatureClick and RETURNS TRUE once it has found a named  
    feature, which is the half that was missing. A declined click falls through to the map's  
    own onClick, and the example shows both outcomes.  
  Verified: gen-examples.py regenerates cleanly, 9 examples, all 9 ported to iOS. NOT built  
  or run - neither binding layer is compiled by the host tests, so this needs a device pass.  
  map-events still needs its gallery screenshot.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(website): call it Massif Maps, list NativeScript, and point CARTO users at the migration  
  Three homepage corrections:  
  - "New in the Akylas fork" is "New in Massif Maps", and the last two Akylas references in  
    the site description and package.json go with it. The fork is the project now.  
  - NativeScript joins the supported platforms strip. It ships on npm and is built on the  
    facade API, so leaving it off the list understated what is available. It stays out of the  
    capability matrix, whose columns are native targets - it inherits Android's and iOS's.  
  - A third hero button, "Coming from CARTO?", next to Explore Features. The migration page  
    was only reachable from the sidebar, which is the wrong place for the one page a CARTO  
    SDK user needs first.  
  Verified: production build is [SUCCESS] with no broken-links block.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(api): let a subscription ask for a rate, and show it in the map-events example  
  map.moved fires 47 to 159 times a second during a drag. A handler that repositions a view or  
  updates a readout does not need every one, and the only tools were coalescing - which does  
  nothing when the producer and the handler are the same thread, as they are - or a hand-rolled  
  timestamp in every app.  
  Adds a `throttle` window to the facade's subscription options: `{"throttle":250}` on mm_on,  
  `MassifMap.onMove(handler, 250)` in Java, `-onMove:throttle:` in Obj-C.  
  A window on the subscription, not a timer. EventBus::due checks and stamps the entry under the  
  same lock as the lookup, so two threads emitting at once cannot both find themselves due, and  
  events inside the window are DROPPED rather than queued - the payload is freed when the emit  
  returns, so a late delivery would read nothing. The stamp starts at the epoch so the FIRST  
  event always gets through. Refused on a consuming subscription: a dropped click is one the SDK  
  is still waiting on an answer for.  
  The trailing edge is deliberately NOT here - it needs a scheduled wakeup, and the facade has a  
  worker pool but no timer; sleeping a pool worker would block a call slot. The NativeScript  
  plugin keeps its own JavaScript debounce for now, and the gap is written down in  
  docs/internals/map-events.md.  
  The map-events example on both platforms now throttles its move handler to 4 a second and  
  shows the counts side by side, so the difference between "track the movement" and "the  
  movement ended" is visible rather than described.  
  Verified: host tests pass, with new coverage for the first event getting through, a burst  
  inside the window collapsing, delivery resuming after it, an unthrottled handler beside a  
  throttled one still getting everything, and a consuming subscription being refused. Every  
  touched .cpp syntax-checks, both wrapper sets regenerate, docs build clean. The Java and  
  Obj-C binding layers are NOT compiled here and nothing is device-verified.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(api): declare the payload map.moved and map.stable actually carry  
  MapEventBridge started emitting a MapMoveInfo with both events, but the !event macros in  
  Options.i still said they carried nothing. The macro is what the schema is generated from, so  
  docs/api/massif-api.json reported `"payload": null` - and every consumer of that schema  
  believed it: the generated autocompletion, the website's API reference, and the NativeScript  
  plugin's typings, where `e.get('reason')` is rejected because a payload-less event only accepts  
  a dotted path.  
  Declaring it adds massif::MapMoveInfo to the schema with its `reason` property, and flips both  
  events' payload. Nothing else in the schema moves.  
  Regenerated with the build's own define set rather than a profile name - `--defines` REPLACES  
  the profile's defines rather than adding to them, and getting that wrong silently drops every  
  class behind a define that was not repeated (2444 lines of schema, on the first attempt).  
  Verified: the schema diff is exactly the new class and the two payload lines. Host tests were  
  NOT re-run here - the worktree this was regenerated in has no submodules, and no C++ changed.  
  SWIG wrappers were not regenerated for this one-line .i change.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(api): list MapMoveReason.h in the .i, so the enum's constants reach the schema  
  The table generator reads an enum's constants out of the headers a module NAMES in its `%{ %}`  
  block, and does not follow includes. MapMoveInfo.h includes MapMoveReason.h, so the C++ was  
  fine and the property knew its enum type - but the constants were absent from the schema's  
  `enums` map, and everything generated from it degraded the property to a plain number.  
  Concretely: the NativeScript typings emitted `readonly 'reason': number` where every other enum  
  property gets a union of its constant names, so `e.get('reason') === 'gesture'` did not compile.  
  Naming the header adds the three constants with their doc text.  
  Verified: the schema now carries massif::MapMoveReason::MapMoveReason with all three members,  
  and the diff is confined to that. Host tests not re-run - the worktree has no submodules and no  
  C++ changed.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  ---------

- due to [`1ea2116`](https://github.com/massif-maps/MassifMaps/commit/1ea2116eb4be5e47224a985cc2295d963cad3f5c) - light the sky by scattering, and fog the whole frame from one model *(PR [#174](https://github.com/massif-maps/MassifMaps/pull/174) by [@farfromrefug](https://github.com/farfromrefug))*:

  FogOptions.horizonAngle is removed - the shared horizon term is terrain-correct  
  without it. horizonBlend keeps its 0..1 range but is now mapbox's exp(-3 (sin/blend)^2) and the  
  distance ramp is (1 - exp(-6t))^3, so both need retuning rather than converting. SkyOptions defaults  
  to SKY_TYPE_ATMOSPHERE; set SKY_TYPE_GRADIENT for the previous two-colour ramp. FogOptions,  
  SkyOptions and TerrainOptions.surfaceShaderSource all changed their shader contract - a custom fog  
  shader defines three entry points, a custom sky or surface shader must no longer fog itself. See  
  docs/migration.md.  
  Requires libs-massif 49a7e9f (vt: the shared fog block; cartocss: the new Map-block properties).  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(api): apply an indexed key in a nested spec instead of dropping it  
  A spec's leftover keys are applied by two different functions: Spec::create's own loop, which calls  
  Context::setProperty and is therefore path-aware, and applySpecProperties, which every spec built as  
  a CHILD goes through - a layer's source, terrain's source, a style's project. The second matched a  
  property by its full name only, so "metaData.dem_encoding" was accepted at the top level and dropped  
  one level down, with a warning in the log and nothing else.  
  That is how an example's DEM source lost its encoding: the terrain fell back to the MapBox decoder  
  on terrarium tiles, which puts the mesh hundreds of kilometres up and the camera inside it - the map  
  rendered as a cylinder seen from below. A log line is not enough for a failure that shape.  
  applySpecProperties now splits at the first dot and uses the indexed setter, the way Context::lookup  
  already does. An unknown prefix is still reported rather than consumed.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(agents): move the tests, docs and example rules into skills  
  .claude/CLAUDE.md was 251 lines and growing: it restated the documentation guidance the document  
  skill already carried, and held a Tests section and an example-screenshot section that only matter  
  while doing those specific jobs. All three are now skills, reached by one line each, and the file is  
  165 lines of what an agent needs on EVERY task.  
  New add-example skill: an example is one id on Android, iOS and NativeScript plus two generators, so  
  adding it to one platform is a half-change. Its reference carries the file layout, each platform's  
  host API side by side, the spec traps that have already cost a round, the caching rule for remote  
  sources and the screenshot rules.  
  New test skill, and the documentation home table moves into the document skill's own reference  
  rather than living in two places.  
  The repo table gains integrations/nativescript - it is a third nested repo, it starts on master, and  
  it needs branching like the other two.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(demo): sky and fog knobs, an atmosphere example, and a cache on every remote source  
  The bench gains the new knobs on both platforms - skyType, skyQuality, skyAtmoSun, skyAtmoColor,  
  skyAtmoHalo, skyAtmoLum, fogVertStart, fogVertEnd - as launch extras, panel controls and DemoLive  
  keys, and loses fogHorizon with the option behind it. Both demos' custom sky shaders and both relief  
  surface shaders stop fogging themselves: the SDK now applies the frame's own haze to whatever they  
  return, which is what lets one custom fog shader cover the whole frame.  
  New atmosphere example on all three platforms: a physical sky over 3D terrain, dawn/noon/dusk/night,  
  stars, peaks standing clear of a valley haze, and a "Comets & clouds" toggle that installs a custom  
  skyColor shader over the SDK's own scattering. One applySky() owns both sky toggles because the  
  custom shader calls atmosphere(), which only exists under SKY_TYPE_ATMOSPHERE - letting the toggles  
  set the type separately would reach GRADIENT with the shader still attached, and a shader naming an  
  undeclared function falls back silently.  
  Every remote source in every example now goes through a persistent cache, reached by a new  
  host.cachePath(). These are other people's free services and a demo that gets panned around was  
  re-fetching the same tiles on every run. The DEM's dem_encoding moves to the whole-map form, which  
  both spec paths apply - the dotted form was dropped inside a nested spec until the previous commit.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  ---------

- due to [`4be100a`](https://github.com/massif-maps/MassifMaps/commit/4be100ae730883d9fd8743a2cbb7f351fb62bdc1) - reach the horizon when zoomed out, and render flat when 3D buys nothing *(PR [#176](https://github.com/massif-maps/MassifMaps/pull/176) by [@farfromrefug](https://github.com/farfromrefug))*:

  3D terrain now renders FLAT when zoomed out past ~2 px of parallax or tilted past  
  88 degrees. Set TerrainOptions.AutoFlattenParallax and AutoFlattenTilt to 0 for the old behaviour.  
  TerrainOptions.ViewDistance no longer shortens the view - it is a minimum on the factor rule rather  
  than a replacement for it, so an app that set it to keep the tile count down at low zoom must use  
  ViewDistanceFactor instead. TerrainOptions.Exaggeration now reads back the app's own value rather  
  than the elevation manager's, which auto-flattening scales.  
  Closes #156  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * chore(api): regenerate the facade bindings for the auto-flatten properties  
  gen-api-bindings.sh writes the schema, the TypeScript typings and the three constant sets from  
  all/modules; they are committed and CI checks them with --check, so the new TerrainOptions  
  attributes had to reach them. No hand edits.  
  Refs #156  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  ---------

- due to [`aafc81b`](https://github.com/massif-maps/MassifMaps/commit/aafc81b9b2b37c2a9b2506439e623c92159ad577) - switch a map between 2D and 3D as one state, and let the app lead it *(PR [#185](https://github.com/massif-maps/MassifMaps/pull/185) by [@farfromrefug](https://github.com/farfromrefug))*:

  TerrainOptions.Flattened is now writable and reports what was ASKED for  
  rather than what the ramp has reached; read isActive() for the instantaneous state. New  
  FlattenMode, FlattenRatio, Switching and AutoFlattenRiseDuration properties.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(demo): show every way of driving the 2D/3D switch, and stop the world-map flash  
  ExampleActivity attaches the map in onCreate and runs the example on a WORKER thread, so  
  the default camera - the whole world at zoom 0 - drew until the example got past building  
  its layers and framed the camera. Covered until the spinner goes. iOS and NativeScript  
  start their examples synchronously and never had it.  
  The host gains a slider, in its own non-scrolling row: a HorizontalScrollView on Android  
  and a UIScrollView on iOS both claim the pan gesture, so a slider inside one cannot be  
  dragged at all.  
  The example reads `flattened` back from the SDK instead of counting button presses - with  
  auto by tilt on the RULE owns the state, and a local flag drifted out of step until the  
  button flew to the tilt the map was already at and nothing moved.  
  --es fullSwitch true on the bench, live-reconfigurable through DemoLive.  
  Requires integrations/nativescript e2c822dc (the same example and the same host slider,  
  plus the regenerated typings).  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  ---------

- due to [`5698945`](https://github.com/massif-maps/MassifMaps/commit/56989451ed4a7cf9fdc8c607ba7b5bc18c841f76) - keep a plate and a halo at their own colour while a label fades in *(PR [#199](https://github.com/massif-maps/MassifMaps/pull/199) by [@farfromrefug](https://github.com/farfromrefug))*:

  a halo is an outline, not a backdrop. A style that set a transparent fill and an  
  opaque halo drew a solid blob and now draws a hollow outline.

- due to [`bba34ac`](https://github.com/massif-maps/MassifMaps/commit/bba34aceef53dc48028fe16377a35fec0468529c) - draw a 3D bridge deck as one clean piece, and keep a city of bridges smooth to pan and zoom *(PR [#207](https://github.com/massif-maps/MassifMaps/pull/207) by [@farfromrefug](https://github.com/farfromrefug))*:

  TerrainOptions::CameraClearance is now an optional floor in  
  metres under the zoom-relative rule, default 0 (was 60). An app that wants the  
  old fixed minimum sets it explicitly.  
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>  
  * docs(terrain): log what the reference tiles, the label anchoring and a fast zoom measured  
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>  
  ---------

- due to [`5fa686e`](https://github.com/massif-maps/MassifMaps/commit/5fa686e8803c30e2e6059b4947bf5f7a2cef6aeb) - build the iOS SDK again — two adopt() overloads made one Objective-C selector *(PR [#216](https://github.com/massif-maps/MassifMaps/pull/216) by [@farfromrefug](https://github.com/farfromrefug))*:

  on iOS, MassifInterop.adopt(kind, objectId, VectorDataSource) is now  
  +adopt:objectId:vectorSource: instead of +adopt:objectId:source:. Rename the keyword at  
  the call site. The overload was added in #169 and has never built on iOS, so no shipped  
  app can be calling it.

- due to [`d9facec`](https://github.com/massif-maps/MassifMaps/commit/d9facecb8e4f15311d6bab74c55a2202c470816b) - 3D bridges, opt-in via TerrainOptions.bridges3DEnabled, with decks that stay on their road *(PR [#231](https://github.com/massif-maps/MassifMaps/pull/231) by [@farfromrefug](https://github.com/farfromrefug))*:

  span features no longer lift onto their chord unless  
  TerrainOptions.bridges3DEnabled (terrain.bridges3DEnabled in the facade) is  
  set to true.  
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>  
  * docs(terrain): a feature page and a renderer page for 3D bridges  
  The span chapter leaves the terrain page for its own, with a code map, an  
  honest word on how modular it is (the geometry rules are; the resolver inside  
  GLTileRenderer is not) and the list of what is still wrong. The feature page  
  says what a style has to say, what the option costs on and off, and the limits.  
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>  
  * test(vt): drive the span resolver on the host with a fake ground  
  A span line built through the real TileLayerBuilder, a ground that answers by  
  position with a spike under the middle of the deck, and the bases read back:  
  off by default and free (no read, no chord, no end to fetch); on, the chord  
  between the two portal heights and the middle vertex on it, not on the spike;  
  a tile-cut piece reports its end and resolves nothing; off again forgets all.  
  Requires libs-massif 81f800e (vt: SpanResolver extracted from GLTileRenderer).  
  The renderer page's code map now names the module.  
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>  
  ---------

- due to [`b38fd6d`](https://github.com/massif-maps/MassifMaps/commit/b38fd6d0530128604927f108b2b0e091cf103e84) - fold the vt/cartocss libs into the repo, so a renderer change is one PR again *(PR [#232](https://github.com/massif-maps/MassifMaps/pull/232) by [@farfromrefug](https://github.com/farfromrefug))*:

  fold the vt/cartocss libs into the repo, so a renderer change is one PR again (#232)

- due to [`1de8826`](https://github.com/massif-maps/MassifMaps/commit/1de88262e26126132c79966b53d4985055584047) - keep buildings on a coarsened tile, and stop the map redrawing at rest *(PR [#238](https://github.com/massif-maps/MassifMaps/pull/238) by [@farfromrefug](https://github.com/farfromrefug))*:

  Options::TileLODForeshorteningLimit is removed, superseded by  
  TileLODMaxZoomLevelsOnScreen. It bounded the same term from the other side with a number that had  
  no reference behind it. Apps setting it through the facade API get a runtime "unknown property"  
  throw, not a compile error.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(renderers): stop the drape baker asking for a frame forever on a still map  
  The redraw condition compared queue SIZES against budgets the loops had not used: at rest the stale  
  loop bakes DRAPE_BAKE_BUDGET_BLANK tiles but the test was against DRAPE_BAKE_BUDGET_STALE, so 2-8  
  stale tiles were baked and another frame asked for anyway - every frame, with the map standing  
  still. In one log 300 of 300 frames came from that single line, and nothing else asked for any of  
  them. The wall-clock budget being cut short was not checked at all, and the restack queue could be  
  cut short with nothing asking for the frame that would finish it.  
  Ask only when a loop actually ran out of budget with tiles still queued, through one bakeSome()  
  helper, so the condition cannot drift from the budget again. The drape cost log carries the queue  
  sizes and whether anything is left, so this is self-diagnosing next time.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  ---------

- due to [`d74bcab`](https://github.com/massif-maps/MassifMaps/commit/d74bcabd1350223fb2900b7a9a496fed1561dcb5) - spell a colour the same way everywhere, and refuse a bad one *(PR [#239](https://github.com/massif-maps/MassifMaps/pull/239) by [@farfromrefug](https://github.com/farfromrefug))*:

  a COLOR property written from an unparseable string is now REFUSED rather  
  than silently written as 0. An app relying on that (a transparent colour from a typo) keeps  
  whatever the property held instead.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(api)!: read a facade colour as "#rrggbbaa", the order a style sheet uses  
  The facade's eight-digit hex was "#aarrggbb", the REVERSE of what mvt::parseCSSColor gives  
  every CartoCSS style sheet - so "#b8c6d880" meant two different colours inside one SDK, and  
  the fog colour a style spells was not the fog colour the facade read.  
  decodeColor and encodeColor now use CSS order, and take the "#rgba" form parseCSSColor  
  already did. A NUMBER stays ARGB: that is what Color is built from and what getARGB reads  
  back. The iOS and Android demo helpers parsed eight digits Android-style and follow.  
  an eight-digit hex colour swaps meaning - "#ffb8c6d8" was opaque slate and  
  is now b8c6d8 at alpha d8. A saved dayCycleLightStops curve written by an older SDK reads  
  wrong, and the value the SDK writes back changes. Six- and three-digit forms and ARGB  
  numbers are unaffected.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(vt): refuse a style colour with a bad hex digit instead of drawing it black  
  parseCSSColor read each pair of digits with istringstream >> std::hex and checked bad(). A  
  non-hex character sets failbit, not badbit, and a failed extraction leaves the target at 0 -  
  so "#gg0000" parsed as black and rendered, instead of being refused as the typo it is.  
  " 0ff00" and "-10000" got through the same way, because >> skips whitespace and signs.  
  Digit by digit now, the same check StructCodec::decodeColor uses.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  ---------

- due to [`28083d2`](https://github.com/massif-maps/MassifMaps/commit/28083d251fc984a0c2845edaf7b3b27f77b22115) - ship Massif Streets, a MapLibre style the SDK draws the same way *(PR [#249](https://github.com/massif-maps/MassifMaps/pull/249) by [@farfromrefug](https://github.com/farfromrefug))*:

  a style's sizes no longer scale with Options::TileDrawSize. An app on the default  
  256 is unaffected; one that sets another size and relied on the magnification must scale its style  
  or its DPI to match.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(labels): stop shield-min-distance thinning shields of different roads  
  Every shield went into one culler group, so the property meant "no two shields anywhere closer than  
  this" rather than "this shield does not repeat closer than this". A style setting it to its own  
  shield-spacing - what mapbox2css writes, so that a road cut into many short ways is not shielded on  
  each one - kept a handful of shields and dropped the rest.  
  Grouped by the label's text along a line, which is what TextSymbolizer already does with its  
  repeats; a point shield still shares one group as before. The unstated case gets the same floor as  
  text, the label's own size, so a repeat cannot stack on itself where two tiles cut one road.  
  Checked in the preview grid at zoom 11 over Grenoble, one camera: the massif row draws N 481, A 480,  
  N 87 and A 41 against the maplibre row's same set, where it drew none before.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(tools): keep a road style's own draw order through mapbox2css  
  line-sort-key was dropped, so a converted style drew its roads in the order the TILE lists them: a  
  residential road painted over the motorway it crosses wherever the tile carried it later, which  
  maplibre never shows because it honours the key natively.  
  A CartoCSS rule cannot reorder its own features, so the key becomes rule ORDER - one attachment per  
  value, emitted lowest first, so the highest is drawn last. Only a match/case over the feature with  
  numeric outcomes expands; anything else is reported as approximated and keeps tile order.  
  Massif Streets converts at 88% coverage where it was 79%, its two road layers becoming 7 rules each,  
  motorway last.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(tools): tell mapbox2css the tile size the style will be drawn at  
  Every zoom stop and zoom predicate carried a hardcoded shift of one level, on the strength of the  
  SDK's default 256-pixel tile. An app on maplibre's 512 already numbers its zoom the way maplibre  
  does, so the shift ran the whole style a level behind: a trunk casing drew 5.2 px where maplibre  
  gave 7.6 at the same camera, which reads as roads that are simply too thin.  
  --tile-draw-size makes the shift log2(512 / size). It defaults to 256, so a conversion that passes  
  nothing is byte-identical to before; a style meant for an app that sets Options::TileDrawSize has  
  to say so. The preview grid converts with 512, which is what puts the massif row's road widths on  
  the maplibre row's at zoom 14.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(vt): draw a group of line casings under all of their fills  
  maplibre's line-border draws a casing from the line's own buffer, one draw before its fill - which  
  is the layer order a mapbox casing gives only as long as the whole road is ONE rule. It is not:  
  mapbox2css --fold-casings puts the casing in the fill rule, and the line-sort-key expansion then  
  makes seven of them, so a motorway's casing drew over the trunk fill beside it.  
  Rules naming the same line-border-group have every casing drawn before any of their fills. The  
  group is hashed at decode, splits the geometry batch like comp op does, and the renderer walks the  
  tile's layers once per group, at most MAX_BORDER_GROUPS of them.  
  Nothing changes for a style that names no group: borderGroup 0 takes the single-pass path it took  
  before, the pass argument defaults to ALL at every existing call site, and the only added work is  
  one int compare per geometry, beside the comp op the loop already reads.  
  Only the 2D pass groups. The shadow caster and the ground-AO bake draw silhouettes, where casing  
  order cannot show.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * Revert "feat(vt): draw a group of line casings under all of their fills"  
  This reverts commit d11a9cc3fa2591684cbf02d7dc6ee02228164291.  
  * fix(tools): stop a folded casing drawing over the road beside it  
  --fold-casings puts the casing in the fill rule, which is right while the road is ONE rule: the  
  renderer draws the border from the same buffer, one draw before the fill. The line-sort-key  
  expansion makes seven rules of it, and each then drew its own casing over the fill of the road  
  beside it - the one thing the casing LAYER it replaced never did, and the last visible difference  
  between a converted Massif Streets and the maplibre render of its source.  
  The fold now skips a pair whose fill states a sort key, and says so. Those roads convert as casing  
  rules followed by fill rules: every casing before every fill. It costs the second pass over the  
  road geometry that folding exists to save - +0.64 ms of the layers section on the Crosscall.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(web): let the preview turn the tile cross-fade off  
  A cased road fades in badly. Its fill is near the background colour, so while the tile blends only  
  the casing reads and the road looks like an outline waiting to be filled - the casing and the fill  
  are in lockstep, it is the colours that make the fade look staged rather than the timing.  
  ?blendspeed=, forwarded by the preview grid, defaulting to 1 - VectorTileLayer's own default.  
  0 disables blending, which is what maplibre does with vector geometry: it fades rasters only.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(layers)!: let a vector tile's geometry appear instead of fading in  
  maplibre and mapbox-gl fade rasters only; vector geometry appears. A cased road cross-fades badly  
  because its fill is usually near the background colour - for the length of the fade only the casing  
  reads and the road looks like an outline waiting to be filled. The casing and the fill blend in  
  lockstep, so it is the colours that stage it, not the timing.  
  Options::LayerBlendingSpeed already carried this: 0 disables blending, which is what TorqueTileLayer  
  has always done. The default moves from 1 to 0. Set 1 back for the fade this SDK used to do.  
  a vector tile layer no longer cross-fades its tiles in by default. An app that  
  wants the fade sets VectorTileLayer::setLayerBlendingSpeed(1). Raster layers are untouched - they  
  keep their own TileBlendingSpeed.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(tools): let a style carry its own fonts  
  The decoder already registers a style's fonts ahead of the system ones and finds them by scanning  
  <style>/fonts/. Nothing put them there: a converted style naming 'Noto Sans Bold' fell back to  
  whatever the build happened to carry, and on the web - which has no system fonts at all - that meant  
  shields drew regular where mapbox draws them bold.  
  --fonts DIR copies the faces into the project and names them in project.json. The decoder needs no  
  list; the list is for whoever carries the project, and the preview reads it to know what to fetch,  
  a project served over HTTP not being listable. The face is fetched with the rest of the project, so  
  it is preloaded nowhere and costs nothing to a build that does not use the style.  
  Massif Streets carries Noto Sans Bold subset to printable ASCII: 132 glyphs, 14 KB against the full  
  face's 569. Its name table is kept - a face is resolved by the name inside it, and a subset without  
  one stops answering to what the style asks for.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(tools): stop a shield plate's border measuring a fifth thin  
  describeFlatPlate reads the border as the run of border-coloured texels the middle row crosses,  
  starting at the OPAQUE box. A stroke is antialiased against nothing on its outer side, so its last  
  texels fall under FLAT_ALPHA, the box starts inside them, and the run is short by up to one: the  
  1.3 px stroke of Massif Streets' plate spans 2.75 texels at @2x - one at alpha 192, two solid - and  
  measured 2. Against maplibre, which draws that same sprite, the border came out visibly thin.  
  Alpha there IS coverage, so it is added back rather than rounded away: the plate now converts at  
  1.38, the width the raster actually carries. The inner edge needs no equivalent - a stroke meets  
  the fill it covers at full alpha and the colour step is sharp.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(tools): give a road shield the colour its country gives it  
  A style picks a shield's plate from the first letter of the ref - A is an autoroute, D a  
  departmental road - as a match over a slice. `==` and `in` already read a prefix as the regex  
  CartoCSS can take, `match` did not: it threw on its own input, so every branch of Massif Streets'  
  per-country colour table was dropped and every shield in every country drew on the neutral plate.  
  match takes the same path now, one regex per label. `upcase` folds onto the whole string instead of  
  the slice, which says the same thing about a prefix and leaves a shape the regex can take, and a  
  label that cannot be a prefix of that length becomes `false` rather than a branch that never fires.  
  The style converts at 109/123 where it was 108, with the colour table intact: a French A road and N  
  road red, a D and an M yellow, and so on per iso_a2.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(tools): stop a filter over a missing field passing everything  
  CartoCSSParser puts ?? in term0, with && and ||, and the comparisons in term1 - so ?? binds LOOSER  
  than they do. `[x] ?? '' = 'y'` therefore parses as `[x] ?? ('' = 'y')`, the coalesce of a field  
  with a boolean, which is truthy for any feature carrying the field at all. Every filter the  
  converter wrote over a possibly-absent field passed everything, and said nothing about it.  
  Two things it broke in Massif Streets, both looking like style bugs: motorway exits drew as road  
  shields, because the guard excluding subclass 'junction' was true for junctions; and on a source  
  that carries `network` - openfreemap does, our own tiles do not - `!([network] ?? false || ...)`  
  was false for every road with one, so no road shield drew at all.  
  A coalesce is parenthesised WHOLE now, not per operand.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(styles): keep a motorway exit off the road shields, and give it its own  
  A ref is not always a road number. openfreemap carries motorway exits in transportation_name as  
  subclass 'junction' with ref '3b', and both sources carry cycle routes as class 'path' with ref  
  'VV5' - so a shield layer filtering on ref alone drew an exit number and a cycle route as though  
  they were road shields, and at every zoom the roads themselves were drawn at.  
  The road shields exclude both. Exits get a layer of their own at zoom 15, which is mapbox  
  Standard's arrangement: an exit number means nothing until its junction is on screen.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(styles): bring road shields in by class, as their roads earn them  
  A tertiary ref drew its shield at zoom 12, a level before maplibre places it from the same tile:  
  both sources carry the road there, but the converter drops symbol-avoid-edges, so a stub of road  
  that maplibre refuses to label gets one here.  
  The shields come in by class instead - motorway and trunk from the layer's own zoom, primary and  
  secondary at 11, everything else at 13. Written as a zoom test in the FILTER, which the decoder  
  evaluates per TILE (ExpressionContext answers view::zoom with the tile's zoom + 0.5 where there is  
  no view state), so it gates the same way maplibre's filter does rather than per frame.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(styles): hold a secondary road's shield back to zoom 13  
  The class gate let secondary through from 11, which still drew a shield at 12 for a road that is  
  not one at the zoom you would look it up at: OpenMapTiles PROMOTES a class as the zoom drops, so  
  D 106B is minor in the z14 tile and secondary in the z12 one.  
  Which also makes gating secondary nearly free - by z12 everything the tileset still carries has  
  been promoted into secondary or better - so it moves to 13 with the rest. Zoom 12 keeps motorway  
  and trunk, as maplibre draws it from the same tile.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(tools): stop a converted rule testing what its filter already proved  
  A CartoCSS when() is a WhenPredicate, and PredicateContainsChecker returns indeterminate for one  
  against anything (cartocss PredicateUtils.h): nothing prunes, and the decoder evaluates the whole  
  expression per feature. The converter emitted them freely - a line-sort-key expansion gave every  
  road branch the layer's whole class list, a negation per earlier branch, and a paint chain  
  re-testing the class it had just been split on.  
  narrow.ts reads what a layer's own filter proves and restates the filter and its values against it.  
  A closed set minus its exclusions is an equality, so the fallback branch pins itself; the set test  
  and the negations then drop, and match/case in a value take their branch. Only equalities may  
  retire a clause - the exclusions were read off those clauses, so letting them judge had every  
  [subclass != 'junction'] prove itself and vanish.  
  Two more: a NEGATED set test is a conjunction, so it brackets one test per value; and a positive  
  one splits into an attachment per value when the paint branches on that field AND the rest of the  
  filter brackets. Without that second gate, splitting copies the rest into every attachment and one  
  when() comes back N times - topo-v4 went 142 to 239.  
  when() before -> after: mapbox-standard 221 -> 146, maptiler-openstreetmap 119 -> 52, streets-v4  
  220 -> 204, outdoor-v4 152 -> 143, topo-v4 142 -> 134, ofm-liberty 73 -> 73.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(styles): let every road shield rule bracket, instead of testing each road per feature  
  Massif Streets converted to one when() per shield layer for two reasons of its own. The coalesce  
  null guards were unnecessary - a missing field already compares unequal, in maplibre and in  
  mapnikvt alike (Predicate.cpp, MismatchResult) - and only stopped the test bracketing. The zoom  
  gate was an `any` of zoom-and-class branches, which no filter can bracket and every feature pays  
  at every zoom.  
  The guards go, in filters only; in a value they still earn their place. The gate becomes three  
  layers with real minzooms - motorway and trunk at 9, primary at 11, the rest at 13 - each  
  excluding what an earlier band drew, so no shield is placed twice. minzoom is a predicate the  
  compiler decides per tile, so at z9 the later bands do not exist.  
  Converts at 1 when() where it was 20; same picture at z11.2, z13 and z14 in the 2x2 preview.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(styles): say what Massif Streets still owes, not what it owed three fixes ago  
  The "Owed" list named the country plate colours, the plate padding and the bold face as broken.  
  All three have landed since: the colour table converts (bbc294e9f), icon-text-fit-padding's  
  [1,3,1,3] arrives as text-background-padding-x 3 / -y 1, and the style carries its own subset  
  NotoSans-Bold (f47868137). The cycleway example was fixed too - a class 'path' ref no longer draws  
  a road shield at all.  
  What is actually left: iso_a2 on transportation_name, which neither tileset carries, so every plate  
  still draws neutral; icon-text-fit on the two US shields, which are real artwork rather than  
  generated plates, so a long ref overruns the sprite; symbol-avoid-edges and text-max-angle.  
  The shield-density line is reworded rather than restated: "three where MapLibre draws six" predates  
  the culler grouping per ref (f4c3132ae), and the preview panes are framed differently, so the  
  number has not been re-counted.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(tools): draw a road again when its filter names its geometry type  
  mapnik::geometry_type is a long long (mapnikvt ExpressionContext.cpp), so comparing it against  
  'LineString' is a type mismatch, which EQ answers false for (Predicate.cpp, ComparisonOperator).  
  A bracketed test always mapped the name to its code; the or-chain a set test fell back to did not,  
  so when(([mapnik::geometry_type] = 'LineString' || ... = 'Polygon')) was false for every feature  
  and the rule never drew. 20 rules in OpenFreeMap Liberty and 11 in MapTiler streets-v4 - which is  
  why Liberty's minor roads rendered as a dark casing with no fill at all.  
  setTest recognises a set test in every spelling a style writes one in - the boolean match, the same  
  with its operands reversed, and ["in", input, ["literal", labels]] - and runs the labels through  
  the constant translation a bracketed test uses. Two things follow beyond the fix: several labels  
  can name ONE constant, both geometry names being type 2, and collapsed to one the test brackets;  
  and a reversed match is a conjunction, so it brackets one != per value where it used to be a  
  `? false : true` ternary evaluated per feature. `["!", ["has", f]]` brackets as !has too.  
  when() before -> after: mapbox-standard 221 -> 135, maptiler-openstreetmap 119 -> 52, streets-v4  
  220 -> 191, outdoor-v4 152 -> 133, topo-v4 142 -> 128, ofm-liberty 73 -> 28. 927 -> 667 in total,  
  and all 66 dead geometry comparisons gone. Massif Streets converts byte-identically - it has no  
  geometry filter - and Liberty's roads were checked A/B in the preview at Grenoble z15.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(renderers): give a converted style's buildings the facades its author saw  
  A plain converted style - OpenFreeMap Liberty, MapTiler's, anything with no `lights` block - drew  
  its extrusions with white roofs and near-flat walls where the browser draws grey roofs and two  
  clearly different greys per block. Two causes, one on top of the other.  
  The roofs first. mapbox's model wants the two building intensities to PARTITION the light -  
  Standard asks for 0.8 ambient over 0.2 direct - and ResolvedLighting says as much, defaulting both  
  to 0.5. But buildingLightIntensity was then overwritten with sunIntensity unconditionally, and  
  LightOptions' own default for that is a full 1.0: the pair summed to 1.5, and pow(1.5, 1/2.2) = 1.2  
  clamped every sunlit roof to white. Only a STATED sun carries now, which LightOptions tracks so an  
  app driving setSunIntensity (DemoSky does) still couples as before.  
  The walls needed maplibre's model, not a better default. It floors its directional term at  
  1 - intensity whichever way a wall faces and multiplies every wall by its gradient floor, landing  
  walls at 42-63% of the roof; mapbox's gives an away-facing wall the ambient alone and cannot reach  
  below 74% without an ambient+direct pair that blows the roof out at some other sun altitude. So  
  resolveLighting picks maplibre's model when nothing states a light, and applyLighting3D takes that  
  branch - fill_extrusion.vertex.glsl, BSD-3, with their own defaults from light_impl.hpp: spherical  
  (1.15, 210, 30), intensity 0.5, gradient on, anchor VIEWPORT, so the shading turns with the bearing.  
  Not carried: the gradient is its clamp floor rather than the ramp, which is exact below ~106 m and  
  a touch flat above it, and the map's shadow still multiplies the result where maplibre has none.  
  The moment anything states a light, mapbox's model runs as before - a converted Standard sets  
  building-ambient and building-light-intensity in its Map block and the day cycle sets both from its  
  curve, so neither reaches the new branch.  
  Verified in the web preview against maplibre over the same tiles at Grenoble z17.6 tilt 60, bearing  
  0 and 130. NOT verified: the day-cycle example itself, which is an Android device check; the  
  argument above is a code-path one. No host test either - resolveLighting is past what the host link  
  reaches (TerrainOptions pulls in ElevationManager), which is why DayCycleLightTest already says so.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(tools): let a 3D building keep the opacity its style asked for  
  fill-extrusion-opacity was forced to 1, for a reason that was real: the 3D pass draws with blending  
  off, and MapTiler's 0.4 turned a city into a wash of half-buildings showing through each other. But  
  forcing it also threw away what a style MEANT by it - maplibre draws OpenFreeMap Liberty's buildings  
  at 0.8, blending a fifth of the pale background back through every wall, which is a good part of why  
  ours read darker than the browser's.  
  It becomes `building-fill-opacity: [param::building_opacity]` with the style's own value as the  
  default, so an app can take it back to 1 as a redraw rather than a re-decode. A ramped opacity still  
  flattens to one number: Standard fades an extrusion in by ramping it alongside the height, and the  
  shadow map is drawn from the building's FULL cast whatever its alpha, so a half-transparent wall  
  shows the shadow it is itself casting. Standard's ramp ends at 1, so a converted Standard is opaque  
  exactly as before - the day-cycle example is untouched.  
  One parameter covers every extrusion in a style. A style asking for two different alphas keeps the  
  first and the coverage report says which; no source style does that today.  
  Liberty converts at 0.8 and its walls lighten to match maplibre's at Grenoble z17.6 tilt 60, with no  
  sign of the wash at that alpha.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(styles): carry an italic face, so a POI label can be one  
  Massif Streets shipped only its bold, which is all a road shield's ref needs. A POI label is  
  italic in every style this one is compared against, and a build with no system fonts - the web one -  
  can reach a face only through the style, so an italic layer fell back to the regular the way the  
  shields did before the bold shipped.  
  Subset wider than the bold: names carry accents where refs do not, so it is cut to Latin-1 and  
  Latin Extended-A rather than printable ASCII - 393 glyphs, 31 KB. Noto ships italic only as a  
  VARIABLE font, so it is pinned to the regular weight before subsetting; both recipes are in the  
  directory's README.  
  Source is google/fonts, not the face vendored beside the bold: harfbuzz's notosansitalic.ttf is a  
  four-glyph test stub, and nothing else on a Mac answers to the name - NotoSansOldItalic is an  
  ancient alphabet, not a slant. SIL Open Font License 1.1.  
  No layer names it yet. It is carried ahead of its use so the POI labels this style still owes have  
  a face to land on; verified meanwhile against OpenFreeMap Liberty, which does name it, where  
  "College prive La Salle" renders slanted with its accents intact and the road labels beside it stay  
  upright.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(tools): keep a dash in proportion to its line across the zooms it is drawn at  
  A MapBox dash length is a multiple of the line width, and CartoCSS takes ONE pattern of pixels per  
  rule - the decoder rasterises it into a bitmap keyed by the literal string, so it cannot be a  
  function of anything. One scale therefore had to cover a whole width ramp, and could not: Liberty's  
  rail hatching is [0.2, 8] over a width running 3 px at z15 to 8 px at z20, and the 5.5 we picked  
  drew it 1.8x too long at z15 and 0.7x too short at z20. It reads as "the dashes are twice the size".  
  splitDashByZoom cuts such a layer into one attachment per band, each scaling its dash by the width  
  in the MIDDLE of its own band. Bands are cut where the width doubles - ceil(log2(ratio)), capped at  
  4 - so the worst error inside one is sqrt(2) instead of the ramp's whole range. That hatching  
  becomes 0.75,30 below z18 and 1.25,49.96 above, where it was a single 1.1,44.  
  Only for a PLAIN dash. Where the style ramps the dash itself, the pattern states the zoom it begins  
  at, reading the width there is already the targeted answer, and Standard's stair treads depend on it  
  - that path is untouched and still tested.  
  Three things stop it doing harm: it measures from the first stop whose width is POSITIVE (the  
  hatching ramp starts at (14.5, 0), and below that there is no width to be in proportion to) and no  
  further than the last stop, above which the width is flat; the outer bands keep the layer's own  
  minzoom/maxzoom, so banding never narrows what is drawn; and band edges are whole zooms, because  
  zoomPredicates floors the min and ceils the max and a fractional edge would round outwards on both  
  sides and draw the seam twice.  
  It costs rules: +3 to +25 across the reference styles, Liberty 113 -> 138, which the 4-band cap  
  bounds. Checked side by side against maplibre over the same tiles at Grenoble z16.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(styles): start a list of what Massif Streets does differently on purpose  
  The preview compares this style against OpenFreeMap Liberty and Mapbox Standard, and most of what  
  differs between them and us has turned out to be a bug on our side. That makes the deliberate  
  departures worth writing down separately, or the next person closing a gap closes this one too.  
  First entry: a POI beats a road shield in a collision, where both references give it to the shield.  
  Symbol placement runs in REVERSE layer order - maplibre walks the style's layers from the last to  
  the first and whoever is placed first claims the slot - so Liberty's shields at layers 98-100 beat  
  its POIs at 91-94, and the SDK reaches the same answer by the opposite arithmetic (priority numbered  
  up with the layer index, LabelCuller sorting it down). A shield repeats along its road and can be  
  read further on; a POI is one place and is either drawn or lost.  
  Nothing changes yet: this style has no POI layers. It is recorded because the ordering is invisible  
  in the generated CartoCSS, and the natural thing to do when adding POIs is to copy Liberty's layer  
  order and inherit its answer without noticing.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(styles): draw the railways, with Mapbox Standard's own track treatment  
  Massif Streets drew roads, water and landcover and no railway at all, which through a station is  
  most of what is on the ground. Standard's is the two-layer idiom and its numbers transfer as written  
  - its zoom stops are on the 512 px tile, the convention this style is authored in.  
  The rails are a line-gap-width pair, the gap opening from 0 at z15 to 20 at z22 with each rail  
  0.5 -> 2 px. Over them goes a wide line worn almost entirely away by a very short dash, 2 -> 32 px  
  carrying [0.05, 0.5], which is what draws the sleepers.  
  Filtered on OpenMapTiles' `class = 'rail'` with `==` rather than an `in` over rail and transit: a  
  two-value set test is a disjunction and would have been this style's second when(). Trams are  
  therefore not drawn - OMT files them under `class = 'transit'` - and a second pair for them is owed,  
  noted in the README, since they want to be thinner than heavy rail anyway.  
  The README also records what is NOT taken yet: Standard's road widths, which are wider than ours  
  everywhere but motorway and compress the hierarchy differently (its minor roads sit much closer to  
  its trunks), and its near-hairline casing. Those are look decisions, so the measured numbers are  
  written down rather than applied.  
  Converts with the style still at exactly one when(). Checked at Grenoble station z15.5, where the  
  yard's parallel tracks and their sleepers match the maplibre pane over the same tiles.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(styles): widen the roads to Mapbox Standard's, and put trams on the map  
  Three things, one look.  
  Standard's `roads` ramp replaces ours, mapped onto OpenMapTiles class names - its  
  street/street_limited is our minor, and everything it does not name (our service) takes its  
  fallback. Ours were narrower everywhere but motorway and compressed the hierarchy differently: at  
  z14 a primary went 4.8 where Standard draws 6.0, a tertiary 3.2 against 5.1, a minor 2.0 against  
  2.9. Standard keeps its minor roads much closer to its trunks, and that is most of why a city read  
  thinner here than in the browser.  
  The casing came with it, because the two do not separate. Standard draws a near-hairline OUTSIDE  
  the fill - a line-gap-width outline of 1 px at z14 reaching only 2 by z22 - and only from z15, where  
  ours was a wider line under the fill from z5. Taking the widths alone is what would look wrong: at  
  z12 Standard's minor road is half a pixel and the old casing would have put 2.5 px of outline round  
  it, so the street would have read as a grey line with a white thread in it. Below z15 a road is now  
  its fill alone, which is Standard's own answer.  
  Trams get their own pair. Standard covers major_rail and minor_rail in one layer; OMT splits them  
  into class 'rail' and class 'transit', so this is two pairs with an `==` each rather than one over  
  an `in` - both bracket, the style stays at one when(), and a tram can be tuned thinner later without  
  touching heavy rail. Today they are identical, as Standard has them.  
  And the rails themselves lose their zoom floor: they draw wherever the tiles carry them, with only  
  the sleepers still waiting for z13 and their own fade. A hairline rail at low zoom is the line being  
  on the map at all.  
  Checked over the same tiles as maplibre at Grenoble z15.2 and at the station z15.5.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(styles): describe the casing this style actually has now  
  The README still said road-casing was a wider line under road-fill for --fold-casings to merge into  
  one line-border rule. It has been Standard's line-gap-width outline since the road widths came over,  
  and the fold was inert even before that - it refuses a pair whose fill states a line-sort-key, and  
  road-fill states one to order the classes. Measured: converting with and without --fold-casings now  
  gives a byte-identical stylesheet.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(tools): let a source style carry a property MapLibre will not accept  
  A hand-written source style has to stay a VALID MapLibre style - the preview draws it with maplibre  
  beside the SDK, and that comparison is the point of the file. But much of what is worth taking from  
  Mapbox Standard is GL v3 only: fill-extrusion-edge-radius, -vertical-scale,  
  -ambient-occlusion-intensity, -ambient-occlusion-ground-radius, -rounded-roof. Written into paint,  
  maplibre does not skip them, it REFUSES the file - the reference pane goes blank and the comparison  
  is dead.  
  metadata is the style spec's own escape hatch: arbitrary, ignored by every renderer. So they go  
  there under `massif:paint` and `massif:layout`, and applyMassifExtras merges them back over the real  
  blocks before anything else runs. The converter then treats them exactly as if Standard had stated  
  them. Converting a real MapBox style is unaffected: it states these in paint, where they are legal  
  for it.  
  Also stops those same properties being reported DROPPED. An extrusion's look is a Map setting here  
  and buildingMapSettings takes it before the per-layer pass runs, so the report said a bevel or an  
  ambient occlusion had been thrown away when it was sitting in the Map block. It said so for Mapbox  
  Standard's own 3d-building too. The fixture's dropped count is now asserted exactly, at the two that  
  are real.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(styles): put buildings on the map  
  Massif Streets drew none, which through a city centre is most of what is there - and it meant the  
  style could not exercise any of the SDK's building work this session lit or made translucent.  
  Standard's shape, adapted: a flat footprint over z14-15, then the extrusion from z15 with  
  render_height and render_min_height off the OpenMapTiles building layer. Its colour is Standard's  
  own colorBuildings default, hsl(40, 43%, 93%), with the hue shifted to 30 for the extrusion as  
  Standard shifts it - a warm off-white that suits this palette, and light enough to leave the  
  contrast to the lighting rather than the fill.  
  The parts maplibre has no property for ride in metadata (see applyMassifExtras): the 0.4 edge radius  
  that puts a seam down a facade instead of one flat face, the vertical-scale ramp that grows a  
  building out of the ground over a third of a level rather than popping it in, and the ambient  
  occlusion that sits it on the pavement. All four reach the Map block; the style stays loadable by  
  maplibre, which is what the reference pane needs.  
  Checked at Grenoble z16.6 tilt 55, both panes drawing the same buildings.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(tools): split a class list into rules even when nothing branches on it  
  A positive set test is a disjunction, so left whole it is a when() the decoder evaluates per feature  
  and the compiler prunes nothing around. expandSetFilter already split one into an attachment per  
  value, but only when the paint branched on that same field - a layer painting one way over a class  
  list kept its when(), on the grounds that N rules cost more than one or-chain.  
  Not where the set is the WHOLE filter: there is nothing to copy into the attachments, so each is a  
  single bracketed test the decoder prunes per tile, against an or-chain it walks per feature. The  
  existing gate still applies - the rest of the filter must bracket, or the when() removed comes back  
  N times (measured on MapTiler topo-v4).  
  Across the six reference styles: 442 when() -> 426, 1176 rules -> 1257. Massif Streets reaches zero.  
  The test that pinned the old rule now pins this one.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(styles): draw a road's name in Standard's hand, and keep its outline zoomed out  
  Road names are Standard's road-label: uppercase at 0.15 letter-spacing, 9 -> 16 px over z10 -> z18  
  down to tertiary, 8 -> 14 for minor, 6.5 -> 13 for service, hsl(0,0%,25%) on a hsl(0,0%,95%) halo.  
  Standard gates the classes with a step over zoom inside its filter; that is the one part not taken,  
  since a filter reading the zoom is a when(). One layer per class instead, least important first so a  
  motorway's name is placed first and wins the collision.  
  The casing no longer stops at z15. Standard's gate is there because a casing on a half-pixel road is  
  all casing, but the fill ramp already fades a class in by width, so the casing mirrors its zero  
  points and follows it down: 0.5 px on motorway/trunk/primary at z3, nothing for the rest until they  
  widen, everything cased by z12. That is what makes a zoomed-out road read as one line - liberty draws  
  its casings from z5 for the same reason.  
  Checked at Grenoble z11 and z13, both rows, both tilesets.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(styles): name the places on the map, and let a poi beat a shield to the slot  
  Three poi layers on liberty's rank bands - rank < 7 from z15, 7-20 from z16, >= 20 from z17 - in the  
  italic face this style has been carrying for them since the shields shipped.  
  Standard could not lend this one. Its poi-label is built on filterrank, sizerank and maki, all  
  Mapbox tileset fields that OpenMapTiles does not have, so its density gate, its size steps and its  
  icon names resolve to nothing here. OMT offers rank and that is all.  
  They go LAST in the style, which is what makes a poi win a collision against a road shield: the  
  generated text-placement-priority runs 2.2M-2.4M against the shields' 1.6M-2.1M, and the culler  
  sorts it down, so the poi is placed first and takes the slot. Both references give it to the shield.  
  A shield repeats along its road and can be read a hundred metres further on; a poi is one place and  
  is either drawn or lost.  
  No icons - the sprite holds shields and nothing else, so a poi is its name alone until an icon set  
  is drawn. Recorded in the README, with the transit pois that wait on the same thing.  
  Checked at Grenoble z16, both rows, both tilesets.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(styles): give every poi its icon  
  97 Maki drawings vendored under sprite-src/poi/, renamed from Maki's hyphens to the OpenMapTiles  
  `class` they answer to (art-gallery -> art_gallery) and recoloured to the label's grey. Eight do not  
  line up and are mapped by hand: rail for railway, rail-light / rail-metro for the two tram classes,  
  toilet for toilets, doctor for doctors, bicycle-share for bicycle_rental, slaughterhouse for butcher,  
  restaurant-sushi for sushi.  
  Named by the class, icon-image is a plain get(class) rather than a ninety-branch table - the  
  converter answers that with one style parameter per sprite, and a class with no drawing draws its  
  label alone, which is what liberty does. florist and furniture keep liberty's subclass override,  
  being the two shops worth their own glyph.  
  The sprite build reads one level of folders now, so a hundred poi glyphs do not sit loose beside the  
  three shields. The sprite name is still the bare filename.  
  Maki is CC0 - a public-domain dedication with no attribution requirement and no share-alike. It is  
  credited in the README because it is worth crediting.  
  Checked at Grenoble z16, both rows: the same bus, tram and park glyphs in the same places as the  
  reference, at the same size.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(vt): stop a line label's own length shrinking the line it has to fit on  
  A line placement is smoothed before the glyphs are laid out, so a line whose edges are shorter than  
  a glyph does not turn its own noise into a turn of the text. The smoothing averaged EVERY window  
  including the first and the last - and a centroid lies inside its own window, so both ends were  
  pulled inward by half a window.  
  The window is a fraction of the TEXT (PLACEMENT_SMOOTH_TEXT_FRACTION), so a longer run shortened the  
  very line it then had to fit on, and buildLineVertexData dropped it for having no room. Both hits  
  scale together: a wider face, or a letter-spacing, costs length twice. On a straight line of 11  
  vertices carrying a run of 9 units, 10 units of line measured 8.5.  
  The ends are kept where they are now and only the interior is averaged, which is what the smoothing  
  was ever meant to do - MapBox smooths nothing at all and measures the whole line. A two-vertex line  
  is unchanged, which is why the existing tests never saw this.  
  The new test fails without the fix.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(styles): colour a poi by what it is for, and let a street name outrank it  
  Three changes to the label layers, all of them about what a reader needs first.  
  COLOUR. Transit blue, outdoors green, care and emergency red, everything else the ordinary grey. The  
  map answers "can I get there", "is it a park", "is it help" before it answers "which shop is this".  
  The sheet is baked, so each drawing is re-cut in its group's colour rather than tinted per feature;  
  the label takes the same colour from a match on class.  
  ANCHOR. text-variable-anchor [bottom, top, right, left] with text-optional, which the converter  
  already maps to shield-anchors and shield-text-optional. The name takes whichever side of the icon  
  is free, and a crowded corner keeps the ICON and drops only the name instead of losing the place.  
  The gap grows with it - a radial offset of 1.0 where the fixed offset was 0.7, which was too tight  
  to read as a pair.  
  ORDER. Least important first: shields, then pois, then road names. Placement runs in reverse layer  
  order, so the names are now placed first and win. Both references order it the other way; ours is by  
  rank of what is lost - a shield repeats along its road, a poi is one place, and a street name is the  
  only label that street will ever have. A street whose name a cafe keeps taking is unusable.  
  Checked at Grenoble z16, both rows: blue transit pois with the name beside the icon, a red pharmacy,  
  and BOULEVARD GAMBETTA drawn where the pois used to take it.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(web): turn the map the way the drag goes, as maplibre does  
  A horizontal drag rotated the web map the opposite way to maplibre-gl under the same gesture. The  
  rotation speed was taken from maplibre's handler/mouse.ts and NEGATED, on the reasoning that a  
  bearing delta and rotate() turn opposite things. Only the PITCH needs that flip - maplibre's pitch  
  is 0 where this tilt is 90 - and rotate() already turns the map the way a bearing delta does.  
  Reported from the style preview, which draws both side by side under one camera. Not reproduced  
  here: a synthetic ctrl+drag pans rather than rotates, so the gesture could not be driven from the  
  harness - the sign is the user's observation, and it wants a confirming drag.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(styles): put the poi category on the icon and leave the label alone  
  Colour on the word was both too dark and the wrong place for it. Mapbox Standard puts the category on  
  the ICON and keeps every poi label one neutral grey - a categorical colour on a word is hard to read  
  and harder to scan, and the glyph beside it already carries the category. The label is Standard's own  
  colorPointOfInterestLabels family, hsl(203, 7%, 40%).  
  The icons keep the category, from MapTiler's categorical hues rather than invented ones, at a  
  lightness that reads on a near-white background: transit hsl(216, 60%, 50%), outdoors  
  hsl(126, 42%, 40%), care and emergency hsl(0, 58%, 52%), the rest Standard's grey.  
  The anchor list loses `top`. A name above its icon reads as belonging to whatever is above it, and  
  the icon is the thing that marks the spot - bottom, then right, then left.  
  Checked at Grenoble z17: red crosses, a green viewpoint, blue transit, every name the same grey.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(web): rotate the map around the cursor, the way maplibre does  
  A drag turned the map at a flat rate of degrees per pixel, so the ground slid under the cursor  
  instead of following it. maplibre does not do that: past 100 CSS pixels from the centre it turns the  
  map by the ANGLE the pointer sweeps AROUND the centre (generateMouseRotationHandler ->  
  getAngleDelta), which is what keeps the point under the cursor under the cursor. Nearer the centre  
  that angle is ill-conditioned, so it falls back to degrees per pixel - and reads the other way round  
  above the centre, which is where the swept angle reverses too.  
  That last part is why the previous commit's flat sign flip could not be right: the correct sign  
  depends on which side of the centre the drag is, so either sign was wrong half the time.  
  The angle is the SDK's own two-finger formula - atan2 of the cross and dot of the previous and  
  current vectors, screen y down - the same one TouchHandler::dualPointerPan is device-proven with, so  
  the sign comes from this repo rather than from translating maplibre's bearing convention.  
  Not verified here: the harness cannot drive the gesture (a synthetic ctrl+drag pans).  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(tools): turn both preview rows the same way  
  The two rotations are NEGATIVES of each other and the preview handed one to the other raw. maplibre's  
  bearing 90 puts EAST up; the SDK's rotation 90 puts WEST up (BaseMapView::getRotation, verbatim: "0  
  means looking north, 90 means west, -90 means east"). So a rotated camera turned the massif row  
  against the maplibre row, which reads as an SDK bug and is not one.  
  Negated in the one adapter that speaks both (massif.mjs moveTo), and the pane's readout reports  
  maplibre's bearing rather than the SDK's rotation so the four camera lines can be compared straight  
  across.  
  Checked at Grenoble z13: all four panes read rot -73.9 and draw the same orientation.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(vt): keep two different labels apart, as mapbox's text-padding does  
  A style could say how far a label had to stay from ANOTHER OF ITS OWN, and nothing about how far it  
  had to stay from a different one. text-padding arrived as minimum-distance, and minimum-distance is  
  grouped: for a line-placed label the group is the TEXT HASH (ShieldSymbolizer), so it held two  
  `D 1508` shields apart and did nothing at all between a `D 1508` and a `D 5`. All that separated  
  those was the culler's own one-unit floor - which is why a tilted view, packing a lot of far-field  
  map into a thin band, crowds worst.  
  An unstated minimum-distance is also what makes the decoder floor a repeat at the label's own size,  
  so a line-placed label could not be given one without disabling that floor. The converter skipped it  
  there for exactly that reason, leaving every line label unpadded.  
  So text-padding / icon-padding now reach the decoder as what they are: collision-padding, bound on  
  TextSymbolizer (ShieldSymbolizer inherits it), carried on the label style, and folded into whatever  
  buffer the caller asks calculateEnvelope for. It is per label, applies between any two, grows the  
  COLLISION box and not the glyphs, and leaves the repeat floor alone. An icon-only layer still drops  
  icon-padding: that is a marker, with no collision box of its own.  
  Massif Streets coverage 89% -> 93%, the 14 dropped text-paddings gone.  
  The new host test fails without the plumbing. NOT verified by render: driving a tilt from the preview  
  harness has not worked (synthetic ctrl+drag pans, keyboard pitch does nothing), so the density change  
  under tilt is owed a device check.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(tools): put a poi's name on the side the style asked for  
  The two anchor spellings are OPPOSITES and the converter carried them across name for name. MapBox  
  names the part of the TEXT nearest the anchor, so `bottom` puts the name ABOVE the point; the SDK  
  names the SIDE the text is laid out on (vt::LabelAnchor, "which side of its anchor a label's text is  
  laid out on"), so `bottom` puts it BELOW.  
  Two errors that cancel, which is why it read as correct in the massif pane and wrong in the maplibre  
  one beside it: the style asked for `bottom` meaning below, maplibre put the name above the icon, and  
  the SDK put it below. Now the style says what it means in MapBox's spelling - `top, left, right` for  
  below, then right of the icon, then left - and the converter flips each name on the way out. The  
  generated CartoCSS is unchanged; only the maplibre row moves.  
  The poi layers also state text-justify auto, which the converter already maps to  
  shield-text-horizontal-alignment 'auto' - LabelLineAlign::AUTO, which follows the side the culler  
  chose, so a name below its icon is centred and one beside it is flush against it.  
  The --shield-anchors flag is untouched: it writes the SDK property directly, in the SDK's spelling.  
  Checked at Grenoble z15 tilt 60: names below their icons in both rows.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(tools): open the preview at a camera, tilt included  
  ?lon= ?lat= ?zoom= ?tilt= ?rot= on the preview URL, in maplibre's names and conventions - tilt is  
  its PITCH, 0 straight down, which is what both rows print.  
  Written because a tilted view could not be reached at all from an automated check: a synthetic  
  ctrl+drag pans instead of rotating, keyboard pitch does nothing, and dragging the compass reached  
  about 6 degrees. Every label-density question under tilt went unverified for want of it.  
  The massif panes take the opening camera in their own query and apply it once they are up, rather  
  than waiting for the first sync - which only fires on a move, so ?tilt= would have left the massif  
  row flat under a tilted maplibre one.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(tools): let a style put its palette in project.json  
  `"metadata": { "massif:params": ["text-color"] }` turns that property's match on one field into a  
  style-parameter LOOKUP - [param::poi-fill-[class]], one parameter per label, the match's fallback  
  left in the rule for ?? to land on. The palette is then editable in project.json without touching the  
  generated stylesheet, and a sixty-branch ternary the decoder walked per feature becomes one lookup.  
  Opt-in per property, because only the author knows which is which: a table is worth it for a palette  
  meant to be tuned and not for the two-branch colour ramp on a road. metadata is ignored by every  
  renderer, so a layer asking for it stays a valid MapLibre style, and every other converted style is  
  byte-identical - measured across the six references, no change. `["icon-image"]` covers a  
  recolourable icon's own params, so an icon palette lands in the same place as the label's.  
  Also raises the split cap for a set that IS the whole filter. Splitting normally copies the rest of  
  the filter into every attachment, which is what MAX_VARIANTS guards; with no rest to copy the only  
  cost is one bracketed rule per value, and a poi category of sixteen classes needs it. Across the six  
  references that is one when() fewer and seventeen rules more, all in maptiler-openstreetmap.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(styles): stand every poi on a disc, and bring them in by category  
  TWO SPRITES BECOME ONE. Each drawing is now a white disc with a grey ring and a neutral glyph -  
  three flats, which is what lets extractIconPlate split it: the glyph becomes a distance field the  
  style tints (shield-icon-fill) and the disc becomes the label's icon plate, whose fill, ring and  
  radius are style properties. Nothing is baked but the shape, where before the colour was cut into 97  
  PNGs and a recolour meant recutting them.  
  The colours arrive the way Mapbox Standard states them, as icon-image image params - background,  
  background-stroke, icon. Those are GL v3 and maplibre will not parse them, so they ride in metadata  
  and the plain icon-image beside them is what the reference pane draws: the maplibre row shows the  
  neutral disc, the massif row the category colour. That is the cost of one sprite, and the same trade  
  the extrusion properties already make.  
  CATEGORY, NOT RANK. Standard's poi-label is built on filterrank, sizerank and maki, none of which  
  OpenMapTiles has. MapTiler's OpenStreetMap style is built on the SAME schema and gates by category  
  and zoom, which says something rank cannot: a museum matters before a bus stop. Its zooms, over the  
  classes we have drawings for - cemetery z14, cultural/attraction/shop z15, food, education, outdoor,  
  sport, health, worship and public z16, transport and lodging z17, waste and the small shops z18.  
  Fourteen layers, each an `in class` the converter splits into one bracketed rule per class, ordered  
  least important first. The three rank bands are gone.  
  95 poi rules, no when(), coverage 99%. Checked at Grenoble z17: every icon on its disc, names below.  
  Co-Authored-…

- due to [`937e61e`](https://github.com/massif-maps/MassifMaps/commit/937e61ec6229ad3bc895d538e61f10d5ed012c5a) - let a distant label shrink the way maplibre's does, and match Standard's POI palette *(PR [#251](https://github.com/massif-maps/MassifMaps/pull/251) by [@farfromrefug](https://github.com/farfromrefug))*:

  labelPerspectiveScaling defaults to 0.5, so labels past the focus point  
  render smaller than in 6.0 on a tilted map. Set  
  VectorTileLayer.setLabelPerspectiveScaling(0) to keep the old constant-screen-size  
  behaviour.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  ---------

- due to [`a3ff0f3`](https://github.com/massif-maps/MassifMaps/commit/a3ff0f34946748522014bb19421ed72e57e51eb2) - draw 3D terrain, buildings, labels and shadows on the globe *(PR [#254](https://github.com/massif-maps/MassifMaps/pull/254) by [@farfromrefug](https://github.com/farfromrefug))*:

  with TerrainOptions enabled, setting RENDER_PROJECTION_MODE_SPHERICAL now  
  draws 3D terrain instead of silently ignoring it. An app that set both and relied on the  
  globe staying flat has to disable terrain explicitly.  
  This has NOT been seen on a device. It is host-tested arithmetic and a shader that has  
  never been compiled; the first device check is the real gate - terrain at a globe camera,  
  and a planar A/B to confirm none of it moved the shipping map.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): keep the camera and picking rules off the globe until they follow it  
  Opening the gates took three blocks with them that are still written in internal Mercator  
  coordinates, and on a globe they are fed a point on a sphere instead. Device check on  
  emulator-5554 with Mapbox Standard: the focus point lands in the wrong place and zooming in  
  stops working after a zoom out.  
  MapRenderer's focus-on-ground block reads focusPos.xy and cameraPos.y as internal x/y, so  
  on a globe it looks up a terrain height at nonsense coordinates and then lifts the focus by  
  the result, every frame. Auto-flatten compares AutoFlatten::parallax against a threshold in  
  raw world units, and the globe's world is twice the plane's scale, so every parallax there  
  reads double. VectorLayer's terrain surface picks through ElevationManager::intersectRay,  
  which marches the height field in the planar frame.  
  All three go back to planar-only. Terrain still RENDERS on the globe - the transformer, the  
  shader, the mesh and the surface are untouched - so this narrows what was opened rather  
  than reverting it. They come back when P4 makes them surface-aware.  
  Also ignore Options::getTileTransformer in the SWIG interface, next to getProjectionSurface:  
  it returns a vt type the bindings have no wrapper for, and the demo only built because the  
  checked-in wrappers predated it.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): subdivide globe terrain tiles for the sphere as well as the relief  
  Terrain tiles on a globe drew as a grey lattice with no landcover over the distant part of  
  the sphere, and the default background bitmap showed through them. Reproduced on  
  emulator-5554 with Mapbox Standard at zoom 3.4, and isolated by A/B: with terrain off the  
  same camera renders perfectly.  
  The decorator forwarded calculatePoint, calculateNormal, calculateVector and calculateHeight  
  to the base but NOT the three tesselation entry points, so a globe tile got terrain's  
  relief subdivision and never the sphere's curvature subdivision. At low zoom that leaves  
  triangles chording straight through the planet, which then lose the depth test.  
  Each entry point now runs the terrain pass into a temporary and hands the result to the  
  base. On a plane the base is a pass-through, so this costs one copy and changes no output.  
  The drape bake is still wrong on a globe - terrainPaintVsh treats aVertexPosition.xy as the  
  tile's unit square, which it is not there, and the log still reports blank baked tiles. It  
  does not show yet because the RTT drape is inactive on the globe and the per-layer path is  
  used instead.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): give the globe the shared ground, so 3D terrain is drawn at all  
  With the RTT drape enabled - the demo's default - a globe had no 3D terrain whatsoever.  
  Reported from the device: "there is no 3d terrain".  
  The drape bakes tile content through a matrix that maps the tile's UNIT SQUARE onto the  
  bake target. On a globe a vertex is a curved position instead, so every tile baked blank,  
  the ground was never drawn, and the frame fell through to the per-layer depth path with no  
  terrain surface in it.  
  The globe now takes the shared ground whatever DrapeFillsEnabled says. That path displaces  
  in the vertex stage rather than baking, so it needs no unit square - and it is the path that  
  replaces the RTT drape anyway, which the render docs say not to build on.  
  TileLayer::resetTileTransformer derives its tesselation mode from the same flag and its own  
  comment warns that a mismatch leaves tiles decoded for the other mode in place forever, so  
  it takes the same globe override.  
  The shared ground now activates - "shared terrain ground - 1 layers, 1 cover tiles" where  
  the log used to say neither path was active. It still reports 0 ground draws, so the surface  
  itself is not reaching the screen yet; that is the next thing.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): turn terrain mode on for the globe in the tile renderer too  
  TileRenderer carried two RENDER_PROJECTION_MODE_PLANAR gates of its own around the flag it  
  passes to GLTileRenderer::setTerrainMode. Without that flag renderTerrainGround returns 0  
  before it looks at anything, so the globe could never draw a terrain surface no matter what  
  the rest of the pipeline did.  
  I missed these when the gates were opened: that change checked MapRenderer, VectorLayer and  
  TileLayer, and the claim that the constant no longer appeared was scoped to those files  
  rather than to the tree. Two of TileRenderer's four uses stay - ray picking through  
  ElevationManager::intersectRay, which marches the planar frame, and isPlanarProjectionMode,  
  which is a projection query and not a terrain gate.  
  Still unevaluated: terrain's default minZoom is 5 and the bench parks the camera at zoom  
  3.43, below the level where terrain exists, so every frame measured so far says nothing  
  either way. This needs a hand-driven camera above zoom 5.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(globe): record that 3D terrain is broken on the globe, and how to diagnose it  
  The globe draws 2D content, labels, the limb and the sky correctly on a device. 3D terrain  
  reaches it and is visibly wrong: a flat surface, plus tile-sized quads floating in the sky.  
  Seen at Mont Blanc, zoom 13, tilted, with Mapbox Standard on emulator-5554.  
  Write down the number that tells the two candidate causes apart rather than a guess. A  
  spherical tile-local unit is EARTH_RADIUS / 2^zoom - 778 m at zoom 13 - so 4000 m of relief  
  must be about 5.1 tile-local units. The spherical branch of setupTerrainUniforms divides  
  calculateHeight's result by frameScaleZ, which for the planar line converts internal units  
  to frame units but here may be a second conversion, leaving the displacement about 40x too  
  small. It is not obviously wrong - TileSurfaceBuilder stores coords3D already  
  matrix-transformed - so it wants measuring, not editing.  
  Also record the two things that wasted measurements: terrain's minZoom is 5 while  
  BenchActivity parks at zoom 3.43 whatever --es zoom says, so every frame below that shows  
  0 ground draws correctly and proves nothing; and the "neither path is active" line sits  
  behind a static bool and is logged once per process, so it can be a stale first frame.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): draw 3D terrain on the globe instead of flat quads in the sky  
  Two bugs, both found by reading rather than by logging, reported together from  
  emulator-5554 at Mont Blanc z13 tilted: the surface was flat and tile-sized quads  
  floated at assorted angles.  
  The quads: TileRenderer turned the tangram shared ground on whenever a terrain  
  texture provider exists, and that surface is ONE flat unit grid reused per tile.  
  A spherical tile matrix only scales and translates, so it cannot curve or orient  
  that square - each tile got a flat quad at its own origin. The grid is planar-only  
  now; the globe takes the per-tile surfaces, which the spherical transformer curves  
  and terrain subdivides, and which are the only ones carrying aVertexSkirt.  
  The flatness: calculateHeight returns metres in the TILE's own frame, and  
  setupTerrainUniforms divided that by frameScaleZ - correct for the planar line  
  beside it, a second conversion here. 4000 m of relief read as 0.13 tile-local  
  units instead of 5.1 at z13. The conversion is the ratio between the tile frame  
  and the draw's frame (1, the coordScale, or ~40 for the shared ground's internal  
  coordinates); it is vt::sphericalMetersToFrame now, split into its own header so  
  the test can pin it against the old formula.  
  An on-device check is still required: 3D terrain at a globe camera, plus a planar  
  A/B - the regular grid is the shipping map's ground path.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(globe): record what the device shows after the terrain fixes - relief lands, content does not  
  Seen on emulator-5554 at Mont Blanc: the relief and the missing quads are confirmed, and the  
  next bug is content over the ground - fills shredded, every line gone - with the world-space  
  depth terms and the sphere's 2x scale as the first suspect.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): put the map's lines back on the globe's terrain  
  Two more globe-only faults, both found by reading, both seen fixed on emulator-5554  
  at Mont Blanc z11 tilt 40.  
  Every LINE was missing - contours, roads, labels - while the same camera with  
  terrain off drew them all. lineFsh discards a fragment outside its target tile,  
  from vTileUnit = pos.xy * uTileUnitScale + uTileUnitOffset, and that scale is  
  non-zero exactly when the tile has elevation. The affine form is the plane's: on a  
  sphere pos.xy is a curved position, so the test threw nearly everything away.  
  vTileUnit now comes from the same sphere inversion the DEM node uv uses, against  
  the tile's own Mercator extent in radians, antimeridian wrap included.  
  And the previous commit turned _terrainRegularGrid off for the globe in  
  TileRenderer, which also switched the renderer to the ADAPTIVE depth model while  
  MapRenderer kept driving the shared ground. Only the GEOMETRY is planar-only now,  
  through GLTileRenderer::terrainGridSurfaces(); tangram's painter depth model, which  
  hangs off the same flag, stays. The lattice clamp, edge stitching, ground shadow  
  casting and the terrain paint follow the geometry - all four read the grid mesh and  
  all four were already out of scope on the globe.  
  Polygon fills are still shredded on the globe; the planar no-drape A/B at the same  
  camera is clean, so it is spherical. Next.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(terrain): drape the map onto the globe's terrain  
  Fills came out shredded on the globe because they had no drape to bake into: a fill  
  is carried at SOURCE DENSITY on purpose, and MapRenderer refused the RTT drape in  
  spherical mode - the bake maps a tile's UNIT SQUARE onto the bake target, and a  
  sphere vertex is a curved position, so every tile baked blank.  
  Same problem as the line clip, same answer. drapeBakeClip places a baked vertex by  
  terrainSphereTileUnit, and the surface samples the drape with that unit instead of  
  its vertex xy. Geometry only: backgrounds and rasters bake through a flat quad whose  
  xy already is the unit square. A spherical bake therefore keeps its TERRAIN program,  
  where the sphere helpers live, takes the drape matrix whole without the coordScale  
  the planar path folds in, and builds the geometry ortho from the layer's TARGET  
  tile - the one its sphere uv was uploaded for.  
  drapeFills is no longer forced off on the globe, in MapRenderer and in  
  TileLayer::resetTileTransformer alike; the two must agree or tiles stay tesselated  
  for the other mode.  
  Seen at Mont Blanc z11 tilt 40 on emulator-5554: relief, fills, water and contours  
  all land, in perspective. A planar run of the same build is unchanged. The ground  
  still reads grey where the plane has it near-white, and labels are missing - both  
  their own paths.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(labels): anchor labels onto the globe's terrain  
  A label's anchor was moved onto the terrain by writing the height into its z, and the  
  height was looked up with the anchor's x/y read as internal Mercator. Both are the  
  plane's conventions: on a sphere the anchor is a curved world position, so the lookup  
  sampled the wrong ground and the lift pushed the label along the world z axis, off the  
  surface - no labels at all on globe terrain, while the same camera with terrain off drew  
  them.  
  vt::TileTransformer answers both now: calculateMercatorPos turns a world position into  
  the internal Mercator xy an elevation lookup is keyed by, and calculateElevatedPos moves  
  a world position to a height ABOVE the surface - absolute, so re-anchoring is idempotent,  
  and radial on a sphere, dropping the Mercator stretch an internal height carries. Both  
  are the identity-ish planar pair, so the plane is unchanged. Label::applyElevation  
  therefore takes positions rather than heights, which is what removes the z assumption  
  from the label path.  
  tests/api/GlobeElevationScaleTest.cpp pins the invariant that matters: a lifted label  
  anchor lands exactly where SphericalProjectionSurface puts the same MapPos - a label and  
  a marker at one place must not disagree.  
  Seen at Mont Blanc z13 tilt 60 on emulator-5554: contour labels back on the globe,  
  following the relief.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(camera): calibrate zoom on the globe's own world, not the plane's  
  The zoom-0 camera distance came from Const::WORLD_SIZE, the PLANAR world width, whatever  
  surface was active. A sphere's equator is twice that, so on the globe the camera sat at  
  half the distance its zoom meant: every style width, label and contour drew a zoom level  
  too large, and by zoom 12 the camera was inside the relief.  
  ProjectionSurface::getWorldWidth() answers per surface - WORLD_SIZE on the plane,  
  2 * PI * SPHERE_SIZE on the globe, which is 2 * WORLD_SIZE - and the terrain surface  
  forwards its base's. The plane reads the same constant it always did.  
  tests/api/SphericalSurfaceTest.cpp pins both widths and the consequence: a zoom-12 tile  
  of world is one tile wide on each surface, measured through calculateDistance.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(camera): hold the camera off the terrain without moving the view  
  Three faults in one rule, on both surfaces.  
  The focus is pinned to the ground so a zoom means "distance to the terrain", and the  
  lift carries the camera with it - mapbox's _centerAltitude. Pinned at EVERY altitude, a  
  pan across a ridge lifted the whole view, which reads as the map bobbing from far above  
  the ground. CameraClearance::focusFollow ramps it: the full ground height at the  
  clearance shell, none of it FOLLOW_BAND (4) shells above. Everything feeding the ramp is  
  measured with the focus PINNED - the lift moves the camera, so a ramp fed the current  
  height would drive its own input and oscillate.  
  The per-frame correction then raised a camera under the shell by TILTING it up, and by  
  zooming out past the tilt range: with the focus no longer riding the ground, panning  
  into a hillside made the tilt jump to 54 on its own. CameraClearance::shellCameraZ is the  
  camera height the shell asks for over the ground UNDER THE CAMERA, and it does not depend  
  on the focus - which is what lets the focus be raised to satisfy it. The camera keeps the  
  tilt and the zoom it was given and rises. The zoom BOUND stays, so a zoom still cannot  
  drive the camera into the ground. TerrainOptions::CameraClampDuration animated that  
  correction and now has nothing to animate; the option is left alone rather than broken.  
  And both were planar-only: they read a position's xy as internal coordinates and its z as  
  a height, which a sphere's 3D point is not, and liftFocus moved along the world z axis.  
  Both go through the projection surface now - calculateMapPos in, calculatePosition  
  (ViewState::setFocusHeight) out - so the lift is radial on a globe and a z move on a  
  plane. The trap is that an ORBIT is a world distance while a height is an internal one,  
  2x apart on a sphere: ViewState::worldPerInternal is the conversion, and getTerrainMaxZoom  
  takes it too.  
  Seen at Mont Blanc z12 tilt 60 on emulator-5554: the globe holds the camera it is given,  
  where it drifted to 13.20 before.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(labels): keep labels the size they are on the plane, on the globe too  
  Two faults, one hiding the other.  
  The constant-on-screen-size rule and the pixel-grid snap were gated behind  
  vt::ViewState::planarProjection, so on the globe labels fell back to scaling with the  
  perspective divide - visibly growing and shrinking as you zoom. Both corrections are  
  pure screen space (view depth over focus depth; a snap in NDC), so nothing in them needs  
  a flat world and the flag is gone.  
  That exposed the second: a label's world size is 2^-zoom scaled by vt's own _scale, and  
  VTRenderer sets that to Const::WORLD_SIZE. The globe's world is twice as wide, so every  
  label came out half size. vt::ViewState::zoomScale carries ViewState::worldPerInternal()  
  now at all five construction sites - the culler's included, since its envelopes have to  
  agree with the glyphs that get drawn.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(layers): refine the globe's tiles at the distance the plane refines its own  
  Tiles refined far too late on the globe - you had to be nearly on top of one. Tangram's  
  rule projects a tile's four corners (0,0)...(1,1) through the tile MATRIX and compares  
  the screen area they enclose, but that matrix only scales and translates: on a sphere the  
  four points land off the surface entirely and the area means nothing.  
  The corners go through createTileVertexTransformer()->calculatePoint() now, which is the  
  same unit square on a plane. The rule's other planar assumptions went with it: the LOD  
  elevation is applied through calculateElevatedPos, so it is radial on a sphere; the  
  incidence cosine measures against the tile's own normal rather than the world z axis; and  
  the two view-distance limits - a style's terrainMaxVisibleDistance in metres, and the  
  terrain cover-tile budget - convert through the surface's own world width.  
  Third fault of one family, after the camera distance and the label size: a length in  
  WORLD units compared against a planar constant.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(layers): place vector elements on the terrain on the globe too  
  Markers, lines and every other vector element skipped TerrainProjectionSurface in  
  spherical mode, so they were positioned at sea level while the map they sit on is  
  displaced - they read as offset, and increasingly so at a tilt.  
  The gate was written for picking, and only picking needs it: the surface delegates every  
  position to its base and adds the height, so it is projection-agnostic apart from  
  calculateHitPoint, which marches the height field in the planar frame. That one keeps its  
  documented fallback - picking on a globe hits the base surface, not the relief.  
  TileRenderer::isPlanarProjectionMode goes with its last caller.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(vt): draw 3D buildings on the globe  
  Extrusions were submitted on the globe and never reached a pixel: polygon3DVsh derives the  
  overzoom clip from the vertex xy, which is a curved position on a sphere, so the fragment  
  shader discarded every wall and roof. It takes the sphere's tile unit now, as the line clip  
  already did.  
  Two scale faults were hiding behind it: the extrusion base rebuilt a z-up vertex instead of  
  offsetting along the surface normal, and uBaseScale converted an internal z with the plane's  
  ratio, half of what the sphere needs.  
  Verified at the Louvre, zoom 17 tilt 60, on emulator-5554: extrusions, streets and labels  
  match the planar frame.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): keep the point under the finger when panning the globe  
  A pick and a pan anchor on ProjectionSurface::calculateHitPoint, and over terrain that asks  
  ElevationManager::intersectRay, which marches the height field in the planar frame. On a globe  
  it answers nothing, so the anchor fell through to the sphere at sea level and the map slid out  
  from under the finger. The fallback bisects on the height above the terrain instead, which  
  needs only the base surface and the height field.  
  The globe's terrain depth pre-pass also coarsened a level early: its subdivision compares a  
  world length against WORLD_SIZE, and the sphere's world is twice that. It reads the surface's  
  own world width now.  
  An on-device check of the pan feel is still owed; the host suite covers the arithmetic.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(demo): run any gallery example on the globe with --es globe true  
  The examples are written against the plane, so this is what exercises the sphere with real  
  content - day-cycle-light covers terrain, shadows, buildings and labels in one run. Applied on  
  the example thread before the camera overrides, not posted: switching the surface re-derives  
  the camera, and racing it lost the example's own zoom and tilt.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(demo): switch a running example onto the globe from the gear panel  
  A `projection` row under Terrain, so an example can be held against the sphere and back without  
  a relaunch - the same facade property `--es globe true` sets at launch. Verified on  
  emulator-5554: the row flips to `spherical` and the map draws Europe on the limb.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): switch a globe map between 2D and 3D again  
  The 2D/3D switch bailed out on any non-planar projection, on the belief that  
  AutoFlatten::parallax reads double there. It reads HALF: the height range is in  
  INTERNAL units and the camera distance in WORLD ones, and only the second is  
  doubled by the globe. The bail-out took the seeding, the ratio ramp and  
  setDecodeActive with it, so a map opening flattened in FLATTEN_MODE_FULL never  
  got its 3D decode back - the ground stayed at a coarse density with an empty  
  drape. Convert the height range and let the switch run on both surfaces.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(vt): light a globe's buildings and its ground as the plane lights them  
  A lighting shader - including an application's own - is written against the  
  MAP's east/north/up: normal.z is how far a face points up and uSunDir.z is the  
  sun's height, and applyLighting3D uses both apart from the N.L term. On a globe  
  a geometry normal is the SPHERE's, so a Paris roof read as a wall facing north  
  and every building went dark and flat.  
  Rotate every normal into the VIEW's own frame (uLightingFrame) before it reaches  
  a lighting function - lightingNormal in the vertex stage, groundLightNormal for  
  the DEM slope in the fragment one. The sun uniform is left alone, which makes it  
  one fixed direction in space: the plane's picture at the focus and a terminator  
  towards the limb.  
  The ground had the same class of bug one layer down. setTerrainLightVaryings  
  built vElevUV with the affine planar form, the exact mistake the displacement  
  path already had a spherical inversion for, so shading and shadowing sampled the  
  DEM at the wrong texels. uTerrainSphereElevUV is that inversion for the full  
  elevation texture, beside the node one.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(vt): cast terrain shadows on the globe  
  Two independent failures, and the first hid the second.  
  The light box: calculateShadowViewProj built an axis-aligned world rectangle  
  from the drawn tiles plus a world-Z slab, and took the sun as a world vector -  
  none of which means anything on a ball. The spherical path drops the rectangle  
  (it only trimmed a box the view frustum's bounding sphere gives anyway), rotates  
  the sun out of the map's east/north/up with the same anchor uLightingFrame uses,  
  converts the height slab from internal to world units, and culls casters against  
  calculateTileBBox grown radially by that slab. texelMeters lands on 21.50/62.11 m  
  on both surfaces at the same camera, which is what says the sides are right.  
  The passes: renderShadowCasters and renderTerrainShadowMask both opened with  
  terrainGridSurfaces(), false on a globe because the shared regular-grid VBO is  
  planar-only. Both drew nothing and the mask was left cleared to white, so the  
  shadow strength had no effect on the picture at all. They now ask for terrain  
  alone; the mask rides renderTileSurfaceFill, which already picks between the  
  shared grid and a globe's per-tile surfaces, and the caster ground pass gained  
  the same branch. The grid's one-bind-per-pass fast path is untouched.  
  Emulator-5554, terrain-3d at Zermatt: shadow strength 0 vs 1 moves 19-40% of the  
  ground pixels on the globe (planar control 17-29%). It moved 0.0% before.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(layers): refine a globe's tiles when the terrain is off or flat  
  Tangram's LOD rule compares the screen area a tile's four corners enclose. On a  
  sphere a coarse tile's corners land on top of each other - the root's are all on  
  the antimeridian - so the area is 0, nothing subdivides, and the recursion stops  
  at zoom 0. Terrain hid it: _terrainMinTileZoom forces subdivision whatever the  
  area says. With the terrain off or flattened a globe map went blank - measured,  
  1 visible tile at zoom 0 against the plane's 8 at zoom 11 at the same camera.  
  Sample the patch on a 3x3 grid and sum its cells when the transformer is  
  spherical. steps = 2 on a plane keeps the old four-corner arithmetic exactly.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(labels): keep a globe's labels readable at a top-down tilt  
  Label::calculateTerrainScaleFactor divides the view depth by the camera-to-focus  
  distance to hold a label at a constant screen size. With ViewState::focusDistance  
  0 it falls back to origin(2) / -viewDir(2) - the camera's height above the z=0  
  PLANE, right on a planar map and the camera's world z on a globe, thousands of  
  kilometres at Zermatt. Every label landed on the 0.05 scale floor: placed,  
  culled, drawn and invisible - 1989 of them in the pass at Zermatt z12 tilt 90.  
  The 0 came from TileRenderer's prepareViewState, the state installed before the  
  cross-layer drape, which set zoomScale and lightBrightness and not focusDistance.  
  Labels on a planar map at a high tilt change size slightly with this, and that is  
  the correction: the fallback measured the height above SEA LEVEL, not the  
  distance to a focus standing on the terrain.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(renderers): frame the same ground at the same zoom on the globe as on the plane  
  A WORLD length is uniformly twice the plane's on a globe. An INTERNAL one is not:  
  Mercator stretches its own coordinates by 1/cos(latitude) and a sphere has none  
  of it, so an internal unit covers 2*cos(latitude) of the globe's world and a  
  flat 2 only at the equator. The camera's zoom is calibrated in internal units on  
  getWorldWidth(), the equator, so the globe's camera sat 1/cos too far: zoom 16  
  over Paris framed 1.52x the ground planar zoom 16 did, 1.44x at Zermatt.  
  That is also what read as taller buildings on the globe. The extrusions are 1:1  
  on both surfaces; you zoomed further in to frame the same view, and  
  building-height-scale ramps on the zoom NUMBER, so they rose.  
  ProjectionSurface::calculateLocalScale is that per-position number - 1 on a plane  
  at every latitude, 2*cos(latitude) on a sphere - ViewState::worldPerInternal  
  answers it at the FOCUS, and the calibration follows it, matching Mercator at the  
  centre latitude as mapbox-gl's globe does. The distance a zoom means therefore  
  moves with the focus, so calculateViewState re-derives zoom0Distance and  
  re-places the camera when it shifts by more than 0.01%; a plane answers the same  
  number every time and never enters that branch.  
  Straight cos(latitude) would put a world view at the +-85 clamp eleven times too  
  close, so the local scale fades back to the equatorial one as the planet fills  
  the frame - the orbit against the planet's own radius, full local within one  
  radius and fully equatorial past four. A zoom threshold would have been a screen  
  height and a DPI in disguise.  
  SphericalSurfaceTest pins the local scale. Device-checked at the Louvre, where  
  globe zoom 16 now lands on the planar zoom-16 frame street for street, and at  
  zoom 3 over latitude 65, where the limb still frames the planet.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): finish the 2D/3D ramp instead of freezing part-way  
  The ramp runs on a clock, so its first step has no delta yet and moves nothing -  
  and updateTerrainFlatten returned without requesting a frame whenever neither  
  the ratio nor the decode had changed. On a map that redraws on demand the switch  
  froze mid-RAMPING: asked for, and never arriving. Request the redraw while the  
  phase is RAMPING.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(vt): stand a globe's buildings on the ground under them  
  An extrusion's base arrives in INTERNAL z units, which carry Mercator's 1/cos(latitude) stretch,  
  but uBaseScale divided it by terrainTexture.metersToInternal - the EQUATOR value, which the planar  
  shader completes per vertex through vElevCosh and the radial spherical path has nothing to complete  
  it with. The base rose 1.52x the ground under it at Paris while the wall's bottom ring stayed on  
  that ground, so every wall stretched by ~18 m and the report was "buildings are taller in globe  
  mode". Divide by metersToInternal * cosh(mercatorY) at the tile centre instead.  
  Geometry and camera were never wrong: measured at the Louvre, world units per metre of height match  
  world units per metre of ground on BOTH surfaces, and the camera sits at 446.4 m planar against  
  446.6 m globe at the same zoom and tilt.  
  Verified on emulator-5554, day-cycle-light at z17.2 tilt 45: globe and plane now draw the same  
  block at the same height.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(vt): draw 3D buildings on a globe with the terrain off  
  TERRAIN_SPHERICAL was only compiled in alongside TERRAIN_VTF_FLAG. polygon3DFsh discards any  
  fragment outside its target tile and polygon3DVsh fed that clip aVertexPosition.xy, which is a  
  curved position on a sphere - so with the terrain off every extrusion was discarded and the map  
  drew its roads, its water and its labels and not one building. The same gate left those buildings  
  lit in the sphere's frame rather than the map's.  
  No new flag bit: bit 31 was already the spherical one, only withheld. Set it whenever the  
  transformer is spherical, move the geometry helpers (terrainSpherePoint, terrainSphereToMercator,  
  terrainSphereTileUnit, drapeBakeClip and their uniforms) out of #ifdef TERRAIN while everything  
  that reads the elevation texture stays inside it, split setupSphericalUniforms out of  
  setupTerrainUniforms for the draws that never reach the terrain path, and upload uLightingFrame on  
  every program bind.  
  Verified on emulator-5554, day-cycle-light at the Louvre z17.2 tilt 45: with the terrain off the  
  globe now draws the same buildings as the plane, in the same light. The planar frame is unchanged.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(demo): flip a running example onto the globe from adb  
  The gear panel already had the row; the adb receiver did not, so an A/B against the sphere meant a  
  relaunch and a different camera. One knob, the same facade property the panel writes:  
    adb shell am broadcast -a com.massifmaps.MassifDemo.CONFIG \  
        --es projection RENDER_PROJECTION_MODE_SPHERICAL  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(vt): stop a globe's shadow pass freezing the map when the sun moves  
  Two faults in the spherical caster path, both invisible until the SUN MOVED - which refits the light  
  box every frame.  
  The cull grew each tile's world box by the caster slab on ALL THREE axes; the slab is radial there,  
  so the offset belongs along the tile's own up. A z16 tile is 20 world units wide at Paris and the  
  slab is 26, so one tile's box covered a dozen and the cull kept everything.  
  Worse, the ground caster TESSELATED inside the frame: a caster tile is usually off screen, so  
  buildCompiledTileSurfaces missed the surface cache - 102 and 212 real tesselations in one pass at  
  ~5 ms each. The plane never pays this, its ground caster being the one shared regular-grid mesh; a  
  sphere's tile-local xy is curved and cannot use it. New tesselations are now rationed per pass and  
  the rest of the ring draws from what is cached, filling in over the next frames - the drape bake's  
  own shape.  
  Dragging the day-cycle hour on the globe, emulator-5554: shadow pass 200-1175 ms and frames of  
  264-1648 ms before, against 45-140 ms planar; the map froze for seconds and jumped hours of palette  
  between frames. After: caster pass 21-24 ms with 0 builds, every frame under 81 ms and different  
  from the last. Shadow strength 0 vs 1 at the Louvre hour 10 moves 2.4-5.0% of the pixels, against  
  0.8-4.9% for the planar control - nothing that cast was culled away.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(vt): cull a globe's shadow casters against the tile, not a box around it  
  The radial slab offset was applied to the tile's world AABB, and a tile at Paris is tilted 49  
  degrees - its axis-aligned box is far bigger than the tile itself, so the cull kept twice what it  
  had to. Sample the patch instead: four corners plus the centre, which carries the bulge a corner  
  hull would cut off, each raised and lowered along its OWN radial. That is the plane's z-slab hull  
  exactly, one dimension over.  
  Caster tiles kept per cascade at the Louvre, same sun and the same light box: 76-99 / 154-239  
  before, 59-60 / 90-92 after, against 38 / 55 for the planar control.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(terrain): stop the drape repainting the ground behind the sun  
  Two rations made the ground trail the sky whenever the hour moved.  
  The scene light goes into a drape tile's fingerprint so that moving the sun re-bakes the cover -  
  otherwise the buildings follow the hour and the ground does not - quantised to 64 steps per  
  channel. A day-cycle drag crossed a step every few frames, so the cover was almost always stale.  
  16 steps is still finer than a drape tile's own colour resolution and re-bakes a quarter as  
  often: 29 stale tiles queued during the drag before, 11 after.  
  And the stale class kept a COUNT ration of 8 tiles per frame with the camera at rest, so the  
  cover repainted tile by tile in front of the user. One per frame is right while the camera moves;  
  at rest the 60 ms wall-clock ceiling was already there to bound it, which is what it was written  
  for.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): give a globe ground tile the lattice the plane has  
  The plane draws every ground tile with one shared 64x64 grid. A spherical tile matrix cannot curve  
  a flat unit square, so the globe takes the per-tile surfaces - whose subdivision came from  
  SphericalTileTransformer's curvature threshold, EARTH_CIRCUMFERENCE / 64, about 626 km. A zoom-14  
  tile is 2.4 km, so from zoom 6 up nothing was ever split and a ground tile was TWO triangles.  
  TerrainTileTransformer does subdivide on top of that, but only when the DEM tile is already cached,  
  only when its relief exceeds 1 mm, and not at all in area source-density mode - three gates under a  
  mesh the plane has unconditionally.  
  TileSurfaceBuilder::setGridResolution lays the tile out on that same lattice instead, at  
  TerrainOptions::MeshResolution, whenever the globe has terrain. Positions stay per tile and on the  
  CPU, in double relative to the render origin: a shared grid curved in the vertex shader is not  
  available here, because the sphere point is O(1) and fp32 cannot hold the tile inside it. maplibre  
  floors the equivalent number at 32 for the same reason.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(vt): stop the globe's drape and its tile borders breaking at high zoom  
  terrainSpherePoint rebuilt the ABSOLUTE unit-sphere coordinate - uTerrainSphereOrigin + pos *  
  uTerrainSphereScale - and terrainSphereToMercator inverted it. That is the tile clip, the drape  
  bake target, vTilePos in polygon3DVsh and both DEM samplings. But a tile is 2^-zoom of the sphere:  
  at zoom 16 it spans 9.6e-5 of an O(1) coordinate whose fp32 ulp is 1.2e-7, so the tile-local detail  
  was below the ulp before the inversion ran. Worst error in the recovered tile uv, running the GLSL  
  verbatim in float, as texels of a 1024-texel drape tile:  
    zoom      10     12     14     16     17     18      19  
    before  0.02   0.15   0.46   2.38   2.37   8.83   17.76  
    after   2e-4   2e-4   2e-4   1e-4   2e-4   2e-4    3e-4  
  uTerrainSphereOrigin is itself a float uniform, so its own rounding was a PER-TILE shift - 1.3  
  texels at zoom 16, 5 at zoom 18, different for every tile. That is the break at the border.  
  terrainSphereMercatorDelta returns the Mercator offset from the vertex frame's own origin and never  
  forms the absolute point: the atanh difference identity with its small terms expanded out of the  
  displacement alone, and the series form of atanh, because log(1 + x) at x = 1e-4 throws away four  
  of the seven digits. Every uv uniform now carries its origin relative to that frame, differenced in  
  double on the CPU, which is also why none of them wraps the antimeridian any more.  
  The error is flat across zoom now, which is the signature to check if it regresses. The older  
  inversion test runs in double and by construction could never have seen this.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(vt): stand a globe's buildings on the ground under them, not kilometres away  
  An extrusion is a rigid prism at ONE elevation, so TileLayerBuilder stores the point its base is  
  read at and GLTileRenderer::resolveExtrusionBases reads it back through SpanResolver::tileMatrix2D  
  - a FLAT tile matrix, tile unit square to normalised Mercator. What was stored was  
  _transformer->calculatePoint(centroid), which on a plane is exactly (u, 1 - v) and on a globe is a  
  curved position in the tile's own frame. Distance from the tile square, measured at Paris:  
    zoom            14                  16                 17  
    planar          0                   0                  0  
    spherical  4.28 tiles (10.5 km) 4.28 tiles (2.6 km) 4.28 tiles (1.3 km)  
  A constant offset in TILE units, so it is a different world position for every tile - two halves of  
  one building read two different hills - and the metres scale with the tile, which is why the step at  
  a border grew as the integer zoom fell. The anchor rule in ExtrusionAnchors.h was doing its job:  
  both sides agreed on the anchor, then both looked it up somewhere else.  
  Stored as the flip written out. On a plane that is bit-for-bit what calculatePoint returned, so  
  nothing planar moves - the test asserts that too - and calculateHeight reads the same slot with its  
  own pos(1) term unchanged.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(vt): put back the antimeridian wrap a coarse stand-in frame needs  
  Dropped in the fp32 fix before this, on the argument that a relative form has nothing to wrap.  
  Wrong: terrainSphereMercatorDelta's longitude comes out of atan, so it is the true offset MODULO  
  2pi, and a coarse stand-in frame - an ancestor serving a finer target while it loads - sits up to a  
  world away in longitude. Measured on a zoom-1 frame, whose origin is at longitude 180: the true  
  offset reaches 337 degrees and 63 of 81 vertices come back a whole world out. Their uv then lands  
  outside the texture and CLAMP smears the edge row into stripes across the tile.  
  The uv uniform is measured from the same frame and wrapped the same way, so the difference is right  
  modulo 2pi and terrainSphereRelative brings the small answer back. Both terms are small, so the  
  wrap costs nothing in precision - the zoom-18 numbers are unchanged.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(vt): stop a globe tile below terrain's minZoom reading another tile's DEM  
  setupTerrainUniforms bails out early when a tile has no elevation, zeroing every planar elevation  
  uniform on the way - but it left uTerrainSphereNodeUV and uTerrainSphereElevUV holding whatever the  
  last draw put there, i.e. another tile's coverage. Below TerrainOptions::minZoom EVERY tile takes  
  that branch, so on a globe the whole zoomed-out map sampled the DEM through a stale window.  
  Zeroed alongside their planar pair, which is what the shader's uv arithmetic collapses to nothing on.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * Revert "fix(terrain): give a globe ground tile the lattice the plane has"  
  This reverts commit 0ee97814a.  
  It fixed nothing that was reported, and it costs a 65x65 mesh per ground tile where the curvature  
  split left two triangles - memory, CPU and a shadow-caster pass that has to tesselate them.  
  It was first reverted on the claim that it CAUSED the low-zoom streaking on the globe. That claim  
  was wrong and is withdrawn: the streaks reproduce with this reverted, and the artefact turns out to  
  vary from run to run, so the single-screenshot A/B behind it could not have attributed anything.  
  The streaking is still open and is unrelated to this commit.  
  What the lattice closed is a real gap, kept in 18-globe.md under what could be better: a globe  
  ground tile is two triangles from zoom 6 up, where the plane has its shared 64x64 grid  
  unconditionally. Re-land it once the ground sampling below it is understood - denser sampling is  
  what would make any such bug visible far more often.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * test(vt): pin the stand-in frame case, so it stops being re-suspected  
  Every other test here uses frame == tile. setupSphericalUniforms is handed the TARGET tile's id and  
  the SOURCE tile's frame matrix, so the two have to agree across a zoom gap - a stand-in, an ancestor  
  serving a finer target while it loads, which is the normal case at low zoom and was the leading  
  suspect for the low-zoom ground streaking.  
  They agree: the worst error is 0.006 drape texels four levels of overzoom deep, across source zooms  
  4 to 14. A negative result, kept because it rules the case out.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  ---------

- due to [`b31d8c5`](https://github.com/massif-maps/MassifMaps/commit/b31d8c5800acfb174364f751003b626bb85e4064) - 2D/3D switch, terrain & label fixes, and a geo-three terrain cut for the peak finder *(PR [#255](https://github.com/massif-maps/MassifMaps/pull/255) by [@farfromrefug](https://github.com/farfromrefug))*:

  a map tilted towards top-down no longer draws fog. A style that relied on haze at a  
  low tilt must raise the tilt or set the fog colour itself. Nothing changes above mapbox's pitch 65.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(labels): resolve a label's terrain height from any cached ancestor  
  The real root cause of labels vanishing over 3D terrain, found by measurement after three wrong  
  guesses: ElevationTextureCache::getDisplayHeight abandons its ancestor walk after  
  BASE_MAX_ANCESTOR_LEVELS = 1, while RenderStats reported zoomGap = 3 at Grenoble. The elevation sits  
  three levels coarser than the render tiles, so a one-level walk refused the only data there was and  
  returned false for EVERY label at every position - which is why elevReanchor stayed 0 through several  
  builds while the provider itself was rewritten twice.  
  The cap is right for its original caller and says so: an extrusion BAKES its base into its vertices,  
  so a far ancestor is a wrong answer rather than a coarse one (Paris, 127 m against 34 m). A label  
  anchor is re-anchored whenever the elevation changes and a few metres is invisible. So the bound is  
  the caller's now: extrusions keep 1, a label passes ANY_CACHED_ANCESTOR, which is what the grid path  
  already grants through LoadMode::CACHED_ONLY.  
  Device-confirmed on the Crosscall at Grenoble: 19 misses in 746 584 queries, elevReanchor 291 against  
  0, and the culler's visible count 813 against 4-22.  
  Carries a temporary PROF LABELELEV line reporting which source answered and the miss count, since  
  that is what finally located this. To be removed once the next round confirms it holds.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(labels)!: stop an occluded label hiding the one next to it, and let a grazing view label  
  Two faults in the terrain occlusion of labels, both reported from the device at Grenoble.  
  An occluded label still reserved its collision slot. Occlusion ran in GLTileRenderer::updateLabel, on  
  the GL thread, AFTER LabelCuller had already inserted the label into the grid - so a hidden label held  
  space and suppressed a visible neighbour. Measured: 299 of 343 placed labels lost to collision.  
  mapbox does the opposite and for this reason: an occluded symbol returns an EMPTY collision box from  
  the collision index (src/symbol/collision_index.ts), so it is never inserted. The test now runs during  
  placement, after updatePlacement so it only costs the labels that could actually take a slot - asking  
  before it ran on every considered label and cost ~18 ms a pass against ~4. A style that keeps a label  
  partly visible when occluded is still drawn, so it keeps its slot and is not tested.  
  The tolerance was wrong in KIND, not in size. An anchor sits on the terrain, but the surface DRAWN  
  under it differs by a small vertical error (the mesh chord, a coarser level, the half-resolution  
  read-back), and along the view ray that error is dz / sin(angle to the ground) - so at a grazing angle  
  a metre of height is tens of metres of depth and a label reads as behind its own ground. POIs in  
  Grenoble and on the slope below La Bastille were dropped from a low camera and reappeared as soon as  
  it rose; raising the flat tolerance did nothing, because a flat relative slack cannot know the angle.  
  The four neighbour taps already fetched measure that angle - their depth SPREAD is how much the  
  terrain's distance changes over a few pixels - so the slack comes from there. Top-down the spread is  
  ~0 and the test is as tight as it was.  
  RenderStats grows an occluded= field, so the culler's verdicts still sum to sorted.  
  TerrainOptions.billboardOcclusionTolerance now defaults to 0.2, not 0. 0 is too tight  
  over real terrain - device-measured at Grenoble, where it dropped POIs on slopes FACING the camera  
  even with the grazing term in. An app that set it explicitly is unaffected.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(labels): elevate a label anchor on the GPU when the CPU has no height for it  
  labelVsh calls the surface's own applyTerrain on the anchor and uses the result for both gl_Position  
  and the occlusion tap, so a label cannot disagree with the ground it stands on and a height the CPU  
  never resolved cannot misplace it. mapbox elevates symbols the same way - z_offset +  
  elevation(tile_anchor), a DEM texture fetch in the vertex shader (src/shaders/symbol.vertex.glsl).  
  A batch elevates against ONE tile's elevation uniforms, so labels needing them must arrive grouped by  
  tile, and the batch binds them with setupTerrainUniforms(prog, tileId, translate4(origin), false) -  
  which derives the elevation uv from the vertex frame it is handed, so the anchor needs no conversion.  
  The label occlusion texture moves to unit 2: elevation owns 1 and the node texture 5.  
  Only a label whose CPU height is UNKNOWN is grouped that way. An anchored one already carries the  
  right height and stays in the shared batch, drawn without the terrain flag. Splitting every label by  
  tile took label draws from 64 to ~450, and the per-frame cost tracks the draw count - which read as a  
  much slower pan in 3D. With the ancestor fix in place almost every label is anchored, so this is one  
  batch again and the GPU path is the fallback it should be.  
  A label on a bridge DECK keeps its CPU height: a span chord is CPU-only data and is not the terrain's.  
  attribs[3] carries that as bit 1 next to the offset mode in bit 0, set by one chordHeightAt test per  
  label at each anchoring site - the height function answers per vertex and cannot report which of its  
  two sources it used. This is mapbox's u_elevation_from_sea, per label rather than per layer, and it is  
  the mechanism a label on top of a 3D building will need.  
  An un-anchored label is no longer re-sampled every frame either. Only new elevation can change the  
  answer and markPendingLabelsDirty re-dirties the label when it arrives, so the retry never converged  
  and cost most of what elevUpdMs spent. The CPU height is now collision-only, which is mapbox's split  
  (getAtPointOrZero defaults to 0 and hides no symbol, src/terrain/elevation.ts).  
  The composed labelVsh was validated with glslangValidator in every define combination, since the host  
  suite does not compile GLSL. Still owed on device: the label draw count back at its old value, and  
  label draw ORDER, which per-tile batching changes for the un-anchored group - halos and plates depend  
  on it.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * chore(renderers): report which step of a 3D frame's prelude cost the time  
  The prelude section held five unrelated things and dominated a 3D pan at 400-700 ms a frame,  
  while its GPU time was 0.0 - so the question was never which draw but which CPU step, and no  
  counter answered it. Splits the section into head / terrain fill / occlusion depth / camera  
  clearance / paint tiles (itself split into the layer walk, its lock wait, the cover walk and the  
  push) / tail, printed as PROF PRELUDE on any frame where the section exceeds 20 ms.  
  Also counts what the terrain drape is doing, which the frame timings could not separate: why each  
  queued tile needs baking (blank, restack, stand-in, partial, stale), whether a stale one lost its  
  fingerprint or its masks, whether the fingerprint terms shared by every tile moved, what the cache  
  evicts, and what a bake spends per geometry type - a line drape visibly lags a fill one.  
  Behind MASSIF_FRAME_PROFILER and MASSIF_VT_RENDER_STATS, so a release build carries none of it.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): stop a drape tile re-baking every frame to replace masks it still had  
  Measured on the Crosscall while panning in 3D with nothing on screen changing: 184 drape tiles a  
  second reported stale, 64 bakes and 207 ms spent on them, and a queue that never drained because  
  8 bakes got through per frame against 23 newly stale. Every tile of the visible cover was being  
  invalidated every frame, and the counters said why: not the colour drape but its COVERAGE MASKS.  
  Two faults, both in the eviction pass:  
  The cache was permanently over budget BY CONSTRUCTION. maxBytes floors at MIN_ENTRIES colour  
  drapes, which at 1024 is the entire 96 MB - so the masks that no tile can be drawn without had  
  nothing left, and 21 drapes plus 20 masks measured 104 MB against 96 MB on every frame.  
  And the pass then took those masks. A mask is a quarter of a drape in bytes, so it looks like the  
  cheapest thing to drop, but the owner re-bakes the WHOLE tile to bring a missing one back: one  
  megabyte reclaimed, a 3 ms bake spent undoing it, 23 tiles a frame, for ever. The read that  
  decides the re-bake is isBaked, which is const and marks nothing used, so a live tile's mask  
  always looked idle to the LRU.  
  So the byte budget counts colour drapes only, mirroring the decision already made for the count,  
  and DrapeEviction states the rule the cache was missing: a mask is part of its drape and is not  
  evictable while that drape is cached. An orphan - a mask whose drape is already gone - goes first.  
  Masks stay bounded by the drapes they belong to, since one outlives its drape by a frame.  
  maplibre ties a drape's lifetime to its tile and releases every stack slot together  
  (render_to_texture.ts), mapbox caps the cache at 50 tiles and marks entries dirty rather than  
  evicting them; neither can express this fault, because neither lets a live tile lose part of its  
  drape.  
  Carries the cache's own occupancy report and eviction counters - the evidence for the above, and  
  what tells "the masks are evicted" from "the masks never baked" if it regresses.  
  On-device check owed beyond the numbers quoted: this is a render path and the host tests only  
  cover the rule.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(renderers): keep drawing while a tile set changes  
  A tile-set change rebuilds the label maps - 26 to 54 ms, 16 times a second while panning - and  
  refreshTiles held the SDK tile mutex across the whole of it. Measured on the Crosscall: 500 ms of  
  lock held per second, on a mutex the render thread takes for every draw and for each per-frame  
  terrain setter, with the cull thread never waiting for it. The mutex protects WHICH tiles are  
  visible, not the rebuild, so setVisibleTiles now runs outside it.  
  Also stops pushing the terrain paint tile list when it has not changed. It was sent every frame  
  per layer and takes the vt renderer's mutex - which a tile-set change holds for that same rebuild -  
  for a list that is only different when the terrain cover moves.  
  The render-thread side of the mutex is now timed (tileLockWaitMs, the other side of  
  refreshTilesLockNs), because the first attempt at this measured the wrong mutex and the wait simply  
  moved to another one.  
  Removes the PROF LABELELEV probe, which has answered: every label's height now comes from the  
  elevation texture (746565 of 746584 queries) and elevReanchor moves.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(layers): stop a composite re-resolving its style config every frame  
  collectDrapeLayers runs on the render thread once per frame per call site, and for every draw item  
  it resolved the slot's style config - walking the style and taking the decoder's mutex - then  
  applied it. Measured on the Crosscall: 41 ms of a 43 ms prelude, and 249 ms on the frame where the  
  cache below misses. The answer only moves when the view zoom or the style does, so it is memoised  
  per slot on (zoom, decoder config version).  
  The version matters for correctness rather than speed: a LIVE style parameter change refreshes  
  without reloading a tile or rebuilding a draw item, so a cache keyed on zoom alone would go on  
  serving the old colour. VectorTileDecoder::getConfigVersion is bumped by both notifyDecoderChanged  
  and notifyDecoderRefreshed, and the cache is dropped in rebuildDrawItems as well.  
  Second fault in the same walk: loadData held _sourceMutex while calling every child layer's  
  loadData - building their fetch sets and queueing their tasks. That is what the render thread then  
  waited behind, 277 ms of a 280 ms prelude, and it hung the map exactly while tiles were streaming  
  in. The mutex protects which children to load, not the loading.  
  Known gap, left deliberately: isUpdateInProgress, calculateRayIntersectedElements and  
  offsetLayerHorizontally walk their children under _sourceMutex the same way. None is measured as  
  hot, and reaching across these two locks on a hunch is what hung the app earlier in this work.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(vt): stop scanning every tile for a contact shadow the style never has  
  isGroundAOBakeable is asked once per drape layer per frame, to fingerprint the drape stack. It took  
  the renderer mutex - which a tile-set change holds for a whole label map rebuild - and then searched  
  every visible tile's every layer's every geometry for a POLYGON3DGROUND. Measured on the Crosscall  
  in an app that draws no 3D buildings at all: 211 ms of a 219 ms prelude.  
  ABSENCE is what costs. The search stops at the first hit, so a style that HAS extrusions answers  
  from one tile; a style with none walks everything to the end and answers no. The one short-circuit  
  ahead of it is the AO intensity, which defaults to 0.2 rather than 0 - so no app ever skipped it.  
  Answered at decode time instead, in the constructor loop that already settles hasSpanGeometry for  
  the same reason, and cached per renderer where the visible tiles are published and where setGroundAO  
  changes the intensity. The per-frame question is now one atomic read.  
  The shared loop may only stop once BOTH flags are settled - keeping the span pass's early break made  
  a span found first report no contact shadow, which the new test caught.  
  Carries the per-geometry-type bake counters that measured a line drape against a fill one: lines  
  cost about twice as much in total, 1.4x per draw on 1.5x the draws.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(layers): resolve a style slot's config once per zoom step, not once per frame  
  The config cache was keyed on the exact view zoom, which looked sufficient and was not. With 3D  
  terrain the camera clearance bound is rebuilt every frame from the ground height under the camera,  
  so panning over a slope drifts the zoom by a hair and misses every time. Measured on the Crosscall:  
    PROF PRELUDE: 254.0 ms | ... paintTiles 249.6 (layers 249.1 [lock 0.0]) tail 0.1  
  249 ms of WORK, no lock wait - every slot walked the style again in one frame, at ~8 ms a slot. In  
  2D the zoom is bit-stable while panning, which is the whole reason 2D never showed this.  
  The zoom is now floored to 1/16 of a level, and the config is resolved AT that zoom so the cached  
  value is the one the key describes. Flooring is exact where it has to be: mapnikvt selects which  
  rules match on the INTEGER zoom, and it cannot move. What steps is a config property that  
  interpolates with the view zoom, by at most one step - the same trade DrapeTuning::bakeZoomTerm  
  already makes for the bake fingerprint and applyConfig for its decode-affecting values.  
  This is a WORKAROUND for a resolve that is three orders of magnitude more expensive than the  
  reference renderers'. mapbox recalculates every layer every frame too (style.ts, layer.recalculate)  
  but walks a pre-parsed property array with rule and filter matching already done at tile build time,  
  so a call costs microseconds. Resolving the structure once per integer zoom and only the values per  
  step is the real fix, and it is a mapnikvt change.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(layers): stop the frame waiting on a layer's zoom range while its tiles refresh  
  getVisibleZoomRange took the LAYER's mutex, and refreshDrawData holds that same mutex across  
  refreshTiles - the whole tile-set change and label map rebuild, which the counters put at 250 to  
  780 ms per second while panning. Every drape collect tests the zoom range on every layer, so the  
  render thread queued behind a rebuild to read two floats.  
  Measured on the Crosscall, and the giveaway was that the cost ALTERNATED between the two walks a  
  frame does: 220 ms in the paint walk with 7.5 in the tail one, then 1.8 and 113.6 the other way  
  round. Whichever ran first paid, which is a wait rather than work - and the walk's own timers  
  (_sourceMutex, the config resolve, applyConfig) were all 0.0.  
  So the range gets its own mutex. Reading it cannot queue behind anything else the layer does, the  
  same way the composite keeps its child list readable without _sourceMutex.  
  prelude fell from 220 ms to 0.6-3.8 ms and the 3D pan went from 12.7 to 24.3 fps as the scene  
  settled. What remains on a tile-arrival frame is prepare at 102 ms: startFrame wants the vt  
  renderer's mutex, which the cull thread holds for buildLabelMaps. Same disease, one layer deeper,  
  and the fix there is to build the label maps off that lock rather than to move it.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * chore(renderers): split the prelude's drape walk into its lock, its config and its work  
  Four rounds of this section each blamed a different lock and each was right about the one it  
  measured, because fixing one moved the wait to the next. These are the splits that ended it: the  
  paint walk against the tail walk, and inside a walk the _sourceMutex wait, the style config resolve  
  and applyConfig separately - which is what showed 220 ms of wait with 0.0 in all three, and pointed  
  at the layer mutex none of them covered.  
  The temporary style-config probe comes out here, having answered: hits 1170, versionMiss 0,  
  zoomMiss 0. The quantised key holds, so the cost was never the resolve.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(vt): build the label maps off the renderer mutex, and only swap them under it  
  setVisibleTiles held the renderer mutex for the whole label map rebuild - 26 to 54 ms, 13 times a  
  second while panning - and startFrame on the render thread wants that same mutex. Measured on the  
  Crosscall after the other four locks in this path were cleared:  
    PROF SPIKE: frame 219.1 ms | prepare 102.5 layers 75.5 | tileSets 2 labelMaps 2 labelsAlloc 802  
  The rebuild splits cleanly. prepareLabelMaps does the two expensive phases - the per-label geometry  
  signatures and the merge that decides what is reused and constructs the rest, about 20 of those  
  ms - and writes NO renderer state: the signature maps are local, and every label it builds or  
  merges into is one nothing else can see yet. commitLabelMaps does the rest under the mutex: release  
  the faded, carry placement and opacity over, sort the draw and cull lists, swap them in. It runs  
  exactly where buildLabelMaps used to, between the tile surfaces and the render tiles.  
  The prepare works on a SNAPSHOT of the previous maps (shared_ptr copies, taken under the mutex), so  
  deinitializeRenderer clearing them on the render thread cannot pull the ground out. A generation  
  counter catches that case and redoes the prepare under the mutex - correct, and rare enough not to  
  matter. The skip guard is answered first from the tile list alone, so an unchanged tile set costs  
  neither the snapshot nor the prepare.  
  The layer filter is snapshotted too: it is set under the mutex and it decides which layers have  
  labels at all.  
  This is the SHARED label path - 2D rebuilds its labels the same way and gets the same change.  
  No host test: tests/vt cannot link GLTileRenderer (it needs GL), and what changed is which thread  
  holds what, not a rule. Type-checked with the NDK toolchain against the app's own compile flags.  
  The device check this owes, in BOTH modes: labels appear and disappear as tiles arrive, do not  
  flicker or reorder while panning, and a fast pan across many tile sets does not crash.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(vt): resolve a frame's view-projection once, not once per label  
  Label::setupCoordinateSystem snaps every point label's anchor to a quarter of the screen pixel grid  
  - that is what keeps glyphs sharp - and to do it it projected the anchor and un-projected the  
  snapped position. Both matrices are identical for every label in the frame, and it built them  
  inside the per-label loop: a 4x4 product and a 4x4 double INVERSE, for 3589 labels an interval on  
  the Crosscall. The line-run layout built the same product twice more, also per label.  
  They are resolved once now, with the rest of the frame's camera, where the frustum is already built  
  from that very product.  
  Found while comparing our label path against mapbox's symbol bucket, which was the plan - and the  
  comparison says NOT to port it here: mapbox keeps the glyph quads in a per-tile buffer and writes  
  only a projected position and an opacity per frame, but it also draws per tile. We already cache  
  the quads per label (buildPointVertexData), and our batching is by glyph ATLAS, which measured 64  
  draws against ~450 when labels were grouped per tile. The reuse idea that motivated the look is  
  dead for a different reason: the anchor snap is deliberately view-dependent, so a batch cannot be  
  reused across a pan however little else changed.  
  Of the label pass's ~127 ms/s, buildMs is ~35 and batchMs ~16; the rest is GL submission and the  
  per-frame upload of six arrays, two of which (anchor and normal) are N identical copies per label.  
  That is the next thing worth measuring.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(vt): stop building label normals a planar map never uploads  
  labelVsh declares aVertexNormal only under LIGHTING_VSH/LIGHTING_FSH, and the SDK compiles those in  
  for a NON-planar projection alone - TileRenderer::initializeRenderer sets the 2D lighting shader  
  only when the projection surface is not planar. renderLabelBatch already knew it and uploads the  
  normal buffer under `if (_lightingShader2D)`.  
  The BUILD did not. Every label filled one normal per vertex, every frame, on every planar map, and  
  none of it was ever uploaded: 3589 labels an interval on the Crosscall, ~40 vertices each, 12 bytes  
  a normal. The caller now says whether anything will read them.  
  This is half of what was scoped. The other half - moving the per-vertex label ANCHOR into a  
  per-label uniform table, since it too is N identical copies - does not fit: a batch is bounded by  
  32768 VERTICES rather than by label count, so it can hold ~800 labels, against an ES2 guarantee of  
  128 vec4 uniforms. Making it fit means flushing every ~64 labels, and per-tile label grouping  
  already measured 64 draws against ~450 here. The anchor stays per-vertex.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(vt): upload a label batch as one buffer, not five  
  A label draw cost 0.43-0.58 ms on the Crosscall against 20-46 us for a geometry draw, and the label  
  pass (renderLabels inside the 3D pass) was 60-81 ms a second of it. The difference is not the bytes  
  but the driver work around them: every label draw respecified FIVE separate buffers - positions,  
  offsets, normals, texture coordinates, attributes - each one an allocation and a chance to wait for  
  the previous frame's draw to finish reading it.  
  One buffer now, orphaned once with a null glBufferData and filled with a glBufferSubData per  
  attribute range, each range 4-byte aligned as every attribute here naturally is. Same bytes, same  
  batching by glyph atlas, same draw order, no shader change - the attributes are bound at their  
  offsets instead of at zero of their own buffer.  
  Found by timing the 3D pass against its own counters: layers3D was 48.8 ms a frame while  
  pass3DLabels2D/Geometry/Labels3D accounted for 65.6 ms an INTERVAL, and the composite's _sourceMutex  
  - the obvious suspect after five other locks in this work - measured 0.0. It was never a lock.  
  Carries the PROF LAYERS3D split that showed it.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * chore(vt): split the label pass into its sort, its glyph atlas lookup and its style block  
  The label pass cost 54-81 ms an interval while the per-label vertex build and the batch upload  
  accounted for 21-27 between them, and the rest was never measured. These three splits found it, and  
  the answer was none of the things that looked likely:  
    labelPass sortMs=0.2 patternMs=12.3 styleMs=98.8   (1611 labels, per interval)  
  The per-pass sort is free. getBitmapPattern - called per label, on the glyph map's mutex that tile  
  threads hold - is 12.3 ms, real but not the cost. The style block is 98.8 ms, and it contains a  
  renderLabelBatch: a mid-loop FLUSH, upload and draw, at a call site with no batch timer on it. So  
  most label draws have been attributed to the style evaluation all along, and batchMs only ever  
  counted the end-of-pass flushes.  
  Counters live in vt's own RenderStats rather than the SDK's FrameProfiler - a first attempt put them  
  in the latter, which would have made vt depend on all/native/utils.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * revert: "perf(vt): upload a label batch as one buffer, not five"  
  This reverts commit 56d0a4aa0, which never showed a benefit: the label pass cost per label was the  
  same before and after it, inside the scene-to-scene noise.  
  A first reading looked like a 4x REGRESSION (batchMs 39.6 ms an interval at 1611 labels against  
  8.5-8.7 at 1454-2038) and this revert was written to that. A second sample of the same build says  
  that reading was an outlier: 5.7, 12.4, 6.7 and 9.7 us a label across four intervals, against 5.8  
  and 4.3 before the change. One interval is not a measurement.  
  So the revert stands on the absence of a win rather than on harm - five clean glBufferData  
  respecifies are what the driver has always been given, and there is no evidence for changing it.  
  The premise was wrong regardless: a label draw is not expensive because of five uploads. There are  
  simply many more draws than labelDraws suggested, most of them mid-loop flushes hidden in the style  
  block.  
  Also times that flush as a batch rather than as style evaluation, which is what hid it: the split  
  banks the style work, the flush goes to labelBatchNs, and the clock restarts for the rest of the  
  block.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * chore(renderers): split the layers section, and time the mutex a tile-set change holds  
  The frame profiler's 'layers' section was one number, 74-151 ms a frame in the slow 3D intervals.  
  It now splits at the calls TileRenderer::onDrawFrame makes - state, lighting, prepare, and the four  
  draws - the same shape the 3D pass already had. The clock starts after the renderer lock, so the  
  wait for it stays in tileRendererLockNs and is not counted twice.  
  That split does not close: the section costs far more than the sum of its parts whenever a tile set  
  changes. layerRefreshHoldNs is the other side of it - how long the cull thread holds the LAYER's  
  mutex across refreshDrawData. It is measured on the CULL side deliberately, so a later fix to the  
  render side can be told apart from a mutex that was never contended in the first place.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(layers): stop a frame waiting on a tile-set change before it can draw  
  The base pass blocked on the LAYER's recursive mutex, which the cull thread holds across the whole of  
  refreshDrawData - 280-978 ms an interval, refreshTiles being nine tenths of it. Every drawn frame  
  wanted that mutex twice before drawing anything: once in readStyleEnvironment, then again in  
  getMapRenderer.  
  So both go. readStyleEnvironment guarded nothing - _tileDecoder is a const shared_ptr and its getters  
  lock themselves. _mapRenderer, _options and _touchHandler get a mutex of their own: they are written  
  once, in setComponents, and read by every frame. Lock order is only ever _mutex then _componentMutex,  
  never the reverse.  
  Removing just the first one changed NOTHING - the frame blocked a line later on the second. What the  
  pair is worth, per interval, with the cull-side hold measured alongside to prove the mutex was still  
  held as long as before:  
    hold 848 ms -> layers section 544 ms, 445 of it unaccounted for   (before)  
    hold 978 ms -> layers section  90 ms,  10 of it unaccounted for   (after)  
  The layers section is 4.2-13.1 ms a frame against 13.6-54.4, and the worst interval of the pan went  
  5.7 to 11.5 fps. No host test: this needs a live cull worker and a GL renderer, so the proof is the  
  counter, on a Crosscall in 3D.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * chore(renderers): split the sky section into its clear, its sky and its background  
  'sky' is 10-25 ms a frame and the largest section of a 3D frame now, but the CPU work in its span is  
  about fifteen uniform calls and a four-vertex quad. It also holds initializeRenderState, whose glClear  
  is the frame's FIRST GL call - where a driver with no free buffer blocks the CPU.  
  So the three are timed apart. If the time is in clearMs it is back-pressure, the frame waiting to be  
  presented, and there is nothing in the sky to fix; that is worth knowing before anyone optimises a  
  shader over it. 'sky' anti-correlating with how busy the rest of the frame is (layers 54.4 with sky  
  10.7, layers 4.2 with sky 16.0) is the reason to suspect it, not a reason to believe it.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(renderers): stop the 2D/3D switch deadlocking against a facade getter  
  updateTerrainFlatten ran under the renderer mutex, and on a switch it calls TerrainOptions::  
  setFlattened - which notifies option listeners, which reaches MapEventBridge::onMapMoved, the facade's  
  map-moved event, and from there application code. In AlpiMaps that is a JS handler posting  
  SYNCHRONOUSLY to the main thread, so:  
    GLThread  holds MapRenderer::_mutex (onDrawFrame)  waits for the main thread's monitor  
    main      holds that monitor                       waits for MapRenderer::_mutex, in  
                                                       BaseMapView::getZoom -> getViewState  
  From the ANR trace of a hang on the Crosscall, symbolised. It fires only when the auto-flatten rule  
  CHANGES its answer, which is why it is intermittent and specific to the switch.  
  So the notify happens off the mutex: the call moves out of the locked block, and the three inputs the  
  mutex owns - the parallax, the tilt and _cameraPlaced - are snapshotted together inside it instead, so  
  they still describe one camera. The rule's own logic is untouched (AutoFlattenTest covers it).  
  No host test: the path needs a GL thread and a SWIG director, so a test here would exercise std::mutex  
  rather than this. GetterLockTest pins the opposite direction of the same class of bug.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(renderers): let the app read the camera without waiting for a frame  
  BaseMapView's getters - zoom, tilt, rotation, focus, camera position - all went through  
  MapRenderer::getViewState, which takes the renderer mutex. That mutex is held for a whole drawn frame,  
  so an app reading the camera on its own thread waits for the frame, and if the frame is meanwhile  
  waiting on that thread the two never meet: the deadlock the previous commit fixes at its source.  
  Those five now read a snapshot published at the end of every frame (and by getViewState itself), behind  
  a mutex held only long enough to swap a shared_ptr. Up to one frame stale, which is what a camera value  
  handed to an application already was.  
  Not every caller: pan derives a DELTA from the focus, and moveTo reads the zoom while already holding  
  holdView - a snapshot there would move the map somewhere the caller did not ask for, so both read the  
  exact state. pan also read it twice for one delta; it now reads once.  
  Defence in depth, not the fix - the emit side is where the cycle was broken.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * chore(renderers): fix two DEM counters that were reporting nonsense  
  liveSum/resolvedSum were a .store() of one cache's size, and there is one ElevationTextureCache per  
  tile layer - so an empty cache overwrote a busy one and the log read 'live=0 resolved=0' in every  
  interval while uploads were plainly happening. Accumulated now, so only zero vs non-zero is meaningful,  
  which is the question: an empty elevation texture cache means the GPU has no displacement data and the  
  ground renders FLAT while labels still sit at terrain height.  
  encodeMs is renamed encodeWorkerMs. It is summed over the encode threads, so it can and does exceed the  
  interval - 13.3 s inside a 1.02 s interval - and reading it as latency per encode was wrong.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): show the relief that lands while the map stands still  
  Switch to 3D and hold still: the labels stand at terrain height over ground that is still flat, and the  
  relief only appears on the next gesture. Measured on the Crosscall - 55 DEM tiles encoded and 12  
  textures uploaded inside an interval that drew 11 frames in 32 SECONDS.  
  An encode finishes on the worker and queues a texture; the upload happens in beginFrame. So the upload  
  needs a frame, and on a still map nothing asks for one: ElevationManager's data arrival already asks  
  (MapRenderer sets a data-changed listener for exactly this), but the encode that follows it did not.  
  The queue then sits full until the user moves.  
  The cache now reports a texture waiting, and TileRenderer turns that into a requestRedraw - the same  
  shape as the ElevationManager hook beside it. Called on the encode worker, outside every lock the cache  
  holds, and requestRedraw coalesces, so a burst of encodes is still one frame.  
  No host test: this needs the encode worker and a GL upload.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * chore(terrain): count the live elevation texture caches  
  Each ElevationTextureCache runs ONE encode thread, so the encode work reported in an interval cannot  
  exceed it by much - and it does: 12.3 s of encode time inside a 1.10 s interval, which needs about  
  eleven encoders running at once. Either several are alive or the accounting is wrong, and the two have  
  opposite fixes.  
  A stale one is easy to keep by accident: TileRenderer drops the pointer when the elevation manager  
  changes, but the terrain texture provider handed to the vt renderer captured the shared_ptr BY VALUE,  
  so an old cache can outlive the reset with its thread still encoding tiles nobody will draw.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * chore(terrain): report what a shared elevation texture cache would have to serve  
  There are five of these alive, one per tile layer, each with its own encode thread over the SAME  
  elevation data - five encodes and five textures per DEM tile. Sharing one per ElevationManager is the  
  obvious answer, and mapbox keeps a single DEM store for every source; what decides whether it is that  
  simple is the detail level, which is set PER LAYER (a painted layer asks for more).  
    detailMask      bitmask of the levels in use. One bit and a single shared cache serves everyone;  
                    several and it has to be keyed by level, or every layer pays the painted layer's  
                    working set - 4x per level, which can cost more than the sharing saves.  
    detailClears    caches emptied by a level CHANGE. Each one re-encodes everything, so a layer that  
                    flips its level is a cause of slow DEM on its own.  
    texelsPerEncode padded texels an encode covers, to size ours against mapbox's 258 squared.  
  One clean single-encode reading in the last log says 242 ms for one encode on one thread, so the  
  700-1100 ms a tile read earlier was five threads' work over one interval, not one encode.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(terrain): encode a DEM tile once for the whole map, not once per layer  
  ElevationTextureCache was a TileRenderer member, so every tile layer had one - measured caches=5 on the  
  Crosscall, each with its own encode THREAD and its own textures over identical heights. Five encodes and  
  five uploads per DEM tile, five threads competing with the render thread, and ~11 textures live per  
  cache holding the same data. It is also why the terrain filled in fast on one cold start and slowly on  
  the next: five threads racing the renderer is high variance by nature.  
  MapRenderer owns one per ElevationManager now and TileRenderer borrows it, which is the model mapbox  
  uses (a single DEM store for every source). detailMask read 0x1 - every layer asks for the same detail  
  level - so there is nothing to key the cache by today.  
  Two things that had to move with it, because per-layer ownership was doing work five times:  
  - beginFrame runs ONCE a frame, from MapRenderer, ahead of the layer draws. Per layer it would spend  
    MAX_UPLOADS_PER_FRAME five times over and reset the per-frame resolution memo four times.  
  - drainContentChanges DRAINED, so the first layer to call it would have taken every change and the  
    other four would never invalidate their extrusion bases. It is getFrameContentChanges now, swapped  
    in by beginFrame and readable by all of them.  
  The detail level is requested rather than set: the frame takes the max over the layers and applies it in  
  the next beginFrame, because setDetailLevels clears the cache and a per-layer set would clear it on  
  every disagreement.  
  No host test: this needs a GL context and the encode worker. The counters to read are caches=1 and one  
  encode per DEM tile.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * chore(terrain): split one DEM encode into its three parts  
  With the cache shared there is one encode thread, so encodeWorkerMs is finally attributable - and it  
  says the encode BLOCKS rather than computes. Two intervals match their own duration to within 3 ms  
  (2156.5 against 2153, 5134.6 against 5131) while a clean single reading earlier was 242 ms. A 20x spread  
  on identical work is a wait, not a cost.  
  So the encode is split at its three steps: encodeTextureWithBorders, the Bitmap copy (the one mapbox  
  does not make), and the node texture with its own Bitmap. The suspicion is the texel sampler walking  
  neighbour grids that decode lazily on first touch - warm neighbours giving 242 ms and cold ones seconds -  
  but that is a guess, and this is what decides it.  
  encodeWorkerMs keeps meaning the whole encode: the three splits run off their own clock and must sum  
  to it.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * chore(terrain): print the DEM encode split  
  The counters landed a commit earlier without their log line, because MapRenderer.cpp was mid-edit in  
  another session and staging the file would have carried half-finished work along.  
    RenderStats: demEncode textureMs=.. bitmapMs=.. nodeMs=..  
  Only meaningful divided by the encodes in the same interval.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * chore(terrain): measure the box an edge node of the DEM node texture averages  
  The node texture IS the DEM encode: textureMs=0.0 bitmapMs=0.0 nodeMs=10354.6 for one encode. Neither  
  the texel sampler nor the Bitmap copy, which were the two guesses.  
  Inside it, an interior node is an O(1) lookup and an EDGE node is recomputed from the neighbours by  
  box-averaging boxX*boxY texels through a std::function. boxTexels is cells*width/nodes and is then  
  multiplied by how much coarser the neighbour is, on both axes - so a coarse neighbour grows the scan  
  quadratically, and there are 4n edge nodes. That would explain a 242 ms encode becoming 10 s when the  
  neighbours change, but it is arithmetic on constants I have not read, so this counts the box instead.  
  boxTexelsPerCall is the whole question: a few hundred and the scan is not it, a million and it is.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): keep the camera clear of the ground after a 2D/3D switch  
  TerrainOptions::CameraClearance never fired after a cold start plus a full 2D->3D switch in  
  mountains, and a tap landed far from the finger. Two causes, both "nothing re-reads the ground  
  once the camera has stopped":  
  - The clearance measures the ground under the CAMERA, and at a low tilt that point sits behind  
    the near plane: no visible tile covers it, nothing ever loads its DEM, and getDisplayHeight  
    answered sea level for good. The camera then looked clear of a shell built on sea level while  
    it was inside the mountain. The clearance block prefetches that one tile itself now  
    (ElevationManager::getTileForInternalPos).  
  - The lift was armed by a pan event only, and a 2D/3D switch tilts the camera down and raises  
    the ground under it without panning. CameraClearance::needsLift arms on any loss of height  
    since the last check - a tilt…

- due to [`17e2614`](https://github.com/massif-maps/MassifMaps/commit/17e26145c20f8013b774fcaaef7f55ef3cfb9911) - fade labels in 300 ms, as maplibre does, instead of a second *(PR [#257](https://github.com/massif-maps/MassifMaps/pull/257) by [@farfromrefug](https://github.com/farfromrefug))*:

  VectorTileLayer.labelBlendingSpeed defaults to 1/0.3 (300 ms fade) instead of  
  1.0. An app that wants the old one-second fade calls setLabelBlendingSpeed(1).

- due to [`24cc825`](https://github.com/massif-maps/MassifMaps/commit/24cc825bc7c879462ceb77e49a7e4904bc9bedc3) - publish the routing library as com.massifmaps:valhalla-routing *(PR [#258](https://github.com/massif-maps/MassifMaps/pull/258) by [@farfromrefug](https://github.com/farfromrefug))*:

  the declared Maven coordinate moves from com.akylas.routing:valhalla-routing  
  to com.massifmaps:valhalla-routing. The JitPack coordinate  
  com.github.massif-maps:MassifMaps-valhalla-routing is unchanged.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * fix(docs): capture feature screenshots from the bench at a caller-given camera  
  The script opened the launcher activity, which is the example gallery, not the demo bench.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * build(style-tools): regenerate the property table for text-callout-band-follow  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  ---------

- due to [`7929567`](https://github.com/massif-maps/MassifMaps/commit/79295677e0dddd0f1d275002f77ab9fca295615d) - fly the camera the way mapbox does, in screenfuls and on an eased clock *(PR [#252](https://github.com/massif-maps/MassifMaps/pull/252) by [@farfromrefug](https://github.com/farfromrefug))*:

  every flyTo pulls back ~1.6 zoom levels less and eases in and out, where it  
  used to run at constant speed. getFlightProgress() reports the eased k, not raw elapsed time.  
  For the old motion pass FLIGHT_EASING_LINEAR - the arc itself was a units bug and has no  
  opt-out. all/modules/ui/BaseMapView.i: the facade's flyTo gained a 7th argument, the CSS  
  curve name; it is optional, so six-argument calls still work.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs: default to no comment, and cap the rest at two lines  
  The three-line rule was already there and did not hold: the flyTo work landed eight-line  
  class docs and six-line rationale blocks above the code they described. What was missing is  
  the bias - a comment is the exception, not the default - and the pass that catches it, so  
  state both.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  ---------

- due to [`45d77dd`](https://github.com/massif-maps/MassifMaps/commit/45d77dd7548badc47e6f289cb6c8381dcfdbf9bf) - skip vertical building corner rounding unless a style asks for it *(PR [#268](https://github.com/massif-maps/MassifMaps/pull/268) by [@farfromrefug](https://github.com/farfromrefug))*:

  a style with building-edge-radius > 0 no longer rounds  
  the vertical corners where two walls meet. Set building-edge-corners: 1  
  in the Map block to keep them (mapbox draws them at every zoom).

- due to [`2d8a19d`](https://github.com/massif-maps/MassifMaps/commit/2d8a19d35f938fba13f2d5f2ef2af6d018efa57b) - add the Massif style family: streets, outdoor, topo, hybrid and e-ink *(PR [#270](https://github.com/massif-maps/MassifMaps/pull/270) by [@farfromrefug](https://github.com/farfromrefug))*:

  water_min_zoom defaults to 14 and the Massif layer ids and  
  parameters changed; an app or extending project overriding them must follow.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * fix(styles): draw a child project's new rules instead of dropping them  
  A converted project names every attachment in `layers`, and a bare entry was  
  the only one that took attachments no other entry claimed. So a child  
  project's new rules (Massif's custom_cycleway example) compiled and never  
  drew: `extends` replaces `layers` whole. A layer with no bare entry now draws  
  its unclaimed attachments with its topmost entry, after that entry's own.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * feat(styles): add an OpenStreetMap-coloured Massif, as a child project example  
  examples/osm re-skins the family with OpenStreetMap Carto's colours as  
  Alpimaps' OSM style has them: the palette redeclared, Massif's widths kept,  
  and the tracks replaced by brown ones dashed by tracktype. The style preview  
  offers it beside the custom example (?project=osm).  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * fix(styles): give Massif Standard's POI colours, early parks and a white e-ink page  
  - POI discs take Standard's day colours; named parks and gardens show from  
    z14 over the sights, viewpoints lowest; a viewpoint keeps nature's green  
    and its glyph at every zoom; a ruin keeps the castle glyph.  
  - 3D buildings default to 5 m where Alpimaps' tiles leave render_height out.  
  - E-ink: the page is white and unlit (landuse told by its edge, emissive 1),  
    small roads uncased below z14, water in Alpimaps' dashes, streams on a pale  
    bed so they are not read as tracks.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * feat(styles): make Massif's streams and cycleways findable, and e-ink roads lighter  
  - streams in their own darker blue, three times wider from z12; cycleways  
    from z13  
  - labels fade behind 3D buildings as Standard's do (text-occlusion-opacity:  
    line and natural labels 0, road numbers 0.1)  
  - a kindergarten in the parks' green  
  - each road class its own casing key, so an extending style can case a  
    primary as OSM does  
  - a `lighting` parameter: 0 draws every colour as stated (e-ink's default)  
  - e-ink: small roads fade from grey to white over z12-13 as their casing  
    grows; half-clear 3D buildings from z15  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * feat(styles): match Alpimaps' OSM look in the OSM example project  
  Alpimaps' sheets are LESS, where the last declaration of a variable wins, so  
  the example now takes those: brown tracks under white dashes by tracktype,  
  red footways, OSM casings per road class, Alpimaps' water, pitch, pedestrian  
  areas and cycleways, bare POI glyphs, buildings from z15, and no lighting.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * fix(style-cli): draw a converted circle at its size and never hide it under a label  
  A MapLibre circle-radius was carried as marker-width, a diameter, so every circle drew at  
  half its size; a circle also never collides in MapLibre.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * feat(styles): light Massif's buildings as Standard does and invert e-ink at night  
  - family.json carries Standard's day lights, so walls get the building light model; night  
    buildings take Standard's tone; ground AO fades in over z17-18  
  - e-ink inverts at night: black page, white ink  
  - OSM example: zoom-ramped road colours, white tertiary, wood/scrub/wetland/rock textures,  
    thicker tracks, day/night cycle  
  - low landcover crossfades over z7.8-8; river labels from z11, smaller water labels  
  - style preview: Standard's sun through the day  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * fix(datasources): draw a merged source past its max zoom instead of dropping it  
  MergedMBVTTileDataSource asked each source only within its own zoom range, so the  
  bathymetry archive (z0-6) vanished at z7 while the style crossfades its landcover over  
  z7.8-8. Past a source's max zoom its last tile is now cut into the requested one: each  
  layer's extent divided, each geometry's first MoveTo shifted, features outside dropped.  
  The preview server does the same cut.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * fix(mapnikvt): keep a colour ramp nested in another ramp a colour  
  A curve over colours yields the colour's packed integer; a step or linear around it read  
  that as a number, so an extending style's `@primary: linear(...)` inside a zoom step drew  
  the road near-white.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * fix(renderers): shade shadows and night buildings as Mapbox Standard does  
  - the shadow strength was the direct light's linear share applied to sRGB colours: a full  
    shadow kept 0.81 of the ground where gl-js keeps 0.91. It is now converted to sRGB.  
  - under a day cycle, once the sun is down, buildings are lit by Standard's moon  
    ([270, 20]) over its night ambient, so walls and roofs no longer take one tone.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * feat(style-cli): upright line-center labels and gl-js wall AO under lights  
  - an upright line-center label becomes one billboard at the line's middle, wrapped at  
    text-max-width, instead of a repeat along the line  
  - under `lights`, a wall's foot takes gl-js's faux AO as the vertical gradient (over 6 m)  
    rather than the unlit gradient gl-js ignores in that mode  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * feat(styles): OSM Carto roads at low zoom, lake names and opaque buildings in Massif  
  - `road_osm_low` param: OSM Carto's low-zoom widths and outlines; the OSM example sets it  
  - lake names upright at the lake's middle, wrapped; rivers named from z9; softer water labels  
  - cliffs in streets and hybrid; building_opacity defaults to 1; building glow night-only  
  - style preview: shadows off by default behind a checkbox  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * fix(styles): keep tunnels and bridges on Massif's low-zoom roads  
  Below z12 the road layers skipped every tunnel and bridge while the tunnel and bridge  
  layers start at z12, so a motorway broke off at each tunnel. They now draw as the road  
  they carry until then; tracks, trails and MTB lines continue across bridges, and tunnel  
  cycleways are drawn. `tunnel_min_zoom` (12) sets where a tunnel takes its dashed look;  
  the OSM example sets 13, OSM Carto's, and draws its tracks in tunnels too.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  ---------

- due to [`fab6208`](https://github.com/massif-maps/MassifMaps/commit/fab6208fdd7fb7ee304c601490e2cf0b56cecf5e) - draw labels about 14% faster in 3D cities *(PR [#265](https://github.com/massif-maps/MassifMaps/pull/265) by [@farfromrefug](https://github.com/farfromrefug))*:

  TileLayer.setLabelOcclusionDepth, isLabelOcclusionWanted and  
  renderLabelOcclusionDepth are gone from the bindings. They were render internals  
  that leaked through SWIG; nothing replaces them.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  ---------

- due to [`6637757`](https://github.com/massif-maps/MassifMaps/commit/66377577beb74a5c90355be112c0a46960b761a1) - shade the Massif relief on the SDK as MapLibre does *(PR [#279](https://github.com/massif-maps/MassifMaps/pull/279) by [@farfromrefug](https://github.com/farfromrefug))*:

  the outdoor and topo relief is stronger and MapLibre-shaded (standard method).  
  Apps building their HillshadeRasterTileLayer from massif:sdk-layer get contrast/heightScale/  
  hillshadeMethod/colours instead of exaggeration 0.35 / opacity 0.55.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  * fix(layers): draw a composite hillshade slot in the colours its style sets  
  The CartoCSS translator writes a colour as rgb()/rgba(), and CompositeVectorTileLayer read only  
  #hex, so every hillshade-shadow/-highlight/-accent-color fell back to black/white/black: grey  
  relief, up to 1.44x darker than the Massif brown. That was the remaining 1.5x against MapLibre.  
  The slot now reads colours through mvt::tryParseColor, un-premultiplying its rgba() result.  
  The facade reads only #hex, so massif:sdk-layer writes its colours as #rrggbb.  
  Measured in the style preview against MapLibre 5.24 at the same scale and DEM zoom  
  (Matterhorn, z13.2): 0.995 of MapLibre's relief over white, 0.947 in the full outdoor style.  
  The earlier SDK/MapLibre numbers were taken at tileDrawSize 256, a zoom level off from  
  MapLibre's scale; docs corrected. The SDK keeps flooring the DEM zoom where MapLibre rounds.  
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>  
  ---------


### New Features
- [`c56c62c`](https://github.com/massif-maps/MassifMaps/commit/c56c62c04fbc0249348b0600e6e6b48f622c33c4) - require OpenGL ES 3.0 and drop the ES 2.0 fallbacks *(PR [#142](https://github.com/massif-maps/MassifMaps/pull/142) by [@farfromrefug](https://github.com/farfromrefug))*
- [`7fbd68e`](https://github.com/massif-maps/MassifMaps/commit/7fbd68e566a9960c71bb7c0354a1ba19a572be5a) - compile the shaders as GLSL ES 3.00, keeping app shaders unchanged *(PR [#143](https://github.com/massif-maps/MassifMaps/pull/143) by [@farfromrefug](https://github.com/farfromrefug))*
- [`c728005`](https://github.com/massif-maps/MassifMaps/commit/c728005aac50def22efdf7a05d09177a2945078c) - **labels**: repeat a road shield along its road without turning it with the road *(PR [#150](https://github.com/massif-maps/MassifMaps/pull/150) by [@farfromrefug](https://github.com/farfromrefug))*
- [`44d9530`](https://github.com/massif-maps/MassifMaps/commit/44d9530673aa70e081d257321a1375a4e945361d) - **terrain**: judge tile detail by the ground's real height, and bound grazing-angle coarsening *(PR [#151](https://github.com/massif-maps/MassifMaps/pull/151) by [@farfromrefug](https://github.com/farfromrefug))*
- [`4d579e3`](https://github.com/massif-maps/MassifMaps/commit/4d579e3ce3cee26e47c1657a0e01bb4f80ff13ba) - **android**: replace the generated Java enums with @IntDef int constants *(PR [#152](https://github.com/massif-maps/MassifMaps/pull/152) by [@farfromrefug](https://github.com/farfromrefug))*
- [`baa2024`](https://github.com/massif-maps/MassifMaps/commit/baa20247e101c6b93f76bd235d10acf074bf8732) - **website**: redraw the brand mark as a two-peak massif *(PR [#158](https://github.com/massif-maps/MassifMaps/pull/158) by [@farfromrefug](https://github.com/farfromrefug))*
- [`eecd5ed`](https://github.com/massif-maps/MassifMaps/commit/eecd5ed237a8458c01bed1687ae57467c6c6a1f1) - **api**: the facade API — property table, specs, events, camera, C ABI and generated bindings *(PR [#153](https://github.com/massif-maps/MassifMaps/pull/153) by [@farfromrefug](https://github.com/farfromrefug))*
- [`0870eca`](https://github.com/massif-maps/MassifMaps/commit/0870eca705e2493d942f3dd8b4f6058f87882020) - **api**: tell an app what moved the map, and end a movement exactly once *(PR [#165](https://github.com/massif-maps/MassifMaps/pull/165) by [@farfromrefug](https://github.com/farfromrefug))*
  - *addresses issue [#163](https://github.com/massif-maps/MassifMaps/issues/163) opened by [@farfromrefug](https://github.com/farfromrefug)*
- [`cedd9f9`](https://github.com/massif-maps/MassifMaps/commit/cedd9f98983042b0d15e1ed2e3be52ec96444358) - **api**: facade coverage for alpimaps, and a Mapnik XML style that keeps everything *(PR [#170](https://github.com/massif-maps/MassifMaps/pull/170) by [@farfromrefug](https://github.com/farfromrefug))*
- [`cef8ec2`](https://github.com/massif-maps/MassifMaps/commit/cef8ec224916bd7e248cef1fa457fe6beec646d4) - **api**: reach a style parameter as a property, and spell a path readably *(PR [#172](https://github.com/massif-maps/MassifMaps/pull/172) by [@farfromrefug](https://github.com/farfromrefug))*
- [`6ce7137`](https://github.com/massif-maps/MassifMaps/commit/6ce71374ddcd81fa9f5fca76e2a22dd4925ae9d1) - **branding**: one generated mark for the site, the GitHub avatar and both demo icons *(PR [#173](https://github.com/massif-maps/MassifMaps/pull/173) by [@farfromrefug](https://github.com/farfromrefug))*
- [`1ea2116`](https://github.com/massif-maps/MassifMaps/commit/1ea2116eb4be5e47224a985cc2295d963cad3f5c) - **sky**: light the sky by scattering, and fog the whole frame from one model *(PR [#174](https://github.com/massif-maps/MassifMaps/pull/174) by [@farfromrefug](https://github.com/farfromrefug))*
  - *addresses issue [#161](https://github.com/massif-maps/MassifMaps/issues/161) opened by [@farfromrefug](https://github.com/farfromrefug)*
- [`4be100a`](https://github.com/massif-maps/MassifMaps/commit/4be100ae730883d9fd8743a2cbb7f351fb62bdc1) - **terrain**: reach the horizon when zoomed out, and render flat when 3D buys nothing *(PR [#176](https://github.com/massif-maps/MassifMaps/pull/176) by [@farfromrefug](https://github.com/farfromrefug))*
  - *addresses issue [#156](https://github.com/massif-maps/MassifMaps/issues/156) opened by [@farfromrefug](https://github.com/farfromrefug)*
- [`0874388`](https://github.com/massif-maps/MassifMaps/commit/0874388a7aa4ad01a380257718c88cc3546a26c4) - **examples**: switch between a flat map and 3D terrain as one animation *(PR [#181](https://github.com/massif-maps/MassifMaps/pull/181) by [@farfromrefug](https://github.com/farfromrefug))*
- [`38eaed6`](https://github.com/massif-maps/MassifMaps/commit/38eaed6fb5a75d0b61ca7b76998283d9219b8840) - **tools**: convert a MapBox style to CartoCSS from the command line *(PR [#183](https://github.com/massif-maps/MassifMaps/pull/183) by [@farfromrefug](https://github.com/farfromrefug))*
- [`e7c89b3`](https://github.com/massif-maps/MassifMaps/commit/e7c89b3d6bbfe4b7de81fa5af8c5632154f90841) - **api**: adopt a VectorDataSource, so a data source can live in an extension *(PR [#169](https://github.com/massif-maps/MassifMaps/pull/169) by [@farfromrefug](https://github.com/farfromrefug))*
- [`aafc81b`](https://github.com/massif-maps/MassifMaps/commit/aafc81b9b2b37c2a9b2506439e623c92159ad577) - **terrain**: switch a map between 2D and 3D as one state, and let the app lead it *(PR [#185](https://github.com/massif-maps/MassifMaps/pull/185) by [@farfromrefug](https://github.com/farfromrefug))*
- [`365d048`](https://github.com/massif-maps/MassifMaps/commit/365d048a6f0748056b3851b2f3c6996839679f81) - **api**: let a data source be implemented in C, and hand over pixels instead of a PNG *(PR [#187](https://github.com/massif-maps/MassifMaps/pull/187) by [@farfromrefug](https://github.com/farfromrefug))*
- [`6dd6e93`](https://github.com/massif-maps/MassifMaps/commit/6dd6e9371131eb853e2849341d406eb53979f4a6) - **api**: let a PMTiles source be built from JSON, like every other tile source *(PR [#192](https://github.com/massif-maps/MassifMaps/pull/192) by [@farfromrefug](https://github.com/farfromrefug))*
- [`59a09d2`](https://github.com/massif-maps/MassifMaps/commit/59a09d2016155b282c220fc0e6f8f4d51aeda970) - **docs**: publish a surface API reference, drop the CARTO guides, and rebuild the site's navigation *(PR [#193](https://github.com/massif-maps/MassifMaps/pull/193) by [@farfromrefug](https://github.com/farfromrefug))*
- [`964b203`](https://github.com/massif-maps/MassifMaps/commit/964b20387747441db9d46bd3543646c32bc316f1) - **vt**: round a building's vertical corners, not only its roof edge *(PR [#201](https://github.com/massif-maps/MassifMaps/pull/201) by [@farfromrefug](https://github.com/farfromrefug))*
- [`4334d5e`](https://github.com/massif-maps/MassifMaps/commit/4334d5e67fac4ad0160e2214543128fddc57d96e) - **vt**: one CartoCSS rule for a line and its casing (line-border-*) *(PR [#202](https://github.com/massif-maps/MassifMaps/pull/202) by [@farfromrefug](https://github.com/farfromrefug))*
- [`27f2694`](https://github.com/massif-maps/MassifMaps/commit/27f26942f857640623ff6ee44e825209c42e2c63) - **tools**: give a converted POI label a free side to sit on, or a font glyph for an icon *(PR [#215](https://github.com/massif-maps/MassifMaps/pull/215) by [@farfromrefug](https://github.com/farfromrefug))*
- [`2b95d13`](https://github.com/massif-maps/MassifMaps/commit/2b95d1394c4c9d2cd1088b1a01a062649d3837a7) - **vt**: keep a converted style's per-layer extrusion emissive *(PR [#220](https://github.com/massif-maps/MassifMaps/pull/220) by [@farfromrefug](https://github.com/farfromrefug))*
- [`454fcf7`](https://github.com/massif-maps/MassifMaps/commit/454fcf7401ab0f67d540663c529b44cf15f5c438) - **demo**: read device logs, crashes and frames without dumping them into context *(commit by [@farfromrefug](https://github.com/farfromrefug))*
- [`d9facec`](https://github.com/massif-maps/MassifMaps/commit/d9facecb8e4f15311d6bab74c55a2202c470816b) - **terrain**: 3D bridges, opt-in via TerrainOptions.bridges3DEnabled, with decks that stay on their road *(PR [#231](https://github.com/massif-maps/MassifMaps/pull/231) by [@farfromrefug](https://github.com/farfromrefug))*
- [`6da011d`](https://github.com/massif-maps/MassifMaps/commit/6da011de78e93b804bb44984d566d716b61a247d) - **web**: render the map in a browser, and preview a style on the docs site *(PR [#233](https://github.com/massif-maps/MassifMaps/pull/233) by [@farfromrefug](https://github.com/farfromrefug))*
- [`be2bc3c`](https://github.com/massif-maps/MassifMaps/commit/be2bc3c35b80d134f4983bd84113ff498944a6f7) - **api**: composite external sources over the string API *(PR [#245](https://github.com/massif-maps/MassifMaps/pull/245) by [@farfromrefug](https://github.com/farfromrefug))*
- [`797d01a`](https://github.com/massif-maps/MassifMaps/commit/797d01ac92fffdde1ab9079e89ea4df89d338a11) - **labels**: size a label plate instead of padding it *(PR [#248](https://github.com/massif-maps/MassifMaps/pull/248) by [@farfromrefug](https://github.com/farfromrefug))*
- [`28083d2`](https://github.com/massif-maps/MassifMaps/commit/28083d251fc984a0c2845edaf7b3b27f77b22115) - **styles**: ship Massif Streets, a MapLibre style the SDK draws the same way *(PR [#249](https://github.com/massif-maps/MassifMaps/pull/249) by [@farfromrefug](https://github.com/farfromrefug))*
- [`937e61e`](https://github.com/massif-maps/MassifMaps/commit/937e61ec6229ad3bc895d538e61f10d5ed012c5a) - **labels**: let a distant label shrink the way maplibre's does, and match Standard's POI palette *(PR [#251](https://github.com/massif-maps/MassifMaps/pull/251) by [@farfromrefug](https://github.com/farfromrefug))*
- [`b1405ca`](https://github.com/massif-maps/MassifMaps/commit/b1405ca9246ea81c6eef3fc1c933fe5022bcfcbf) - **demo**: show the 2D/3D switch on a composite layer an app would actually ship *(PR [#253](https://github.com/massif-maps/MassifMaps/pull/253) by [@farfromrefug](https://github.com/farfromrefug))*
- [`a3ff0f3`](https://github.com/massif-maps/MassifMaps/commit/a3ff0f34946748522014bb19421ed72e57e51eb2) - **terrain**: draw 3D terrain, buildings, labels and shadows on the globe *(PR [#254](https://github.com/massif-maps/MassifMaps/pull/254) by [@farfromrefug](https://github.com/farfromrefug))*
- [`b31d8c5`](https://github.com/massif-maps/MassifMaps/commit/b31d8c5800acfb174364f751003b626bb85e4064) - **terrain**: 2D/3D switch, terrain & label fixes, and a geo-three terrain cut for the peak finder *(PR [#255](https://github.com/massif-maps/MassifMaps/pull/255) by [@farfromrefug](https://github.com/farfromrefug))*
- [`275abe4`](https://github.com/massif-maps/MassifMaps/commit/275abe47809d661d50ddfdde2f6bdecefaa5e60d) - **web**: ship the web SDK on npm, and run every example live on the site *(PR [#260](https://github.com/massif-maps/MassifMaps/pull/260) by [@farfromrefug](https://github.com/farfromrefug))*
- [`3e3944b`](https://github.com/massif-maps/MassifMaps/commit/3e3944bd06bb3d7600d921b3dfb00803cdec4435) - **api**: emit celestial.clicked, text-callout-anchor-visible, signed handle fix *(PR [#267](https://github.com/massif-maps/MassifMaps/pull/267) by [@farfromrefug](https://github.com/farfromrefug))*
- [`2d8a19d`](https://github.com/massif-maps/MassifMaps/commit/2d8a19d35f938fba13f2d5f2ef2af6d018efa57b) - **styles**: add the Massif style family: streets, outdoor, topo, hybrid and e-ink *(PR [#270](https://github.com/massif-maps/MassifMaps/pull/270) by [@farfromrefug](https://github.com/farfromrefug))*
- [`2376cd0`](https://github.com/massif-maps/MassifMaps/commit/2376cd0677f81da5ad7319a0a242d17823720fb3) - **celestial**: constellation artwork on the sky, and taps on empty sky reported as sky.clicked *(PR [#272](https://github.com/massif-maps/MassifMaps/pull/272) by [@farfromrefug](https://github.com/farfromrefug))*
- [`a9f1cf7`](https://github.com/massif-maps/MassifMaps/commit/a9f1cf78b9adbe05137f6059be46726c0729e8ae) - **styles**: resolve map legends from the live CartoCSS style *(PR [#271](https://github.com/massif-maps/MassifMaps/pull/271) by [@farfromrefug](https://github.com/farfromrefug))*
- [`2507c2e`](https://github.com/massif-maps/MassifMaps/commit/2507c2e13f7081539ac35e61cc5f05bb5d916e1b) - **examples**: run the peak finder on Android, iOS and NativeScript *(PR [#275](https://github.com/massif-maps/MassifMaps/pull/275) by [@farfromrefug](https://github.com/farfromrefug))*
- [`7af9872`](https://github.com/massif-maps/MassifMaps/commit/7af98720814f92202f1291317e836788ab6a10b0) - **styles**: publish the Massif styles for MapLibre and the SDK, and draw every example with them *(PR [#276](https://github.com/massif-maps/MassifMaps/pull/276) by [@farfromrefug](https://github.com/farfromrefug))*
- [`3a2f257`](https://github.com/massif-maps/MassifMaps/commit/3a2f257a2044c8fb9124cb6c9dda6a8cd571f9eb) - **web**: ship a full variant with routing, geocoding and offline packages *(PR [#282](https://github.com/massif-maps/MassifMaps/pull/282) by [@farfromrefug](https://github.com/farfromrefug))*
- [`39dee92`](https://github.com/massif-maps/MassifMaps/commit/39dee92344d809da0c6ca9f10cda816a3e00e862) - **examples**: run live maps on the feature pages, with sky, label and maneuver-arrow examples *(PR [#286](https://github.com/massif-maps/MassifMaps/pull/286) by [@farfromrefug](https://github.com/farfromrefug))*
- [`af19747`](https://github.com/massif-maps/MassifMaps/commit/af19747d7cdf98354ccc2d9733ad1bc259fd7bac) - **vt**: draw translucent roads once, so the Massif hybrid shows no disc at every join *(PR [#295](https://github.com/massif-maps/MassifMaps/pull/295) by [@farfromrefug](https://github.com/farfromrefug))*

### Bug Fixes
- [`a6d0582`](https://github.com/massif-maps/MassifMaps/commit/a6d05826fc69607d587009541b37d7007da1ea91) - **ios**: restore the iOS build broken by a mistyped data source import *(PR [#141](https://github.com/massif-maps/MassifMaps/pull/141) by [@farfromrefug](https://github.com/farfromrefug))*
- [`d113a7a`](https://github.com/massif-maps/MassifMaps/commit/d113a7aa8df3357d3fab1767dd22c614ede92148) - **ui**: keep onMapStable firing - checkMapStable state had no recovery path *(PR [#164](https://github.com/massif-maps/MassifMaps/pull/164) by [@farfromrefug](https://github.com/farfromrefug))*
  - *fixes issue [#162](https://github.com/massif-maps/MassifMaps/issues/162) opened by [@farfromrefug](https://github.com/farfromrefug)*
- [`37d6f09`](https://github.com/massif-maps/MassifMaps/commit/37d6f092d86812f0982bbbf39ac40b8985a7dc7c) - **terrain**: draw a frame when elevation lands, instead of waiting for a gesture *(PR [#166](https://github.com/massif-maps/MassifMaps/pull/166) by [@farfromrefug](https://github.com/farfromrefug))*
- [`f61634f`](https://github.com/massif-maps/MassifMaps/commit/f61634fb3a52574d11c46acadcc18a7d06974d04) - **api**: release the map VIEW id on close, so a reopened screen gets its own camera *(PR [#167](https://github.com/massif-maps/MassifMaps/pull/167) by [@farfromrefug](https://github.com/farfromrefug))*
- [`a4e82ef`](https://github.com/massif-maps/MassifMaps/commit/a4e82ef3c451ed5c956e64faa4051af2d08ed4ee) - **docs**: let the Android javadoc build resolve @IntDef without androidx *(commit by [@farfromrefug](https://github.com/farfromrefug))*
- [`0d68637`](https://github.com/massif-maps/MassifMaps/commit/0d686376f4a7f20a90b4722a1f7c3d16b80d10c5) - **docs**: unbreak the iOS API reference - jazzy dies on mustache 1.1.3 *(commit by [@farfromrefug](https://github.com/farfromrefug))*
- [`7779c5b`](https://github.com/massif-maps/MassifMaps/commit/7779c5b47fff64b930189874aa5cfa8754ad709f) - **renderers**: stop dereferencing a null GLResourceManager when a frame outlives the surface *(PR [#180](https://github.com/massif-maps/MassifMaps/pull/180) by [@farfromrefug](https://github.com/farfromrefug))*
  - *fixes issue [#178](https://github.com/massif-maps/MassifMaps/issues/178) opened by [@farfromrefug](https://github.com/farfromrefug)*
- [`83fdd1b`](https://github.com/massif-maps/MassifMaps/commit/83fdd1bc3ed30a934af6e6c9de4fb3b804ea15aa) - **cartocss**: drop the point-* placement properties a point can never honour *(PR [#182](https://github.com/massif-maps/MassifMaps/pull/182) by [@farfromrefug](https://github.com/farfromrefug))*
- [`29afa98`](https://github.com/massif-maps/MassifMaps/commit/29afa98195ee932e6478028bf93f946c05849d37) - **terrain**: draw a no-drape layer in its style position, not over the whole drape *(PR [#184](https://github.com/massif-maps/MassifMaps/pull/184) by [@farfromrefug](https://github.com/farfromrefug))*
  - *fixes issue [#175](https://github.com/massif-maps/MassifMaps/issues/175) opened by [@farfromrefug](https://github.com/farfromrefug)*
- [`996346a`](https://github.com/massif-maps/MassifMaps/commit/996346ad02b16e49ea211d233400ed222bd425c2) - **layers**: stop a style whose #contour block only styles lines from crashing *(PR [#188](https://github.com/massif-maps/MassifMaps/pull/188) by [@farfromrefug](https://github.com/farfromrefug))*
- [`1aacdd9`](https://github.com/massif-maps/MassifMaps/commit/1aacdd9b0e49d10d5f158170e24de27a4144b958) - **styles**: load a style with deeply nested expressions instead of hanging on it *(PR [#194](https://github.com/massif-maps/MassifMaps/pull/194) by [@farfromrefug](https://github.com/farfromrefug))*
- [`5698945`](https://github.com/massif-maps/MassifMaps/commit/56989451ed4a7cf9fdc8c607ba7b5bc18c841f76) - **labels**: keep a plate and a halo at their own colour while a label fades in *(PR [#199](https://github.com/massif-maps/MassifMaps/pull/199) by [@farfromrefug](https://github.com/farfromrefug))*
- [`d28e972`](https://github.com/massif-maps/MassifMaps/commit/d28e972379681c6c771bfcf601f0dfd4b3e2030b) - **api**: retain a facade event listener's director, not just its C++ half *(PR [#204](https://github.com/massif-maps/MassifMaps/pull/204) by [@farfromrefug](https://github.com/farfromrefug))*
- [`9fcbc0e`](https://github.com/massif-maps/MassifMaps/commit/9fcbc0e6a6e47ad1609de54f7ca61e98d48e65ac) - **vt**: keep a line label's text on the line it names *(PR [#205](https://github.com/massif-maps/MassifMaps/pull/205) by [@farfromrefug](https://github.com/farfromrefug))*
- [`bba34ac`](https://github.com/massif-maps/MassifMaps/commit/bba34aceef53dc48028fe16377a35fec0468529c) - **terrain**: draw a 3D bridge deck as one clean piece, and keep a city of bridges smooth to pan and zoom *(PR [#207](https://github.com/massif-maps/MassifMaps/pull/207) by [@farfromrefug](https://github.com/farfromrefug))*
- [`c0074b1`](https://github.com/massif-maps/MassifMaps/commit/c0074b12b9201e47e377f2e180f8f9943a43a455) - **terrain**: sharpen the draped ground, and grow a draped road with the zoom *(PR [#209](https://github.com/massif-maps/MassifMaps/pull/209) by [@farfromrefug](https://github.com/farfromrefug))*
- [`c32c761`](https://github.com/massif-maps/MassifMaps/commit/c32c761005ece7f9d713b3f5fc726187f6048a03) - **terrain**: read a building's base from a field that is actually smooth *(PR [#211](https://github.com/massif-maps/MassifMaps/pull/211) by [@farfromrefug](https://github.com/farfromrefug))*
- [`202eb95`](https://github.com/massif-maps/MassifMaps/commit/202eb95afad5be96378ae3524746715a90d8b156) - **terrain**: stop a tilted 3D map being killed for running out of memory *(PR [#212](https://github.com/massif-maps/MassifMaps/pull/212) by [@farfromrefug](https://github.com/farfromrefug))*
- [`5fa686e`](https://github.com/massif-maps/MassifMaps/commit/5fa686e8803c30e2e6059b4947bf5f7a2cef6aeb) - **api**: build the iOS SDK again — two adopt() overloads made one Objective-C selector *(PR [#216](https://github.com/massif-maps/MassifMaps/pull/216) by [@farfromrefug](https://github.com/farfromrefug))*
- [`24fafb5`](https://github.com/massif-maps/MassifMaps/commit/24fafb5a705074b01df56adbc0110c2280209e89) - **ios**: ship the facade headers the framework's umbrella imports *(PR [#217](https://github.com/massif-maps/MassifMaps/pull/217) by [@farfromrefug](https://github.com/farfromrefug))*
- [`c65f734`](https://github.com/massif-maps/MassifMaps/commit/c65f73411fc19a86aff2fe3415b4d6bcd0f897be) - **vt**: light a bridge deck's road once, so the bridge is not black at night *(PR [#219](https://github.com/massif-maps/MassifMaps/pull/219) by [@farfromrefug](https://github.com/farfromrefug))*
- [`1e3f67d`](https://github.com/massif-maps/MassifMaps/commit/1e3f67d806d71a717c1447c16ae73f9a960e25b5) - point master at the merged libs-massif commit, so it clones and builds *(PR [#221](https://github.com/massif-maps/MassifMaps/pull/221) by [@farfromrefug](https://github.com/farfromrefug))*
- [`78ac542`](https://github.com/massif-maps/MassifMaps/commit/78ac5424019d58c8ee359bf89cd443055fa54526) - **labels**: show a marker layer added after the map's first frame *(PR [#224](https://github.com/massif-maps/MassifMaps/pull/224) by [@farfromrefug](https://github.com/farfromrefug))*
- [`5274932`](https://github.com/massif-maps/MassifMaps/commit/5274932324956434da50d9a748e238378ea9e697) - **api**: stop the map hanging at startup when a listener reads the camera back *(PR [#227](https://github.com/massif-maps/MassifMaps/pull/227) by [@farfromrefug](https://github.com/farfromrefug))*
- [`623d08a`](https://github.com/massif-maps/MassifMaps/commit/623d08a784a1e50676cfb619d9603b8d98cc77f6) - **terrain**: start a 3D map in 3D, instead of flat and then ramping up *(PR [#226](https://github.com/massif-maps/MassifMaps/pull/226) by [@farfromrefug](https://github.com/farfromrefug))*
- [`1655fc2`](https://github.com/massif-maps/MassifMaps/commit/1655fc2e6d0f298924080a04c06afaaa43bf6f37) - **terrain**: stop the ground blanking, buildings vanishing and bridge decks flashing dark during a fast zoom *(PR [#222](https://github.com/massif-maps/MassifMaps/pull/222) by [@farfromrefug](https://github.com/farfromrefug))*
- [`45ec87a`](https://github.com/massif-maps/MassifMaps/commit/45ec87ae8b3eb9121beeddcee944da6bf055e022) - **vt**: keep a bridge deck at one height across tile cuts, and stop it jumping between culls *(PR [#225](https://github.com/massif-maps/MassifMaps/pull/225) by [@farfromrefug](https://github.com/farfromrefug))*
- [`140cf78`](https://github.com/massif-maps/MassifMaps/commit/140cf78dc4b5d3d24c67976ffbd8df966ae95bc3) - **terrain**: stop the camera lifting off a low-tilt view over terrain *(PR [#235](https://github.com/massif-maps/MassifMaps/pull/235) by [@farfromrefug](https://github.com/farfromrefug))*
- [`71e2713`](https://github.com/massif-maps/MassifMaps/commit/71e2713549e197d06600b852d5f5e4deb164aafc) - **demo**: stop an example crashing when it is left while still starting *(PR [#236](https://github.com/massif-maps/MassifMaps/pull/236) by [@farfromrefug](https://github.com/farfromrefug))*
- [`1de8826`](https://github.com/massif-maps/MassifMaps/commit/1de88262e26126132c79966b53d4985055584047) - **layers**: keep buildings on a coarsened tile, and stop the map redrawing at rest *(PR [#238](https://github.com/massif-maps/MassifMaps/pull/238) by [@farfromrefug](https://github.com/farfromrefug))*
- [`d74bcab`](https://github.com/massif-maps/MassifMaps/commit/d74bcabd1350223fb2900b7a9a496fed1561dcb5) - **api**: spell a colour the same way everywhere, and refuse a bad one *(PR [#239](https://github.com/massif-maps/MassifMaps/pull/239) by [@farfromrefug](https://github.com/farfromrefug))*
- [`fee52ca`](https://github.com/massif-maps/MassifMaps/commit/fee52ca72d3d10cca38d44d385da5d3781db4c66) - **terrain**: make a 3D city usable again - buildings on their own ground, no deadlock on tilt, and a far field that is not paid for twice *(PR [#240](https://github.com/massif-maps/MassifMaps/pull/240) by [@farfromrefug](https://github.com/farfromrefug))*
- [`36c8536`](https://github.com/massif-maps/MassifMaps/commit/36c85365506f8b1be0fc4dee98e57966394566ed) - **vt**: stop 3D buildings smearing the screen and killing the GPU when the map flattens *(PR [#244](https://github.com/massif-maps/MassifMaps/pull/244) by [@farfromrefug](https://github.com/farfromrefug))*
- [`3aa1700`](https://github.com/massif-maps/MassifMaps/commit/3aa1700f8d975ec620f8c4d74b0672043f19729c) - **mapnikvt**: carry table style parameters through the compiled XML *(PR [#247](https://github.com/massif-maps/MassifMaps/pull/247) by [@farfromrefug](https://github.com/farfromrefug))*
- [`95a3381`](https://github.com/massif-maps/MassifMaps/commit/95a3381d4f869748bc96760f8721b3cd26ed32cd) - **vt**: actually apply the no-drape occlusion mask, and stop it flattening contours *(PR [#246](https://github.com/massif-maps/MassifMaps/pull/246) by [@farfromrefug](https://github.com/farfromrefug))*
- [`17e2614`](https://github.com/massif-maps/MassifMaps/commit/17e26145c20f8013b774fcaaef7f55ef3cfb9911) - **labels**: fade labels in 300 ms, as maplibre does, instead of a second *(PR [#257](https://github.com/massif-maps/MassifMaps/pull/257) by [@farfromrefug](https://github.com/farfromrefug))*
- [`8aa89c1`](https://github.com/massif-maps/MassifMaps/commit/8aa89c189762002e242bc78ce0db6fa312af96e2) - **styles**: apply contour-label-stubs and contour-label-interval from a style *(PR [#259](https://github.com/massif-maps/MassifMaps/pull/259) by [@farfromrefug](https://github.com/farfromrefug))*
- [`7929567`](https://github.com/massif-maps/MassifMaps/commit/79295677e0dddd0f1d275002f77ab9fca295615d) - **renderers**: fly the camera the way mapbox does, in screenfuls and on an eased clock *(PR [#252](https://github.com/massif-maps/MassifMaps/pull/252) by [@farfromrefug](https://github.com/farfromrefug))*
  - *fixes issue [#179](https://github.com/massif-maps/MassifMaps/issues/179) opened by [@farfromrefug](https://github.com/farfromrefug)*
- [`c99e4b3`](https://github.com/massif-maps/MassifMaps/commit/c99e4b31f8876f65c82ec4999c7c7d987dfc5a45) - **layers**: stop logging a debug line per layer on every tile cull *(PR [#262](https://github.com/massif-maps/MassifMaps/pull/262) by [@farfromrefug](https://github.com/farfromrefug))*
- [`41c0a83`](https://github.com/massif-maps/MassifMaps/commit/41c0a831c9beab58725c636f0892c717c017b89b) - **renderers**: stop building shadows lingering on the ground when zooming out fast *(PR [#269](https://github.com/massif-maps/MassifMaps/pull/269) by [@farfromrefug](https://github.com/farfromrefug))*
- [`36a5d9c`](https://github.com/massif-maps/MassifMaps/commit/36a5d9cee7f95981b7c6e02397fa973599c8ab62) - **celestial**: draw sky directions at their true altitude on the globe *(PR [#273](https://github.com/massif-maps/MassifMaps/pull/273) by [@farfromrefug](https://github.com/farfromrefug))*
- [`41efc6f`](https://github.com/massif-maps/MassifMaps/commit/41efc6f24b3dcb153f91ba2aa1fea0c14b8a531b) - **terrain**: stand a first-person eye on its own ground once the DEM loads, without a gesture *(PR [#274](https://github.com/massif-maps/MassifMaps/pull/274) by [@farfromrefug](https://github.com/farfromrefug))*
- [`6637757`](https://github.com/massif-maps/MassifMaps/commit/66377577beb74a5c90355be112c0a46960b761a1) - **styles**: shade the Massif relief on the SDK as MapLibre does *(PR [#279](https://github.com/massif-maps/MassifMaps/pull/279) by [@farfromrefug](https://github.com/farfromrefug))*
- [`450b491`](https://github.com/massif-maps/MassifMaps/commit/450b49133a6d45cef30fc3f6d625e3d351e10907) - **build**: build geocoding without the package manager, and routing without Valhalla *(PR [#281](https://github.com/massif-maps/MassifMaps/pull/281) by [@farfromrefug](https://github.com/farfromrefug))*
- [`082eb1a`](https://github.com/massif-maps/MassifMaps/commit/082eb1af06ee4afcc9d67d5853d2293d651806ec) - **api**: build @massif-maps/api again, and take an easing in a timed moveTo *(PR [#283](https://github.com/massif-maps/MassifMaps/pull/283) by [@farfromrefug](https://github.com/farfromrefug))*
- [`f249a24`](https://github.com/massif-maps/MassifMaps/commit/f249a24d02b2c302258f7532abebd3018a128873) - **vt**: lay a building's contact shadow only in the tiles it reaches *(PR [#284](https://github.com/massif-maps/MassifMaps/pull/284) by [@farfromrefug](https://github.com/farfromrefug))*
- [`582c437`](https://github.com/massif-maps/MassifMaps/commit/582c437673c298a052d3934e887aa1da930f6751) - **release**: make the GitHub release public before JitPack, Swift and npm, with notes that fit *(PR [#292](https://github.com/massif-maps/MassifMaps/pull/292) by [@farfromrefug](https://github.com/farfromrefug))*
- [`8bb1162`](https://github.com/massif-maps/MassifMaps/commit/8bb116241ace9442248beacc14757dee6fd591c0) - **layers**: stop tiles reloading in a loop with terrain shadows or preloading on *(PR [#280](https://github.com/massif-maps/MassifMaps/pull/280) by [@farfromrefug](https://github.com/farfromrefug))*
- [`b62be68`](https://github.com/massif-maps/MassifMaps/commit/b62be684f2808d2d725fd867c88dc75972ca6416) - **lighting**: flip the day-cycle palette near sunset, not mid-afternoon *(PR [#293](https://github.com/massif-maps/MassifMaps/pull/293) by [@farfromrefug](https://github.com/farfromrefug))*
- [`ab9aeb6`](https://github.com/massif-maps/MassifMaps/commit/ab9aeb6c69987b62c3edb0af0ce26a15fce040e2) - **android**: build the AAR again, TileLayer.coversGround leaked into the Java bindings *(PR [#301](https://github.com/massif-maps/MassifMaps/pull/301) by [@farfromrefug](https://github.com/farfromrefug))*

### Tests
- [`3e0f0ae`](https://github.com/massif-maps/MassifMaps/commit/3e0f0ae8dbb324eba74fdab2cd905ad9ba85db7b) - **style**: pin when a style parameter folds at decode and when it stays live *(PR [#196](https://github.com/massif-maps/MassifMaps/pull/196) by [@farfromrefug](https://github.com/farfromrefug))*
- [`f2d4a65`](https://github.com/massif-maps/MassifMaps/commit/f2d4a65c6d9e89f0295e01cb4e86cabff921a2d8) - **vt**: assert a line label's run stays on its line, and document the rule *(PR [#200](https://github.com/massif-maps/MassifMaps/pull/200) by [@farfromrefug](https://github.com/farfromrefug))*
- [`1462bb7`](https://github.com/massif-maps/MassifMaps/commit/1462bb7976a5d8d38137339e0a312f1568ad4f8c) - **vt**: measure how far a line reaches out of its own width at a join *(PR [#203](https://github.com/massif-maps/MassifMaps/pull/203) by [@farfromrefug](https://github.com/farfromrefug))*


## [v6.0.1] - 2026-08-18
### BREAKING CHANGES
- due to [`40ef746`](https://github.com/massif-maps/MassifMaps/commit/40ef746a9bc32bae03864d89beb3870f92d63744) - fog the whole map in 2D and let an app replace the fog shader *(PR [#123](https://github.com/massif-maps/MassifMaps/pull/123) by [@farfromrefug](https://github.com/farfromrefug))*:

  TerrainOptions.FogColor/FogStartDistance/FogDistance and  
  SkyOptions.FogBlend/FogHorizon are gone - use FogOptions, whose range is in multiples of the  
  camera-to-focus distance rather than metres, so the old numbers do not carry over. The style  
  properties fog-start-distance / fog-distance become fog-range-start / fog-range-end. See  
  docs/migration.md.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(renderers): draw stars as dots and stop the haze erasing them  
  Two things were wrong with the star field:  
  - It lit a whole cell of a direction-space lattice, which reads as a grid of grey squares.  
    Port the demo day-cycle sky's version (scripts/android-dev, DemoSky.buildSkyShader), which  
    had it right: cells in (azimuth, elevation) rather than a flat projection that streaks them  
    near the horizon, one star per cell at most, placed at a random point INSIDE its cell and  
    drawn as a soft dot with its own brightness.  
  - They were added before the fog blend, so they were multiplied by (1 - haze) like everything  
    else and vanished wherever the fog band reached - which HorizonBlend alone decided, leaving  
    them in the strip of sky above it and nowhere else. They are now added after it and take  
    only the square root of the haze: they sit beyond the atmosphere, so they dim into it rather  
    than being erased by it, and they still go where the haze actually saturates. That also puts  
    them outside skyColor, so a custom sky shader gets them too - StarIntensity defaults to 0.  
  Give every fog preset a high colour as well - it is the one property with no other way to see  
  it - and make the 'space' preset a night sky: dark ground haze, deep blue atmosphere,  
  near-black zenith, stars well up.  
  Note when testing: a custom sky shader that replaces skyColor still draws its own gradient, and  
  '--es daycycle true' installs the demo's sky, whose stars are gated on its own day value.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(demo): let the camera tilt down to 10 degrees  
  The tilt range floored at 30, which is a fairly steep view: the sky is a thin strip at the top  
  of the screen and the horizon never really opens up. 10 gives enough sky to judge the fog band,  
  the atmosphere colours and the stars against.  
  ---------

- due to [`d6f25d2`](https://github.com/massif-maps/MassifMaps/commit/d6f25d26699d70d323b4462f203d5ba2b6596442) - keep long mountain shadows when panning back, in view-relative units *(commit by [@farfromrefug](https://github.com/farfromrefug))*:

  LightOptions.setShadowDistance takes a multiple of the  
  camera-to-focus distance, not metres. The signature is unchanged, so an app passing  
  20000 compiles and asks for 20000 times the view. Drop the call and take the  
  default, or scale from there. See docs/migration.md.

- due to [`35174ea`](https://github.com/massif-maps/MassifMaps/commit/35174eaf545ae0b9f223cb74fc5db0dccd56ff01) - keep mountain and building shadows sharp and present at every zoom and tilt *(PR [#128](https://github.com/massif-maps/MassifMaps/pull/128) by [@farfromrefug](https://github.com/farfromrefug))*:

  LightOptions.setShadowDistance takes a multiple of the  
  camera-to-focus distance, not metres. The signature is unchanged, so an app passing  
  20000 compiles and asks for 20000 times the view. Drop the call and take the  
  default, or scale from there. See docs/migration.md.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(terrain): add LightOptions.shadowNormalOffset for cleaner building shadows  
  A receiving surface is pushed along its own normal by N shadow-map texels before it looks  
  itself up, mapbox's model (default 3, theirs). Acne then clears by moving the sample sideways  
  rather than by lifting its depth, so the depth bias can stay small enough for a shadow to stay  
  attached to the building casting it. 3D extrusions only - the terrain surface takes its normal  
  per fragment from the DEM, out of a vertex-stage offset's reach. 0 disables it.  
  Requires libs-massif 67a0ef7 (vt: the offset itself, and the clamp that stops a far cascade's  
  offset walking a roof out of the mountain shadow it stands in).  
  Demo: panel slider, --es shadowNormalOffset, and live over the CONFIG broadcast.  
  Emulator-verified at Grenoble z17 tilt 40; not checked on the Crosscall. No camera has yet been  
  found where the offset earns its cost - documented as an open gap in  
  docs/internals/rendering/08-lighting-sky-fog.md.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(terrain): make the shadow map the depth buffer, not a packed copy of it  
  TerrainShadowMap attaches a DEPTH_COMPONENT24 texture as the depth attachment and drops the  
  colour attachment entirely wherever a depth texture can be sampled (ES3 core, or  
  OES_/ANGLE_depth_texture). The caster pass then writes depth alone: before it wrote depth to a  
  renderbuffer AND a packed-RGB copy of gl_FragCoord.z to an RGBA8 target, which the receiver  
  unpacked with a dot. The atlas goes RGBA8 + D16 -> D24 and the packing is masked off.  
  24 bits and not 16 because the packed path spread gl_FragCoord.z over three bytes - a D16  
  texture would have LOST precision and bought acne back. ES2 + OES_depth_texture has only the  
  unsized form.  
  GLContext gains ES3 / DEPTH_TEXTURE / SHADOW_SAMPLERS detection and logs them at startup. A  
  depth-only framebuffer is complete by the ES3 spec but is not guaranteed on ES2 drivers, so an  
  incomplete status falls back to the packed map rather than to no shadows - iOS builds against  
  MetalANGLE, whose README records the build patched down to ES2 for 32-bit devices.  
  Requires libs-massif 20d9a43 (vt: the SHADOW_DEPTH_TEXTURE lookup).  
  Emulator, OpenGL ES 3.0 (4.1 Metal - 90.5): depth texture 1, shadow samplers 0, shadows ACTIVE,  
  depth-only framebuffer complete, image equivalent to the packed path at Grenoble z12.53 tilt 26.  
  NOT checked on the Crosscall, and no ES2 device has exercised the fallback.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * perf(terrain): bind the shadow map as a comparison sampler for hardware PCF  
  TerrainShadowMap sets TEXTURE_COMPARE_MODE = COMPARE_REF_TO_TEXTURE and LINEAR filtering on the  
  depth texture wherever the shading language can declare a sampler2DShadow - ESSL 3.00, hence an  
  ES3 context. LINEAR is only meaningful there, because the comparison happens BEFORE the filter:  
  one fetch returns the bilinear average of four depth compares, where the manual path did a fetch,  
  an unpack and a compare per tap.  
  GL_EXT_shadow_samplers reads 0 on the Crosscall (Adreno, OpenGL ES 3.2 V@0502.0) and on the  
  emulator, because it is an ES2 extension that a driver does not advertise on an ES3 context -  
  sampler2DShadow being core there. The capability is therefore derived from ES3 + depth texture,  
  not from the extension string.  
  Requires libs-massif 0954e2c (vt: the ESSL 3.00 programs and the comparison lookup).  
  Emulator, OpenGL ES 3.0 (4.1 Metal - 90.5), Grenoble z12.53 tilt 26: shadows ACTIVE, 123,401 px  
  of 2,592,000 differ from the manual-tap path. FRAME TIME NOT MEASURED on either device - the win  
  is inferred from the fetch count. NOT run on the Crosscall.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(terrain): record that hardware PCF buys quality, not speed  
  Interleaved A/B on the Crosscall (Adreno 610, -PprofileRender, Grenoble z13 tilt 30, base  
  composite, shadow 0.6, panning, medians over 42 one-second windows): hardware PCF 14.6 fps /  
  6.0 ms drape against manual taps 14.5 / 6.0 - within noise. Four taps against one: also within  
  noise. The tap count was never the cost.  
  What the feature costs at that camera is 23.9 -> 14.6 fps and 1.2 -> 6.1 ms of drape, and  
  1.4 ms of it is the CASCADE COUNT (14.6 -> 17.0 fps at one cascade) - one shadow matrix per  
  vertex and one highp vec3 varying per cascade. Same conclusion the earlier tap-count experiment  
  reached from the other side.  
  Corrects this page's claim that the frame-time win was merely unmeasured: it was measured and it  
  is zero. What hardware PCF does buy is 16 effective samples for the price of 4.  
  Next perf step is therefore cascades, not sampling.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * docs(terrain): record why building shadows are softer than mapbox's at z16  
  The cause is the shadow-map texel, not the filter and not the screen-space mask. With the range at  
  4.5 x the camera-to-focus distance (~7 km at z16) split three ways, the near cascade covers ~1.5 km  
  through a 1024 page - about 1.5 m of ground per texel, i.e. metres-wide steps across a street.  
  mapbox puts the same near-cascade distance through a 2048 page and spends nothing on a third  
  cascade.  
  Measured on the Crosscall, Grenoble z16 tilt 45, same camera forced by broadcast: 1024 x 3 gives  
  14.1 fps / 5.8 ms drape and washed courtyard shadows, 2048 x 2 gives 13.6 / 5.8 and tight edges.  
  Nearly free, because the per-cascade cost is matrices and varyings rather than sampling. Defaults  
  are left alone - 2048 x 2 at D24 is ~33 MB of atlas against ~12.6 MB - and the knobs are documented  
  instead.  
  Records the dead end too: SHADOW_MASK_DIVISOR = 1 was tried on the theory that the quarter-  
  resolution mask was blurring building shadow edges. It made them look worse - the full-res mask  
  exposed the shadow-map staircase the blur had been hiding. The mask hides quantisation, it does not  
  cause it.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): keep building shadows sharp as the view tilts  
  Each shadow cascade is now fitted to the bounding sphere of its view-frustum slice instead of to  
  the visible-ground wedge, so the texel size no longer depends on the pitch, the bearing or the sun  
  azimuth. Shadows at a low tilt look like shadows at tilt 90, which is how mapbox behaves and why  
  theirs stay sharp.  
  MapRenderer passes the camera-to-focus distance into the fit. It used to be read from the vt view  
  state, which TileRenderer::onDrawFrame fills AFTER the shadow pass runs - stale at best, absent on  
  the frames that matter.  
  Requires libs-massif 9548f46 (vt: the sphere fit itself, and the ~200 lines of wedge machinery it  
  replaces).  
  Device-verified on the Crosscall at the reported camera (lat 45.188499 lon 5.734500, z16 tilt 45  
  rotation -15.12, bld3d, sun 16.5 UTC): shadows with no locatable edge before, hard edges on  
  individual buildings after. Frame cost not re-measured, and tilt 90 not re-checked for regressions.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): bound the shadow caster ring by the throw distance, not by a tile count  
  A mountain's shadow was missing at z16 and appeared as soon as you zoomed out or panned enough to  
  pull the mountain into the visible cover. The caster ring was a fixed number of tiles at the  
  cover's own zoom, and a tile count is a DISTANCE THAT SHRINKS WITH THE ZOOM: at z16 a tile is  
  ~430 m, so the default margin of 3 reached 1.3 km while the mountain casting into the view was  
  several km away and simply had no caster drawn for it.  
  The ring is now sized by how far a shadow can physically be thrown - relief / tan(sun altitude),  
  which the shadow pass's 15-degree floor caps at about 3.7 x the relief. Holding that distance means  
  dropping the resolution, since 7 km at z16 would be a 35x35 ring: the ring is generated at the  
  COARSEST tile zoom that still spans the throw in shadowCasterMargin tiles, and the existing  
  partition logic subdivides whatever overlaps the finer cover.  
  shadowCasterMargin therefore now sets the ring's RESOLUTION rather than its reach; the reach is  
  derived and correct at every zoom. Documented on the option and in the rendering notes.  
  Device-verified on the Crosscall at the reported camera (lat 45.193196 lon 5.735717 z16.04 tilt 90  
  rotation -15.12, terrain lighting, shadow 0.8): the shadow is now present at the default margin and  
  matches what the old code only produced at margin 8 (81k px of 1.36M differ, tile-load noise;  
  against the old margin 3 it is 750k). 22 caster tiles per pass, 1.0 ms per pass; the ring costs  
  2.3 ms of drape (20.5 fps against 25.4 with no ring at all), where the old fine ring drew up to 49  
  tiles there and still missed the mountain.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): size the shadow caster ring from the surrounding massif, not the visible cover  
  The previous commit bounded the caster ring by the shadow throw, but computed the throw from the  
  relief of the VISIBLE COVER. At z16 top-down over a valley that cover is a few tiles of flat ground:  
  metres of relief, a throw of a couple of hundred metres, and a ring that collapses straight back  
  onto the cover's own zoom - the exact behaviour it was meant to replace. The mountain casting into  
  the view is outside the cover, so its height was never in that range.  
  The relief now comes from a coarse ancestor of the cover (SHADOW_RELIEF_ZOOM = 10, ~28 km at  
  latitude 45), which spans the massif. One elevation query per frame.  
  FASTER than the broken version, not slower, because the same number of caster tiles now covers the  
  throw coarsely instead of covering the valley floor finely. Crosscall, lat 45.193196 lon 5.735717  
  z16.04 tilt 90, terrain lighting, shadow 0.8, panning:  
    cover relief (broken):  20.5 fps, 4.8 ms drape, 22 caster tiles per pass  
    massif relief (this):   23.5 fps, 3.5 ms drape, 21 caster tiles per pass  
    no ring at all:         25.7 fps, 2.3 ms drape  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * feat(terrain): log how many shadow casters were skipped for missing elevation  
  Added to the periodic shadow line beside the caster tile count and the extrusion draws, because  
  "the shadow is missing" has three different causes - the caster was never in the list, it was  
  clipped by the light box, or it had no elevation and was skipped - and nothing on screen tells  
  them apart.  
  Requires libs-massif 9c5888a (vt: the counter itself).  
  Used it straight away to rule out the coarse caster ring asking for DEM tiles that had not been  
  fetched: at Grenoble lat 45.190410 lon 5.734305 z16.53 tilt 90 the count is 0 per pass with 33  
  caster tiles drawn, so the remaining truncation reported at that camera is neither the ring's  
  reach nor a missing DEM.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  * fix(terrain): span the caster tiles with the shadow height slab, not the cover  
  vt has no per-tile heights for the caster RING - it measures every ring tile at the single range it  
  is handed - so a ridge taller than that range is clipped out of the caster pass by the light box's  
  near plane. Its shadow then arrives truncated along an edge that moves with the camera, because the  
  range follows the cover. Measured at Grenoble lat 45.190410 lon 5.734305 z16.53 tilt 90: the cover's  
  range was 5.75..17.43 while the caster tiles reached 145.13, eight times taller.  
  The range is now widened with the caster tiles' own min/max - exact, rather than the coarse-ancestor  
  guess tried first, which fell short at 120. The per-tile ranges still narrow each cascade's RECEIVER  
  slab, so the texel size does not pay for it.  
  Device-confirmed by Martin: reproducible before, not reproducible with this build, reproducible  
  again after rebuilding without it.  
  Recorded in the docs because it was nearly lost: a single-frame screenshot A/B scored this fix at  
  19,425 px of 1,357,952 - noise - and it was reverted as refuted. The symptom only appears while  
  panning, so a still frame never measured it. Two other candidates were ruled out with the same  
  method and those refutations stand on their own evidence (0 casters skipped for missing elevation;  
  ring reach needs 1.76 of its 3 tiles), but a static frame is not a valid detector for a motion  
  artifact.  
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>  
  ---------


### New Features
- [`645c736`](https://github.com/massif-maps/MassifMaps/commit/645c736aba439da48d22eb0e54e71d00f953805f) - **demo**: add the style regression repro layer *(commit by [@farfromrefug](https://github.com/farfromrefug))*
- [`40ef746`](https://github.com/massif-maps/MassifMaps/commit/40ef746a9bc32bae03864d89beb3870f92d63744) - **renderers**: fog the whole map in 2D and let an app replace the fog shader *(PR [#123](https://github.com/massif-maps/MassifMaps/pull/123) by [@farfromrefug](https://github.com/farfromrefug))*
- [`6f13bb4`](https://github.com/massif-maps/MassifMaps/commit/6f13bb447c0626a4d790a08f70eb9ed1ec356f94) - **terrain**: add LightOptions.shadowNormalOffset for cleaner building shadows *(commit by [@farfromrefug](https://github.com/farfromrefug))*
- [`525d15b`](https://github.com/massif-maps/MassifMaps/commit/525d15beddace5ed8918d3606318b6a85e7075f2) - **terrain**: log how many shadow casters were skipped for missing elevation *(commit by [@farfromrefug](https://github.com/farfromrefug))*
- [`f94ef5c`](https://github.com/massif-maps/MassifMaps/commit/f94ef5c1ac0d9f7e10dd5433b99a74bafd5a6be4) - **labels**: let one style pick flat labels in 2D and billboard ones in 3D *(PR [#136](https://github.com/massif-maps/MassifMaps/pull/136) by [@farfromrefug](https://github.com/farfromrefug))*

### Bug Fixes
- [`97da528`](https://github.com/massif-maps/MassifMaps/commit/97da52894007d067b7cb00e9187c14813ff0fe44) - **vt**: the reported style regressions in labels, lines and clipped text *(commit by [@farfromrefug](https://github.com/farfromrefug))*
- [`c999149`](https://github.com/massif-maps/MassifMaps/commit/c999149e9fcaceedd5d3673cd2d25d3985387fc7) - **terrain**: stop the previous zoom flashing over the map when zooming out *(PR [#124](https://github.com/massif-maps/MassifMaps/pull/124) by [@farfromrefug](https://github.com/farfromrefug))*
- [`d6f25d2`](https://github.com/massif-maps/MassifMaps/commit/d6f25d26699d70d323b4462f203d5ba2b6596442) - **terrain**: keep long mountain shadows when panning back, in view-relative units *(commit by [@farfromrefug](https://github.com/farfromrefug))*
- [`be7f83f`](https://github.com/massif-maps/MassifMaps/commit/be7f83fd93d87ed9712069016cfe5afe6441489c) - **terrain**: keep building shadows sharp as the view tilts *(commit by [@farfromrefug](https://github.com/farfromrefug))*
- [`1351675`](https://github.com/massif-maps/MassifMaps/commit/1351675e0dd5a957c411b85c9aacf1d301d48fdd) - **terrain**: bound the shadow caster ring by the throw distance, not by a tile count *(commit by [@farfromrefug](https://github.com/farfromrefug))*
- [`6bf362a`](https://github.com/massif-maps/MassifMaps/commit/6bf362a9576609f182dab4643a8eec2b6d3b6d2c) - **terrain**: size the shadow caster ring from the surrounding massif, not the visible cover *(commit by [@farfromrefug](https://github.com/farfromrefug))*
- [`0f10837`](https://github.com/massif-maps/MassifMaps/commit/0f10837f2a7c39d44ca2a4641fe6fb6d6a153854) - **terrain**: span the caster tiles with the shadow height slab, not the cover *(commit by [@farfromrefug](https://github.com/farfromrefug))*
- [`35174ea`](https://github.com/massif-maps/MassifMaps/commit/35174eaf545ae0b9f223cb74fc5db0dccd56ff01) - **terrain**: keep mountain and building shadows sharp and present at every zoom and tilt *(PR [#128](https://github.com/massif-maps/MassifMaps/pull/128) by [@farfromrefug](https://github.com/farfromrefug))*
- [`ff196c8`](https://github.com/massif-maps/MassifMaps/commit/ff196c8c8dd700fbc5240087f1d8085a142aa013) - shadow strenght was too high *(commit by [@farfromrefug](https://github.com/farfromrefug))*
- [`fb4b533`](https://github.com/massif-maps/MassifMaps/commit/fb4b5330bdf43f5aa993ce3bf343d5a4dbb5b247) - **terrain**: shade contours with the ground they lie on *(PR [#130](https://github.com/massif-maps/MassifMaps/pull/130) by [@farfromrefug](https://github.com/farfromrefug))*


## [v6.0.0] - 2026-08-16

The first release under the project's own name. The **CARTO Mobile SDK is now Massif Maps**: every
namespace, package, class prefix, artifact and debug token is renamed, the hosted CARTO services are
gone, and the project has moved to the [`massif-maps`](https://github.com/massif-maps) organisation.

The same release carries the whole 3D terrain, lighting and rendering-performance cycle that landed
since v5.2.3 — 3D terrain with GPU draping and cascaded shadows, a sky and sun model, MapLibre Tiles
(MLT) support, on-the-fly contours, composite vector-tile layers, celestial objects, custom shaders,
and a large body of measured performance work.

Full documentation: <https://massif-maps.github.io/MassifMaps/>

---

### Migration from the CARTO Mobile SDK

Massif Maps **is** the CARTO Mobile SDK, forked and kept alive after CARTO stopped maintaining it.
The 4.x/5.x concepts, class shapes and CartoCSS styles all still apply — what changed is the name.
For most apps the migration is a dependency line and a find-and-replace.

**1. Change the dependency.**

```gradle
// before
implementation 'com.carto:carto-mobile-sdk:4.4.+'
// after — JitPack overrides the declared groupId, so the coordinate is the GitHub path
implementation 'com.github.massif-maps:MassifMaps-android-aar:v6.0.0'
```

On iOS the CocoaPod is replaced by a Swift package:
`https://github.com/massif-maps/MassifMaps-ios-swift`.

**2. Rename the imports.**

```bash
# Android / Java / Kotlin
grep -rl 'com\.carto\.' src/ | xargs sed -i '' 's/com\.carto\./com.massifmaps./g'

# iOS / Obj-C / Swift — class prefix NT -> MSF
grep -rl 'NT[A-Z]' Sources/ | xargs sed -i '' -E 's/\bNT([A-Z][A-Za-z0-9]*)/MSF\1/g'
```

**3. Replace anything that talked to CARTO's servers** (see *Removed* below).

**4. Styles need no change.** `nuti::` still parses, with a deprecation warning.

| | Before | After |
|---|---|---|
| Java / Kotlin | `com.carto.*` | `com.massifmaps.*` |
| Java (routing-lib) | `com.akylas.routing.*` | `com.massifmaps.routing.*` |
| Objective-C / Swift | `NTMapView`, `NT_*` | `MSFMapView`, `MSF_*` |
| .NET | `Carto.Ui`, `Carto.Layers` | `Massif.Ui`, `Massif.Layers` |
| C++ | `carto::` | `massif::` |
| Build defines | `_CARTO_*_SUPPORT` | `_MASSIF_*_SUPPORT` |
| Native library | `libcarto_mobile_sdk.so` | `libmassif.so` |
| Gradle artifact | `com.carto:carto-mobile-sdk` | `com.massifmaps:massif` |
| logcat tag | `carto-mobile-sdk` | `massif` |

**CartoCSS keeps parsing the old spelling**, with one deprecation warning per token per stylesheet:
`nuti::x` → `param::x`, `nutiparameters` → `styleparameters`, and the placements
`nutibillboard` → `billboard`, `nutibillboardline` → `billboard-line`, `nutipoint` → `flat`,
`nuticallout` → `callout`.

**Deliberately not renamed**, because they name data users already have on disk or upstream work,
not this SDK: the `NUTi` bitmap magic, `nutikeysha1`, `.nutigraph` / `.nutigeodb`,
`__Nuti_pkgmgr_`, `cartodb_id`, CartoCSS the language, and the CartoDB attribution.

Every renamed token is listed in [docs/migration.md](docs/migration.md)
([online](https://massif-maps.github.io/MassifMaps/docs/migration)).
Rebrand PR: [#99](https://github.com/massif-maps/MassifMaps/pull/99).

---

### New Features

#### 3D terrain

The map surface is displaced by a DEM elevation source (Mapbox or Terrarium RGB encoding), attached
with `Options.setTerrainOptions(...)`. Every layer type renders on it — vector tiles, raster
overlays, hillshade, 3D buildings and vector elements — with a single shared depth model.
See [docs/features/3d-terrain.md](docs/features/3d-terrain.md) and
[docs/internals/rendering/04-terrain.md](docs/internals/rendering/04-terrain.md).

- **GPU draping.** Elevation tiles are uploaded as GL textures and every draped vertex shader
  replaces its `z` by sampling the shared texture, so all layers agree on heights exactly.
  Polygon fills and backgrounds are baked into a per-tile render-to-texture drape (MapLibre's
  model), cached across frames, so they follow the terrain with no holes or see-through.
  `TerrainOptions.NoDrapeLayerFilter` (default `^contour.*`) keeps hairline content at screen
  resolution. ([#17](https://github.com/massif-maps/MassifMaps/pull/17),
  [#21](https://github.com/massif-maps/MassifMaps/pull/21))
- **Painter-order depth model**, ported from tangram-ng: a terrain depth pre-pass is the single
  depth source, so overlapping draped layers cannot z-fight while ridges still occlude what is
  behind them. Red-green edge-local tesselation removes the T-vertex cracks at LOD transitions.
  ([#17](https://github.com/massif-maps/MassifMaps/pull/17))
- **Matching DEM heights across tile edges.** Elevation texture borders are backfilled from real
  neighbours (texel-exact at the same level, sampled at border texel centres from a coarser
  ancestor), and visible tiles now prefetch their own elevation tile and its neighbours through a
  background worker instead of relying on map-tile fetches. Opt out with
  `setSeamlessTileEdgesEnabled(false)` / `setElevationPrefetchEnabled(false)`.
  ([#35](https://github.com/massif-maps/MassifMaps/pull/35))
- **Camera and gestures are terrain-aware.** `TerrainOptions.setCameraClearance` holds the camera
  above the ground as a zoom bound rather than a corrective jump, panning keeps the touched terrain
  point under the finger, and pinch/rotate take their scale and angle from the screen (tangram's
  model) so a grazing ray can no longer cancel a gesture.
  ([#23](https://github.com/massif-maps/MassifMaps/pull/23),
  [#77](https://github.com/massif-maps/MassifMaps/pull/77))
- **View distance and fog.** `TerrainOptions.ViewDistanceFactor` ends the ground on tangram's rule
  and the background plane and sky pick up the same fog; `FarPlaneFactor` controls the far plane.
  ([#39](https://github.com/massif-maps/MassifMaps/pull/39),
  [#49](https://github.com/massif-maps/MassifMaps/pull/49))
- **Labels and billboards** are anchored on the surface, hidden behind ridges via a depth readback
  with hysteresis (`setBillboardOcclusionEnabled`), and keep a constant screen size.
- Also: `setMaxTileZoomOffset` caps terrain LOD relative to camera zoom, `setExaggeration`,
  `setMeshResolution`, `setDrapeResolution`, and `getElevation`/`getElevations` queries sharing the
  elevation cache. Terrain is `PLANAR` render projection only.

#### Sky, sun lighting and shadows

`LightOptions` gains the sun (azimuth/altitude, or `setSunPositionFromTime`), intensity, ambient
and the shadow controls; `TerrainShadowMap` renders **cascaded shadow maps** (3×1024 by default),
snapped and cached so the caster pass only re-runs when the light box, caster set or tile content
changed. `SkyOptions` draws a ray-direction sky that an app can replace wholesale with
`setShaderSource`. `StyleEnvironment::resolveLighting`/`resolveFog` merge the app's options with
the CartoCSS `Map` block, per property and zoom-dependent, and light the fog itself — dark at
night, warm at a low sun — so ground, background plane and sky always agree.
([#27](https://github.com/massif-maps/MassifMaps/pull/27),
[#40](https://github.com/massif-maps/MassifMaps/pull/40),
[#29](https://github.com/massif-maps/MassifMaps/pull/29)) —
[docs/features/sky-sun-shadows.md](docs/features/sky-sun-shadows.md)

#### Data sources and tile formats

- **MapLibre Tiles (MLT).** `MBVectorTileDecoder.TileFormat` = `AUTO` (default) / `MVT` / `MLT`.
  Resolution order is an explicit `setTileFormat`, then the source's own metadata, then per-tile
  detection (measured at 1 ns/tile for MVT, 19 ns for MLT). Everything downstream of the decode is
  unchanged, so an app pointed at an MLT source needs no code change.
  `TileDataSource::getMetaData(key)` is now a base virtual, forwarded by the cache, contour, ordered
  and combined sources. ([#90](https://github.com/massif-maps/MassifMaps/pull/90)) —
  [docs/features/maplibre-tiles.md](docs/features/maplibre-tiles.md)
- **`ContourTileDataSource`** — on-the-fly contour lines from any RGB-elevation source via marching
  squares, sharing its fetch and decode. Emits a `contour` layer with `ele` and `div` matching the
  `gdal_contour` pipeline, so it drops into a normal `VectorTileLayer` with no style change.
  Options for base interval, generation resolution, min zoom, simplify tolerance and seamless edges.
  ([#18](https://github.com/massif-maps/MassifMaps/pull/18)) —
  [docs/features/contours.md](docs/features/contours.md)
- **GeoJSON sources are tiled from a geojson-vt pyramid** instead of rescanning every feature for
  every tile and keeping one re-simplified copy per zoom.
  ([#54](https://github.com/massif-maps/MassifMaps/pull/54)) —
  [docs/features/geojson-vector-tiles.md](docs/features/geojson-vector-tiles.md)
- **`DirAssetPackage` and `AndroidAssetPackage`** load a style from a filesystem directory or from
  the app's assets, so a style can be edited in place with no repackaging.
  ([#42](https://github.com/massif-maps/MassifMaps/pull/42))

#### Layers and styling

- **`CompositeVectorTileLayer`** weaves named external sources (raster, hillshade, extra vector or
  contour) into a master CartoCSS style's layer order, each placed at a matching layer name and
  configured from a `#name { … }` block with zoom- and parameter-dependent expressions. Sources can
  be added and removed at runtime. Single-pass rendering decodes the master style once and is the
  default (`setSinglePassRenderingEnabled(false)` restores the per-group path).
  ([#19](https://github.com/massif-maps/MassifMaps/pull/19),
  [#20](https://github.com/massif-maps/MassifMaps/pull/20)) —
  [docs/features/composite-vector-tile-layer.md](docs/features/composite-vector-tile-layer.md),
  [reference](docs/features/composite-layer-reference.md)
- **`CustomRasterTileLayer`** runs an app-supplied GLSL filter shader over any raster source;
  `HillshadeRasterTileLayer` is now its DEM specialisation and gains shader-drawn contour lines
  (`setContourEnabled`/`Interval`/`Color`/`Width`) evaluated per fragment at a fixed metre interval.
  ([#18](https://github.com/massif-maps/MassifMaps/pull/18)) —
  [docs/features/custom-raster-shaders.md](docs/features/custom-raster-shaders.md)
- **Hillshade matches MapLibre.** The `STANDARD` method is back at parity: the north-south aspect
  mirror and the 90° azimuth error are fixed, `applyLighting` no longer divides out the slope
  intensity, colours are premultiplied up front, and `contrast` / `exaggeration` are separated onto
  MapLibre's meanings with matching defaults. `CompositeVectorTileLayer` gains `zoomLevelBias` so a
  high-resolution DEM child can be fetched at more detail than the base map.
  ([#34](https://github.com/massif-maps/MassifMaps/pull/34)) —
  [docs/features/hillshade.md](docs/features/hillshade.md)
- **Live style parameters.** A `setStyleParameter` call whose parameters only feed properties the
  renderer evaluates per frame now answers with a repaint instead of a full re-decode of every
  visible tile (~130 ms per tile before). Covers colour-only changes and selection parameters.
  ([#73](https://github.com/massif-maps/MassifMaps/pull/73),
  [#76](https://github.com/massif-maps/MassifMaps/pull/76)) —
  [docs/features/style-parameters.md](docs/features/style-parameters.md)
- **Post-process effects.** `MapRenderer.setPostProcessEffect(...)` renders the frame through a
  custom fullscreen GLSL shader with the packed terrain depth available, plus
  `TerrainOptions.setSurfaceShaderSource` for the terrain surface itself and runtime float/colour
  parameters on both. Ships a PeakFinder-style relief-outline effect built entirely from these
  hooks. ([#56](https://github.com/massif-maps/MassifMaps/pull/56)) —
  [docs/features/post-processing.md](docs/features/post-processing.md)
- **`CelestialLayer`** places objects that are not on the map — by direction (azimuth/altitude,
  distance 0 = infinitely far) or by geographic position and altitude. `CelestialSprite` is a
  camera-facing quad sized by angle or pixels, batched per bitmap so a catalogue of thousands is one
  draw call; `CelestialArc` draws a circle about an axis, a path or disjoint segments. Both are
  clickable — which also fixed the click path dropping any touch whose ray missed the ground.
  ([#55](https://github.com/massif-maps/MassifMaps/pull/55)) —
  [docs/features/celestial-objects.md](docs/features/celestial-objects.md)

#### Labels and text

- **Variable shield placement** (Mapbox/tangram-style: the name takes the free side) and **font
  icons** instead of bitmap shields. ([#57](https://github.com/massif-maps/MassifMaps/pull/57)) —
  [docs/features/label-styling.md](docs/features/label-styling.md)
- **System fonts.** `SystemFontUtils` resolves a `face-name` the style does not package from the
  device (Android scans `/system/fonts` and friends, iOS/macOS asks CoreText, UWP asks DirectWrite),
  with style fonts keeping precedence. An inline CartoCSS string, which cannot carry a font asset
  package, previously failed every label with `Failed to load text font`.
  ([#45](https://github.com/massif-maps/MassifMaps/pull/45))
- **Font name lists** on both text stacks — style `text-face-name` and every vector-element font
  name — with optional per-platform entries. `BalloonPopup`, `Text` and popup buttons rasterize
  through the platform text API and so never saw the system-font work at all; the SDK default
  `HelveticaNeue-Light` is an iOS PostScript name that silently rendered as Roboto on Android.
  ([#101](https://github.com/massif-maps/MassifMaps/pull/101))
- **Callout labels** with a band, leader line and plate box.
  ([#78](https://github.com/massif-maps/MassifMaps/pull/78))

#### Camera, gestures and views

- **The tilt may go below 0** so the camera can look above the horizon, keeping the camera in place
  and pitching the view about it (`dist(camera, focus)` is preserved, so zoom, culling and the depth
  budget are unchanged). Opt-in — the default tilt range is still `(0, 90)`.
- **Free roam is a mode**: `Options.FreeRoamMode` = `OFF` / `LOOK` / `FIRST_PERSON`. In
  `FIRST_PERSON`, tilt and rotation pivot about the camera for every caller, so an orientation
  sensor driving `setTilt`/`setMapRotation` behaves exactly like a one-finger drag, and two fingers
  move instead of panning.
- **A map view can be translucent**: `setTranslucent(boolean)` on `MapView`, `TextureMapView` and
  `MSFMapView` — with a transparent clear colour the frame is empty wherever the map does not paint.
- **`Options.PanningSpeedMode`** (default `ANCHORED`) controls whether a tilted pan re-derives its
  scale from where the finger is now. ([#55](https://github.com/massif-maps/MassifMaps/pull/55))

#### Geometry

- **`ManeuverArrowBuilder`** cuts the turn arrow a navigation app draws on a route: given a route
  and a maneuver (a position, or the point index from a `RoutingInstruction`) it returns the piece
  of the route around it as one WGS84 line feature, which the style draws with `line-end-arrow` —
  no marker, no bitmap, no label. ([#61](https://github.com/massif-maps/MassifMaps/pull/61)) —
  [docs/features/maneuver-arrows.md](docs/features/maneuver-arrows.md)

---

### Performance

Measured on an Adreno 610 device unless stated; method, cameras and the candidates that measured as
*not* the bottleneck are in
[docs/internals/performance-log.md](docs/internals/performance-log.md) and
[docs/internals/rendering/10-performance.md](docs/internals/rendering/10-performance.md).

| Area | Result | PR |
|---|---|---|
| Terrain pan (mid-range device) | 1.2 fps / 810 ms per frame → usable | [#49](https://github.com/massif-maps/MassifMaps/pull/49) |
| Label re-anchoring on DEM load | 82% of the render thread, ~750 k elevation samples/frame → targeted per-tile path | [#58](https://github.com/massif-maps/MassifMaps/pull/58) |
| City pan | 7.5 → 27 fps; GPU total 33 → 12 ms; 3.4× fewer geometry indices | [#82](https://github.com/massif-maps/MassifMaps/pull/82) |
| Style load (symbolizer context) | 277 ms → 27 ms | [#84](https://github.com/massif-maps/MassifMaps/pull/84) |
| Elevation refetching on terrain start | stopped | [#80](https://github.com/massif-maps/MassifMaps/pull/80) |
| GeoJSON tile build (long lines) | 3.3× faster on device | [#54](https://github.com/massif-maps/MassifMaps/pull/54) |
| Idle map | a still 3D map no longer re-renders every 16 ms | [#27](https://github.com/massif-maps/MassifMaps/pull/27) |
| Release build | render and tile paths compiled at `-O2` instead of `-Oz` | [#63](https://github.com/massif-maps/MassifMaps/pull/63) |
| Valhalla | built without the 19 unused service actions | [#64](https://github.com/massif-maps/MassifMaps/pull/64) |
| iOS static framework | LTO applied to the artifact that actually ships | [#67](https://github.com/massif-maps/MassifMaps/pull/67) |
| Android build | ninja + ccache: 70.9 s cold, 13.8 s warm (one arm64 Release ABI) | [#62](https://github.com/massif-maps/MassifMaps/pull/62) |
| SWIG regeneration | 28.3 s → concurrent passes | [#66](https://github.com/massif-maps/MassifMaps/pull/66) |

---

### BREAKING CHANGES

- **Everything is renamed.** `carto::` → `massif::`, `com.carto.*` → `com.massifmaps.*`,
  `NT*` → `MSF*`, `Carto.*` → `Massif.*`, `libcarto_mobile_sdk.so` → `libmassif.so`,
  `_CARTO_*_SUPPORT` → `_MASSIF_*_SUPPORT`. CartoCSS is the exception — the old spellings still
  parse with a deprecation warning. ([#99](https://github.com/massif-maps/MassifMaps/pull/99))
- **CARTO's hosted services are removed** with no renamed equivalent: `CartoOnlineVectorTileLayer`
  and the hosted basemap, the CARTO offline package endpoints, the hosted routing and geocoding
  endpoints, and the API-key / app-registration flow. Bring your own `TileDataSource` and style, use
  MBTiles or PMTiles offline, and the embedded Valhalla / SGRE engines for routing.
  ([#99](https://github.com/massif-maps/MassifMaps/pull/99))
- **C++20 is required** for Android, Apple and UWP (the MLT decoder's public headers use
  `std::span` and `std::ranges`). ([#90](https://github.com/massif-maps/MassifMaps/pull/90))
- **`TerrainOptions.DrapeLinesEnabled` now defaults to `true`.** Vector-tile lines over 3D terrain
  are baked into the drape texture rather than drawn as geometry; set it to `false` for sharp lines
  that keep their style width at any slope.
  ([#47](https://github.com/massif-maps/MassifMaps/pull/47),
  [#82](https://github.com/massif-maps/MassifMaps/pull/82))
- **`TerrainOptions.FarPlaneFactor` defaults to `2`**, so a terrain app's far plane moves unless it
  sets the factor to `0`. ([#49](https://github.com/massif-maps/MassifMaps/pull/49))
- **`Options.setFreeRoam(bool)` is replaced by `Options.setFreeRoamMode(FreeRoamMode)`** —
  `setFreeRoam(true)` becomes `setFreeRoamMode(FREE_ROAM_MODE_LOOK)`.
  ([#55](https://github.com/massif-maps/MassifMaps/pull/55))
- **`ViewState.setTerrainMinCameraZ` is replaced by `setTerrainCameraReference(terrainZ, minCameraZ)`**
  and is no longer exposed to bindings — it is renderer plumbing published once per frame.
  ([#77](https://github.com/massif-maps/MassifMaps/pull/77))
- **`TerrainOptions` loses `PainterOrderDepthEnabled`, `RegularGridEnabled` and
  `ElementTerrainSlack`.** All three were pinned to their only working values; drop the calls, there
  is no replacement. ([#88](https://github.com/massif-maps/MassifMaps/pull/88))
- **Removed classes**: `NMLModelLODTreeLayer`, `On`/`OfflineNMLModelLODTreeDataSource`,
  `NMLModelLODTreeEventListener`, `NMLModelLODTreeClickInfo`, `GDALRasterTileDataSource`,
  `OGRVectorDataSource`, `OGRVectorDataBase`, `StyleSelector`, `StyleSelectorBuilder`, and the
  `nmlmodellodtree` build profile — none was in a buildable profile, so no working app binds them.
  `NMLModel` the vector element is untouched.
  ([#88](https://github.com/massif-maps/MassifMaps/pull/88))
- **The map now goes idle when nothing changes.** It previously re-rendered continuously; anything
  that quietly relied on a free frame every 16 ms must now request one.
  ([#27](https://github.com/massif-maps/MassifMaps/pull/27))
- Any change to `all/modules/*.i` regenerates the bindings, so **every platform binding must be
  rebuilt** — gradle never runs SWIG.

---

### Bug Fixes

**Terrain**

- Camera clearance is a zoom bound instead of a corrective event ([#23](https://github.com/massif-maps/MassifMaps/pull/23))
- The drape no longer flashes white and rebuilds itself on zoom out ([#30](https://github.com/massif-maps/MassifMaps/pull/30))
- Landcover fills no longer disappear in non-draped terrain mode ([#32](https://github.com/massif-maps/MassifMaps/pull/32))
- 3D terrain labels stabilised, and the label/tile churn behind them cut ([#31](https://github.com/massif-maps/MassifMaps/pull/31))
- The shadow sun altitude is floored for the shadow pass ([#29](https://github.com/massif-maps/MassifMaps/pull/29))
- The view distance applies immediately and the ground behind it is fogged ([#39](https://github.com/massif-maps/MassifMaps/pull/39))
- Far tiles no longer render flat — the elevation level is re-clamped ([#38](https://github.com/massif-maps/MassifMaps/pull/38))
- Drape textures baked from a previous layer stack are dropped ([#43](https://github.com/massif-maps/MassifMaps/pull/43))
- Labels no longer blink against the terrain depth buffer ([#48](https://github.com/massif-maps/MassifMaps/pull/48))
- The map stays movable with the camera against the ground; the zoom pivot no longer drags the focus underground ([#77](https://github.com/massif-maps/MassifMaps/pull/77), [#82](https://github.com/massif-maps/MassifMaps/pull/82))
- The shadow caster set is kept a partition of the ground, cutting the shadow cost ([#92](https://github.com/massif-maps/MassifMaps/pull/92))
- Terrain occludes 3D extrusions ([#37](https://github.com/massif-maps/MassifMaps/pull/37))

**Labels and vector tiles**

- Labels no longer jump or disappear while panning — placement identity is stable across tile-set changes ([#16](https://github.com/massif-maps/MassifMaps/pull/16))
- Composite child layers get their labels placed ([#46](https://github.com/massif-maps/MassifMaps/pull/46))
- Line joins fixed, and contours stay meaningful zoomed out ([#50](https://github.com/massif-maps/MassifMaps/pull/50))
- Halo width restored to what the single-raster build drew ([#79](https://github.com/massif-maps/MassifMaps/pull/79))
- Terrain occlusion is queried with the camera the depth was rendered from ([#87](https://github.com/massif-maps/MassifMaps/pull/87))
- A translucent layer no longer punches its shape out of every layer after it ([#86](https://github.com/massif-maps/MassifMaps/pull/86))
- A terrain line is capped against the unpacked binormal ([#74](https://github.com/massif-maps/MassifMaps/pull/74))
- Style attachments match in composite group filters ([#28](https://github.com/massif-maps/MassifMaps/pull/28))

**Layers, data sources and UI**

- A long view distance no longer paves the horizon in fine tiles ([#59](https://github.com/massif-maps/MassifMaps/pull/59))
- The pan axes no longer go arbitrary at a vertical tilt ([#60](https://github.com/massif-maps/MassifMaps/pull/60))
- `PersistentCacheTileDataSource` no longer dereferences a failed tile ([#24](https://github.com/massif-maps/MassifMaps/pull/24))
- `MemoryCacheTileDataSource` attaches tile metadata ([#25](https://github.com/massif-maps/MassifMaps/pull/25))
- Android HTTP failures report the underlying Java exception ([#26](https://github.com/massif-maps/MassifMaps/pull/26))
- A redraw is requested after `Layers::setAll` ([#69](https://github.com/massif-maps/MassifMaps/pull/69))

---

### Build, platforms and tooling

- **Valhalla upgraded to 3.8.3** ([#103](https://github.com/massif-maps/MassifMaps/pull/103))
- **Mac Catalyst archives work again** — prelinking removed for the Catalyst slices and `date`'s
  `ios.mm` pulled in for the timezone symbols ([#104](https://github.com/massif-maps/MassifMaps/pull/104),
  [#106](https://github.com/massif-maps/MassifMaps/pull/106),
  [#107](https://github.com/massif-maps/MassifMaps/pull/107))
- **iOS framework build and the published API reference repaired**
  ([#100](https://github.com/massif-maps/MassifMaps/pull/100),
  [#102](https://github.com/massif-maps/MassifMaps/pull/102))
- **An iOS demo bench** mirroring `scripts/android-dev`, with the camera readout overlay
  ([#68](https://github.com/massif-maps/MassifMaps/pull/68),
  [#70](https://github.com/massif-maps/MassifMaps/pull/70))
- **CI**: build caching, retry resilience and composite actions
  ([#110](https://github.com/massif-maps/MassifMaps/pull/110)); the release body is generated with
  the changelog and install instructions ([#108](https://github.com/massif-maps/MassifMaps/pull/108));
  release-tag drift now fails fast ([#111](https://github.com/massif-maps/MassifMaps/pull/111)–[#118](https://github.com/massif-maps/MassifMaps/pull/118))
- **6261 lines** of code the fork will not use removed, along with 23 empty translation units per
  ABI per build ([#88](https://github.com/massif-maps/MassifMaps/pull/88))

### Documentation

- A **documentation site** with the generated Android and iOS API reference, deployed automatically
  to <https://massif-maps.github.io/MassifMaps/>
  ([#22](https://github.com/massif-maps/MassifMaps/pull/22),
  [#105](https://github.com/massif-maps/MassifMaps/pull/105))
- Everything consolidated into **one `docs/` tree**, browsable on GitHub and published verbatim,
  including the rendering internals ([#114](https://github.com/massif-maps/MassifMaps/pull/114),
  [#91](https://github.com/massif-maps/MassifMaps/pull/91)) — the frame and threads, tiles and LOD,
  the GL draw path, 3D terrain, the depth model, labels, hillshade and contours, lighting/sky/fog,
  the composite layer, performance method, and a tangram comparison

## [v5.0.0-rc.13] - 2025-10-11
### New Features
- [`332c6c6`](https://github.com/Akylas/mobile-sdk/commit/332c6c6cff5230759092a73ed83bb55cf17032e4) - add fetcDelay to `startDownloadArea` to add delay between each tile request


## [v5.0.0-rc.12] - 2025-06-30
### Bug Fixes
- [`15c9997`](https://github.com/Akylas/mobile-sdk/commit/15c999705ec054801677e2131f67044429724b4b) - nutibillboardline fix *(commit by [@farfromrefug](https://github.com/farfromrefug))*


## [v5.0.0-rc.11] - 2025-06-08
### New Features
- [`f23d26b`](https://github.com/Akylas/mobile-sdk/commit/f23d26b9a9978cd1278bdb4b73e9788de642d51d) - added the ?? operator for cartocss and mapnikvt *(commit by [@farfromrefug](https://github.com/farfromrefug))*


## [v5.0.0-rc.10] - 2025-06-02
### New Features
- [`4ceb7d2`](https://github.com/Akylas/mobile-sdk/commit/4ceb7d279b4e13efae255153d2a572a3c59b33db) - view::rotation and view::tilt *(commit by [@farfromrefug](https://github.com/farfromrefug))*


## [vv5.0.0-rc.9] - 2025-05-25
### Bug Fixes
- [`c8a1f4d`](https://github.com/Akylas/mobile-sdk/commit/c8a1f4d598cc0392ead713c461e3239bb5c2e06e) - valhalla non network penalty fix *(commit by [@farfromrefug](https://github.com/farfromrefug))*


## [v5.0.0-rc.8] - 2025-05-13
### Bug Fixes
- [`64d3035`](https://github.com/Akylas/mobile-sdk/commit/64d3035eb55c28f36ddbe6e498b6d7864a821aca) - with position line shields should not be oriented *(commit by [@farfromrefug](https://github.com/farfromrefug))*


## [v5.0.0-rc.7] - 2025-04-20
### Bug Fixes
- [`9a3256e`](https://github.com/Akylas/mobile-sdk/commit/9a3256e5e8952c69ac60a0b0a922809ca344b793) - exclude_unpaved for bicycle *(commit by [@farfromrefug](https://github.com/farfromrefug))*


## [v5.0.0-rc.6] - 2024-12-06
### Bug Fixes
- [`597cde4`](https://github.com/Akylas/mobile-sdk/commit/597cde4ab792e3caba03178c5f8b27e9a27ba5ee) - valhalla fix


## [v5.0.0-rc.4] - 2024-10-25
### :bug: Bug Fixes
- [`5ec739c`](https://github.com/Akylas/mobile-sdk/commit/5ec739c76556ca5a7da385c0a2f462656ac1c43c) - allow shield to have no shield image
- [`7fef879`](https://github.com/Akylas/mobile-sdk/commit/7fef87973e49eca5874af8be50a939fc8afea67a) - **android**: add support for 16 KB page sizes


## 5.0.0-rc.2 (2024-08-30)


### Features

* add map options to disable rotation gesture without disabling programmatic changes to rotation ([f2ce639](https://github.com/Akylas/mobile-sdk/commit/f2ce639686546d3383e0c8f48b50ba80d3cee9bf))
* add valhalla instruction to RoutingInstruction ([5bd5a9e](https://github.com/Akylas/mobile-sdk/commit/5bd5a9ed672f6361dc1e71fb6e5ceaa0f58e4c4e))
* added const predicate support (with `#variable`) ([f9f182b](https://github.com/Akylas/mobile-sdk/commit/f9f182bbe90e7e46ae70fa6b5923b5fda82a27db))
* added doubleClickMaxDuration option ([51f3861](https://github.com/Akylas/mobile-sdk/commit/51f38617a945b7c5c32fb79dd710e731047803e6))
* added doubleClickMaxDuration option ([533bdf8](https://github.com/Akylas/mobile-sdk/commit/533bdf8865cf30c4415221517f5e61a04bf167c9))
* added new options for `VectorTileSearchService` : `sortByDistance`, `layers`, `preventDuplicates` ([c890104](https://github.com/Akylas/mobile-sdk/commit/c890104385d61fe61ea294df84fcf50924dc00b1))
* added OruxDBDataSource ([748de52](https://github.com/Akylas/mobile-sdk/commit/748de526aa8ddda46dc7af2503ac439b0824b055))
* allow calculateRoute and matchRoute to return rawResult ([a092b66](https://github.com/Akylas/mobile-sdk/commit/a092b663bd0fac1bed757def9379594dc62a2faa))
* basic TextureMapView ([78d3dc7](https://github.com/Akylas/mobile-sdk/commit/78d3dc73230aea770e6ac5c63d25ce0814f0b9ed))
* basic TextureMapView ([85d31dd](https://github.com/Akylas/mobile-sdk/commit/85d31dd90f2805cb451c86aff1d1af001f4d420c))
* custom normalmap shader support ([ba27fc6](https://github.com/Akylas/mobile-sdk/commit/ba27fc6335c8f3bda1c7b94f6f67c227f274d84c))
* exagerateHeightScaleEnabled property ([4269754](https://github.com/Akylas/mobile-sdk/commit/4269754a0164c404dddf4fdc8c484b4a97c5037f))
* GeoJSONVectorDataSource all to add/remove single features ([607350b](https://github.com/Akylas/mobile-sdk/commit/607350bc1ae7920663f1c8ef30a1037f570dc6a2))
* LocalPackageManagerTileDataSource ([2df9f52](https://github.com/Akylas/mobile-sdk/commit/2df9f52d658908ad686aa8702afba957ce007564))
* maxOverZoomLevel for DataSource ([e7d7cd2](https://github.com/Akylas/mobile-sdk/commit/e7d7cd2179ee6ead478ffc58ccb066c2ba32e282))
* maxSourceOverzoomLevel ([47cc12e](https://github.com/Akylas/mobile-sdk/commit/47cc12ea425c5cac99da00a4efbd1ed8a2fd1726))
* MultiOSMOfflineGeocodingService, MultiOSMOfflineReverseGeocodingService ([92e4ece](https://github.com/Akylas/mobile-sdk/commit/92e4ece65d0643b440c366d5e8633adc0f62e57e))
* MultiValhallaOfflineRoutingService ([f6aae4f](https://github.com/Akylas/mobile-sdk/commit/f6aae4f8c609a240cc73c48a0e9de04d3d77e7c8))
* new `LayersLabelsProcessedInReverseOrder` `Options` property ([e82e9fc](https://github.com/Akylas/mobile-sdk/commit/e82e9fc45fcd6c9357179d4c383668a6ac500f70))
* new methods for `MBVectorTileDecoder`:  `setJSONStyleParameters` and `setStyleParameters` ([9ad8e69](https://github.com/Akylas/mobile-sdk/commit/9ad8e6947fee061dbda24495086669fb8d6a8671))
* normal accent color ([44394a3](https://github.com/Akylas/mobile-sdk/commit/44394a3218f84f3def4dacbb3c728c42a4922c0a))


### Bug Fixes

* added missing methods to MultiTileDataSource ([c85e6ca](https://github.com/Akylas/mobile-sdk/commit/c85e6ca934e4c800546a85844146c539722699ec))
* allow shield to have no shield image ([5ec739c](https://github.com/Akylas/mobile-sdk/commit/5ec739c76556ca5a7da385c0a2f462656ac1c43c))
* allow to add a layer to another renderer. The usecase is when an android activity is re created. You might still have the reference to the native layer which you want to add back to the carto map. It is faster to only add it again than to re create all layers ([9ba3241](https://github.com/Akylas/mobile-sdk/commit/9ba324181df07795c923250a7033914d81ab33e4))
* **android:** add support for 16 KB page sizes ([7fef879](https://github.com/Akylas/mobile-sdk/commit/7fef87973e49eca5874af8be50a939fc8afea67a))
* better support for tileMask ([a0c304e](https://github.com/Akylas/mobile-sdk/commit/a0c304e43a03e2b0fd82a0f325dfd716078575ad))
* correctly copy info.plist ([1219ff0](https://github.com/Akylas/mobile-sdk/commit/1219ff03ac18181988a89e137d4e8fd97ed68c89))
* correctly encode/decode GeoJSON properties for `GeoJSONVectorTileDataSource` so that properties with sub-objects are correctly returned in `onVectorTileElementClicked` ([313eb38](https://github.com/Akylas/mobile-sdk/commit/313eb3858541b937149eb7f0f822354526b71aaf))
* correctly handle click events on MultiPoint PointGeometry ([bcd1e83](https://github.com/Akylas/mobile-sdk/commit/bcd1e835729319807185a78a8b8c99d8e5221331))
* correctly handle valhalla route result ([f73a239](https://github.com/Akylas/mobile-sdk/commit/f73a2393c48dd2f5bd7961f649212bece0d015d5))
* correctly query points elevation ([a8a65d8](https://github.com/Akylas/mobile-sdk/commit/a8a65d8fd8278f320068721204ea3bbd4174df49))
* ensure customParameters are always applied ([8bda9ed](https://github.com/Akylas/mobile-sdk/commit/8bda9ed5fbdd5299ec3bf8e8e06b6f382313c456))
* ensure pointIndex is good on multi leg/trip ([659d10d](https://github.com/Akylas/mobile-sdk/commit/659d10ddff3f072e6f1d5f185535aceb0d6fd036))
* fix after merge ([e304cfc](https://github.com/Akylas/mobile-sdk/commit/e304cfc2a2806258d9eda44bc6791a7384cbb2f9))
* fix for api name change ([148eaae](https://github.com/Akylas/mobile-sdk/commit/148eaaedfe240903371c141273e31942c16ec580))
* for now dont crash on wrong geojson feature ([bfc62f9](https://github.com/Akylas/mobile-sdk/commit/bfc62f9d283119f66784d16dcbe589eaad3afeaf))
* fully fixed normalIlluminationMapRotationEnabled ([14ba203](https://github.com/Akylas/mobile-sdk/commit/14ba203201310b0397bb79d16c16279cb2fd9d10))
* hillshade exageration fix on overzoom ([b07c869](https://github.com/Akylas/mobile-sdk/commit/b07c869187dabad03e8478acd1df2e5d8763a0fe))
* hillshade getElevation(s) handle isReplaceWithParent ([d22602f](https://github.com/Akylas/mobile-sdk/commit/d22602f6a96c16d13b395d26a1471bffcdcce2d0))
* hillshader overzoom fix ([19f48bd](https://github.com/Akylas/mobile-sdk/commit/19f48bd474ee41fafe197352e880d22958d3397f))
* if replaced with parent we should return the other one ([c8ea35c](https://github.com/Akylas/mobile-sdk/commit/c8ea35cb62c9bc39591880aa833b204ed22b32fd))
* LocalPackageManagerTileDataSource working ([96b14e9](https://github.com/Akylas/mobile-sdk/commit/96b14e93876be7d3a42812920310fa8b16f815d9))
* missing update for LIGHTING_SHADER_NORMALMAP with accent_color ([1195473](https://github.com/Akylas/mobile-sdk/commit/119547308eee39f34e965b0e2ab204b977919dab))
* MultiDataSource supports maxOverZoomLevel ([36320ad](https://github.com/Akylas/mobile-sdk/commit/36320ad0fd5a655f5e18fb129372c329defd1fc5))
* request parent tile if isReplacedByParent ([5108d4b](https://github.com/Akylas/mobile-sdk/commit/5108d4bd907784cc205c1f28e62f0a85ed8cfbb8))
* searchProxy fix by allowing searchRadius<0 to disable distance check ([65102fc](https://github.com/Akylas/mobile-sdk/commit/65102fcc2d19273aa8e54d2503f671b98cfe146a))
* shader dymanic change fix ([09d144c](https://github.com/Akylas/mobile-sdk/commit/09d144cdcb0d54dd1773dfecbb8ef4fde90c0ae3))
* some JNI cleanup ([6df0354](https://github.com/Akylas/mobile-sdk/commit/6df0354e98bbc5b8163e10e4e1eee853fd475ef9))
* some MultiDataSource improvements ([7e6c8bd](https://github.com/Akylas/mobile-sdk/commit/7e6c8bdbc3090ba21e4541eb3c2dd2a1136302ba))
* support lite mode ([5af204d](https://github.com/Akylas/mobile-sdk/commit/5af204d4d1244ed6f25a94667303aa947be16f26))
* try to fix build on macos ([6357995](https://github.com/Akylas/mobile-sdk/commit/6357995a29bdf95fde03c4238b084618f838e236))
* trying to improve workflow for versioning ([8975155](https://github.com/Akylas/mobile-sdk/commit/8975155ead0604da5e47aa24a4ec903869dc295b))
* ValhallaOnlineRoutingService allow creating without apiKey ([65d8895](https://github.com/Akylas/mobile-sdk/commit/65d8895be14d0b5ef5254ef4c0d9eae1c240ddfa))
* working MultiTileDataSource (renamed from LocalPackageManagerTileDataSource) ([527b8b9](https://github.com/Akylas/mobile-sdk/commit/527b8b95ab4ffdc980e179226545d60534d9429f))

CARTO Mobile SDK 4.4.7RC1
-------------------

### New features:

* Added support for generic expressions in CartoCSS 'Map' element.
* Added support for CartoCSS 'line-miterlimit' property, tweaked join handling in case of offsets/patterns. 
* Generalized CartoCSS font support, added support expression based face names

### Changes, fixes:

* Fixed Angle UWP related threading issues, if multiple views were used.
* Fixed minor synchronization issue with RasterTileLayer
* Improved handling of null blob in TileData
* Improved normal map building for overzoomed tiles, resulting is less artifacts.
* Improved reporting of .so loading errors on Android (re-throw original exception, instead of just logging/failing afterwards)
* Added handling of 'OnPointerExited' event in UWP MapView
* Build script fixes, fixes related tolatest Python versions, Android NDK25 support


CARTO Mobile SDK 4.4.6
-------------------

### Changes, fixes:

* Fixed minor rendering issue with lines joined at steep angles when BEVEL/ROUND join modes were used


CARTO Mobile SDK 4.4.6RC1
-------------------

### New features:

* Added 'getTimeout', 'setTimeout' methods to 'CartoOnlineTileDataSource', 'MapTilerOnlineTileDataSource' and 'HTTPTileDataSource'

### Changes, fixes:

* Fixed iOS specific issue related to SDK not properly handling 'didBecomeActive' notifications, resulting in MapView not being rendered.
* Fixed critical synchronization issue on UWP platform related to stopping rendering loop.
* Fixed flickering issues when MapView was resized on UWP platform.
* Fix global pattern alignment when using 'polygon-pattern' symbolizer.


CARTO Mobile SDK 4.4.5
-------------------

### Changes, fixes:

* Fixed 'PersistentCacheTileDataSource' tile preload canceling not working
* Fixed several cases where tile datasources could be accessed with tile coordinates out of bounds


CARTO Mobile SDK 4.4.5RC1
-------------------

### New features:

* Added 'setFeatureIdOverride' and 'isFeatureIdOverride' methods to 'MBVectorTileDecoder'
* Added 'isAnimationStarted' method to 'MapInteractionInfo'

### Changes, fixes:

* Fixed critical issue with non-ASCII string wrapping on UWP platform
* Fixed missing 'onMapInterAction' callback on double tap zoom
* Changed user initiated zoom behaviour when 'PIVOT_MODE_CENTERPOINT' mode is used, now screen center is used as a pivot point.
* Updated harfbuzz, libwebp and pugixml dependencies to latest stable versions
* Fixed stack overflow issue in external css2xml utility due to missing rules for EXP/LOG functions
* Added 'build id' to Android shared libraries, to help analyze Android native stack traces


CARTO Mobile SDK 4.4.4
-------------------

### New features:

* Feature id is now accessible in CartoCSS using 'mapnik::feature_id' variable

### Changes, fixes:

* Fixed issues with 'feature id' handling in vector tile renderer when feature was used in multiple layers
* Updated harfbuzz dependency to the latest stable version
* Fixed wrong compilation profile used for UWP builds, resulting in missing a few features
* Dropped 'PersistentCacheTileDataSource' from 'lite' compilation profile, making 'lite' SDK build smaller
* Minor tweaks to built-in styles, related to admin boundaries
* Minor fixes related to non-standard SDK profiles
* Minor optimizations


CARTO Mobile SDK 4.4.4RC1
-------------------

### New features:

* Added 'getDefaultLayerBuffer', 'setDefaultLayerBuffer' methods to 'GeoJSONVectorTileDataSource'. This allows controlling buffer size (in tile pixels) for vector tile layers.

### Changes, fixes:

* Restored support for arbitrary expressions in transform arguments (available in 4.3.x but removed from 4.4.0-4.4.3)
* Improved batching for transformed geometries, all non-translated geometries can be now added into a single batch.
* Fixed shield symbolizer issues where background was affected by fill color.
* Fixed several clipping related issues in 'GeoJSONVectorTileDataSource'
* Improved EAGLContext handling for iOS, workaround for a crash when a view is moved out of a window and then back


CARTO Mobile SDK 4.4.3
-------------------

### New features:

* Added an experimental option to configure various 'VectorTileLayer' parameters via project.json nutiparameters
* Added support for configuring vector tile map parameters via project.json
* Updated boost dependency to the latest stable version

### Changes, fixes:

* Build script cleanup


CARTO Mobile SDK 4.4.3RC3
-------------------

### New features:

* Added 'getRendererLayerFilter', 'setRendererLayerFilter', 'getClickHandlerLayerFilter', 'setClickHandlerLayerFilter' methods to 'VectorTileLayer'. These methods allow ignoring certain layers for rendering or click detection.
* Added 'reverse' function support to CartoCSS 'text-transform'

### Changes, fixes:

* Dropped 'doclava' based javadoc generation, documentation for Android is now based on standard JDK doclet
* Improved Android documentation by hiding unneeded wrapping related details
* Fixed regression in 4.4.3RC2 related to parallel requests to 'ValhallaOfflineRoutingService'
* Added better support for 'none' keyword in CartoCSS
* Minor improvements to error reporting for CartoCSS issues
* Fixes and cleanups in Android build script
* Updated internal FreeType library to latest stable version
* Minor speed and size optimizations


CARTO Mobile SDK 4.4.3RC2
-------------------

### Changes, fixes:

* Fixed 'TileLayer' not properly recalculating tiles when visibility changes, causing layer to remain hidden.
* Fixed deadlock in 'ClusteredVectorLayer' when its data source is non-empty with all elements being hidden
* Fixed stale tiles remaining in caches when offline packages were removed
* Fixed subtle synchronization issues in 'PackageManager'
* Added support for parallel requests to 'ValhallaOfflineRoutingService'
* Added javadoc to published Android artifacts to Maven central
* Minor fixes to iOS build script
* Updated internal libjpeg-turbo, harfbuzz libraries to latest stable versions


CARTO Mobile SDK 4.4.3RC1
-------------------

### Changes, fixes:

* Fixed critical coordinate scaling issue in iOS Metal build (occurs only with iPhone 6 Plus, iPhone 7 Plus and iPhone 8 Plus devices)
* Fixed regression in 'GeoJSONVectorTileDataSource' which caused parsing failure with features with non-object properties
* Optimized parsing of complex CartoCSS styles, improving performance by 20-40% for complex styles
* Optimized loading of compiled 'Mapnik' styles by using symbolizer cache, improving performance by up to 50% for complex styles
* Updated internal Valhalla, sqlite, harfbuzz, botan and protobuf libraries to latest stable versions


CARTO Mobile SDK 4.4.2
-------------------

### Changes, fixes:

* Fixed style fallback version in 'CartoPackageManager' (when using 'startStyleDownload' method)
* Changed exception type when encoutering unsupported geometry in 'GeoJSONVectorTileDataSource'
* Minor iOS build script fixes


CARTO Mobile SDK 4.4.2RC1
-------------------

### New features:

* Added 'setSimplifyTolerance', 'getSimplifyTolerance' methods to 'GeoJSONVectorTileDataSource'
* Added support for complex CartoCSS selectors ('when' selectors)
* Added support for 'bevel', 'none' linejoin modes and 'square' linecap mode in CartoCSS.
* Added 'marker-color' property to CartoCSS that can be applied to both file-based markers and built-in markers.

### Changes, fixes:

* Started using API 31 as compilation target on Android
* Implemented better error reporting of undefined variables in CartoCSS translator
* Fixed deadlock in NMLModel.setRotation(axis, angle) method caused by improper synchronization
* Reimplemented 'setLayerFeatureCollection' method in 'GeoJSONVectorTileDataSource' to make it faster by skipping serialization/parsing steps.
* Implemented switching to 'bevel' linejoin at sharp angles when using 'miter' linejoin
* Fixed multiple issues with string escaping in parsers and generators in CartoCSS and MapnikVT library.
* Fixed minor issues related to internal expression -> predicate conversion in MapnikVT library.
* Fixed dash array generation for subpixel wide lines when rendering vector tiles
* Revised feature id generation logic in 'GeoJSONVectorTileDataSource', SDK now uses feature id, if available or a deterministic auto id generation when not available.
* Reduced default simplication tolerance for 'GeoJSONVectorTileDataSource', new default value should not generate visible simplification artifacts
* Converted CartoCSS 'marker-opacity' property to a view-level parameter, so it can be dependent on 'view::zoom'.
* Updated protobuf and harfbuzz libraries to the latest versions
* Disabled Sqlite locking extensions on iOS and MacCatalyst builds
* Minor optimizations


CARTO Mobile SDK 4.4.1
-------------------

### Changes, fixes:

* Set minimum target to iOS 10 for i386 simulator target (due to thread_local not supported on iOS 9)
* Added libc++, libz dependencies to modulemap of iOS framework
* Updated build scripts to support building Swift Packages of the SDK
* Fixed SDK/MetalANGLE linking issue with iOS Metal build causing uncaught exceptions due to networking problems


CARTO Mobile SDK 4.4.1RC2
-------------------

### Changes, fixes:

* Fixed excessive initialization times when MBTilesTileDataSource was used with databases not containing zoom level metainfo
* Fixed potential memory leaks on iOS when network requests fail
* Added 'setDoubleClickMaxDuration' and 'getDoubleClickMaxDuration' methods to Options class
* Added 'extends' support to JSON project files, to reduce copy-paste declarations in map project files
* Added support for CartoCSS 'line-offset', 'line-pattern-offset' attributes
* Added support for CartoCSS 'text-wrap-character' and 'shield-wrap-character' attributes
* Added the following color manipulation functions to CartoCSS: 'hsl', 'hsla', 'red', 'green', 'blue', 'alpha', 'hue', 'saturation', 'lightness'
* Fixed handling of 'text-min-distance' and 'shield-min-distance' CartoCSS parameters
* Improved label id generation for repeated labels, creating more stable label placements
* Minor tweaks to built-in styles
* Minor optimizations to iOS Metal build
* Updated libjpeg, libwebp, freetype, harfbuzz, miniz to latest stable versions
* Minor optimizations


CARTO Mobile SDK 4.4.1RC1
-------------------

### New features:

* Metal build of iOS framework now supports Mac Catalyst apps
* Added ClickInfo class, to store click related information (click type, duration)
* New mode for reducing click event latency when double click handling is not required


### Changes, fixes:

* Re-implemented 'click type detection disabled' mode, click events are now triggered when finger is lifted
* Added setDoubleClickDetection, isDoubleClickDetection methods to Options class to allow reducing click handling latency
* Added setLongClickDuration, getLongDuration methods to Options class to allow configuring long click detection duration
* Classes like MapEventListener, VectorElementClickInfo now contain ClickInfo instance for additional click attributes
* Added support for decoding proprietary Apple 'PNG' files
* Fixed decoding of specific bitmap formats when using CreateBitmapFromUIImage on iOS
* Fixed Android bitmap decoding when non-standard stride sizes are used
* Fixed tile layer refreshing issue when data source bounds changed
* Fixed old view state being used when adding labels to the vector layer
* Updated built-in style asset, tweaked displaying of multilingual names
* Updated MetalANGLE library to the latest stable version, tweaked build settings to produce smaller binaries
* Updated font rendering libraries, tesselation library to the latest stable version
* Various minor optimizations


CARTO Mobile SDK 4.4.0
-------------------

### Changes, fixes:

* Fixed CartoCSS string-expression evaluation issue, causing some misoptimizations
* GeoJSONGeometryReader and GeoJSONGeometryWriter are now RFC7946 compliant and accept null geometry in features.
* GeoJSONVectorTileDataSource now supports features with null geometry and non-object properties
* Added support for shorthand-encoding of 'nutiparameters' in project.json files
* SDK now catches feature processing exceptions earlier and report thems without causing whole tile decoding to fail.
* Fixes to iOS build scripts


CARTO Mobile SDK 4.4.0RC4
-------------------

### Changes, fixes:

* Fixed iOS Cocoapod packaging issues, causing issues with MetalANGLE framework when used within other frameworks
* Introduced 'carto.utils.DontObfuscate' annotation for Android Java library. This can be used to finetune Proguard obfuscation rules.
* Optimized protobuf library compilation, making SDK binaries 3-5% smaller.
* Replaced Cryptopp library dependency in SDK with Botan library, fixing portability issues
* Enabled 'tile blending speed' attribute for HillShaderRasterTileLayer (default value is 0). This also fixes blending artifacts when using the layer.
* Various fixes and tweaks in SDK build scripts


CARTO Mobile SDK 4.4.0RC3
-------------------

### Changes, fixes:

* Fixed issues iOS with simulator targets not working due to problems with latest cryptopp library
* Fixed issues with some 32-bit Android targets due to problems with latest cryptopp library
* Fixed potential deadlock issue with TouchHandler class. Removed redundant 'onMapMoved' callbacks.
* Fixed potential deadlocks in AnimationHandler and KineticEventHandler when certain SDK APIs were used in MapEventListener callbacks
* Changed compilation flags for 32-bit Android targets to make then compatible with really old devices not supporting NEON extensions
* Tweaked compilation flags for Android, binary sizes are now about 10% smaller while critical code paths are better optimized
* Enabled Link Time Code Generation for UWP builds. This results in smaller and faster binaries.
* Various fixes and tweaks in SDK build scripts


CARTO Mobile SDK 4.4.0RC2
-------------------

### New features:

* Implemented smarter caching logic for CARTO online tile sources. New implementation can keep larger number of tiles in memory and uses better zoom-based tile prioritization during eviction.
* Added getLayerBlendSpeed, setLayerBlendSpeed, getLabelBlendSpeed, setLabelBlendSpeed methods to VectorTileLayer, for controlling transition animations.
* Added getTileBlendSpeed, setTileBlendSpeed methods to RasterTileLayer, for controlling transition animations.

### Changes, fixes:

* Fixed critical regression in GeoJSONVectorTileDataSource causing 'unknown pbf type' errors
* Fixed rendering artifacts with larger halo radiuses in vector tile renderer
* Fixed regression with tile loading canceling, causing updates to vector tiles being slow
* Fixed potential synchronization issues regarding tile invalidation and caching
* Fixed layers not being correctly refreshed in rare cases
* Implemented more robust time interval calculation for transition animations
* Various fixes in build scripts


CARTO Mobile SDK 4.4.0RC1
-------------------

### New requirements:

* Android 3.0 (API 11), previously 2.3 (API 9)
* iOS 9.0, previously 7.0
* CocoaPods 1.10.1, previously 1.6

### Key highlights:

* Much faster CartoCSS processing and compilation. Loading and initialization of CARTO vector layers is now about 3x faster. 
* 30-40% faster vector tile decoding performance and 10% lower memory consumption during decoding.
* Reworked tile loading and prefetching algorithms to provide more responsive UX.
* 3D NML models can now be used together with bitmap markers, with same basic features (auto orientation, transition animations, overlap analysis)
* Built-in Valhalla 3.1 routing engine vs Valhalla 3.0 in SDK 4.3.x.
* New 'TextureMapView' class for Android for applications that need to use 'MapView' with fragments.
* Additional map callback that provides detailed information about the user interactions.
* SDK for iOS is now distributed as XCFramework. Previous SDK versions used Universal Frameworks with 'fat binaries'.
* There are now two prebuilt versions of iOS frameworks: a legacy version using OpenGLES rendering backend and a new version using OpenGLES -> Metal API converter that does not use deprecated iOS APIs.
* SDK built-in vector styles now include fonts and glyphs for Arabic, Hebrew, Georgian and Armenian locales.

### New features:

* Added TextureMapView class and MapViewInterface interface to the SDK. TextureMapView is a subclass of android.view.TextureView and behaves better in apps built from fragments. MapViewInterface provides a common interface for both MapView and TextureMapView.
* Added getDescription method to RoutingInstruction. This provides textual description of the instruction. The description depends on the routing instruction, it can be either generated by the engine or by the SDK.
* Added UI based interaction callback to MapEventListener (onMapInteraction method). The callback receives detailed information about the type of the interaction.
* NMLModel is now a subclass of Billboard. This allows using billboard features like special scaling, orientation modes and transition animations for 3D models.

### Removed features and API changes:

* Removed deprecated compressToPng method from Bitmap (replaced with compressToPNG)
* Removed deprecated NMLModel constructors (replaced with constructors with NMLModelStyle argument)
* Removed getGeometryTagFilters and setGeometryTagFilters methods from RoutingRequest. They are replaced with getPointParameter/setPointParameter methods (with 'geometry_tag_filter' parameter)
* Removed setResolution method from TorqueTileDecoder, changed 'resolution' definition for getResolution method to reflect actual resolution defined in CartoCSS

### Changes and fixes:

* Tile prioritization during tile loading has been reworked to provide quicker feedback, by fetching shared parent tiles when appropriate
* Cancelling of tile loading and decoding is more flexible, puts less pressure on tile caches
* Removed duplicate points in Valhalla routing results, consecutive manuevers can now share the endpoints. This uses the same convention as other routing engines, but may potentially break apps that depend on the old behaviour.
* SolidLayer is now deprecated. If really needed, a custom VectorTileLayer or RasterTileLayer can be used instead.
* CartoOnlineRoutingService is now deprecated, third party online routing services should be used instead
* Address is now depreacted and will be removed in future versions. use GeocodingAddress instead (currently a subclass of Address)
* setRotationAngle, getRotationAngle methods are deprecated in NMLModel, use setRotation, getRotation instead.
* Added setRotationAxis, getRotationAxis methods to NMLModel
* Added setOrientationMode, getOrientationMode, setScalingMode, getScalingMode methods to NMLModelStyleBuilder
* Added getOrientationMode, getScalingMode, getModelAsset methods to NMLModelStyle
* All street names (separated using '/') are now included in Valhalla routing results
* Fixed billboard size animations not working when using BILLBOARD_SCALING_WORLD_SIZE size mode
* Fixed potential native crash when geocoding databases were corrupted
* Fixed potential native crash when map packages were corrupted
* Tweaked memory usage of offline packages, fixed potential issues with read/write access rights
* Made SolidLayer work in globe mode
* Added bitmap argument nullptr check to SolidLayer constructor
* Fixed lighting direction calculation in NMLModelLODTreeRenderer (wrong sign)
* Added getAnimationDuration method to TorqueTileDecoder
* Added Resolution property to TorqueTileDecoder for dotnet APIs.
* Fixed getParent method in MapTile to handle negative tile coordinates
* Fixed NMLModel rotation in globe rendering mode
* Fixed complex offline geocoding queries failing due to memory constraints
* Fixed slow loading of Torque tiles
* Optimized handling of color interpolation expression in vector tile renderer
* Started using latest FreeType and HarfBuzz libraries to render localized names
* Replaced 'msdfgen' Signed Distance Field glyph render with official FreeType SDF glyph renderer.
* Reduced memory reallocation when decoding vector tiles
* Dropped glyph preloading when generating fonts to speeds up map initialization
* Improved error reporting for CartoCSS interpolation expression issues
* Better handling and optimization of 'match' operator when compiling CartoCSS property sets
* Implemented various MBVT decoder optimizations, including decoded geometry cache
* Added extra vector tile label sorting rule, to make visible label selection more deterministic
* Added model color support for NML models. This can be set using setColor method in NMLModelStyleBuilder.
* Added support for generic 'frame-offset' filters for Torque styles. Previously only equal comparison was available.
* Added support for cumulative data aggregation for Torque layers
* Changed vector tile background rendering order, fixed stencil configuration detection when FBOs are used.
* Optimized rendering of VT layers with 'comp-op' defined.
* Fixed potential issues when calculating intersections with 3D polygons.
* Changed internal vector tile rendering order, rendering is done done strictly per-layer, not per-tile. This fixes issues when stencil buffer is not available or switched off (Torque rendering). 
* Fixed orientation angle interaction with line placements in TextSymbolizer
* Tweaks to marker placements on line geometry when using MarkersSymbolizer
* Changed argument types of setCapacity in cache classes from unsigned int to unsigned long on iOS, so that >4GB caches can be used on 64-bit targets.


CARTO Mobile SDK 4.3.5
-------------------

### Changes/fixes:

* Minor documentation fixes and updates


CARTO Mobile SDK 4.3.5RC1
-------------------

### Changes/fixes:

* Fixed handling of 'CANCEL' touch actions in Android. This caused mishandling of following touch events.
* Fixed thread race issue when connecting Java directors, causing issues with classes instantiaton
* Changed iOS framework packaging. Fixed several issues with header files, added support for xcframeworks.
* Fixed performance issue when calculating scaling of 3D polygons


CARTO Mobile SDK 4.3.4
-------------------

### Changes/fixes:

* Fixed out of range memory access issues when packing large VT geometries
* Fixed an issue in VT line clipping implementation causing missing initial vertices in border cases
* Optimizations when converting GeoJSON data to vector tile format (GeoJSONVectorTileDataSource)


CARTO Mobile SDK 4.3.4RC1
-------------------

### Changes/fixes:

* Added support for setting routing parameters to SGREOfflineRoutingService (setRoutingParameter, getRoutingParameter methods)
* Added 'placement-priority' support for vector tile labels, allowing setting priorities for individual labels
* Added onSurfaceChanged event to MapRendererListener. This method is called when map is resized.
* Reduced rendering artifacts of wide dashed lines in vector tile renderer
* Better precision when compressing vector tile coordinates, fixes rare visual artifacts
* Fixed critical Xamarin iOS synchronization redrawing/disposing issues, causing exceptions
* Fixed VectorTileLayer rendering issue related to opacity handling
* Fixed watermark options being ignored after initial rendering
* Fixed non-opaque highlight/shadow color handling in HillshadeRasterTileLayer
* Additional safety checks in Android bitmap conversions


CARTO Mobile SDK 4.3.3
-------------------

### Changes/fixes:

* Fixed regression in label ray-hit detection routine when using globe mode


CARTO Mobile SDK 4.3.3RC2
-------------------

### Changes/fixes:

* Fixed critical content scaling issue on iPhone Plus devices
* Started using API 30 as compilation target on Android


CARTO Mobile SDK 4.3.3RC1
-------------------

### Changes/fixes:

* Fixed PersistentCacheTileDataSource not working with large cache files
* Faster initialization of PeristentCacheTileDataSource with large database files
* Tweaks and fixes to vector tile feature click detection, marker images are now used to detect transparent pixels


CARTO Mobile SDK 4.3.2
-------------------

### Changes/fixes:

* Fixed PersistentCacheTileDataSource not working with large cache files
* Changed PersistentCacheTileDataSource to be more conservative when estimating cache file size


CARTO Mobile SDK 4.3.2RC2
-------------------

### Changes/fixes:

* Fixed multiline RTL text formatting in VT renderer


CARTO Mobile SDK 4.3.2RC1
-------------------

### Changes/fixes:

* Added two new properties to HillshadeRasterTileLayer: shadow color and highlight color
* Minor optimization: avoid tile reloading when listener is disconnected from the layer.
* Slighlty higher background thread priority for tile/data loading tasks
* Added dynamic thread creation to CancelableThreadPool when all workers are busy with lower priority tasks. 
* Fixed transform/orientation being ignored when flipping vector tile labels
* Enabled SIMD optimizations for WebP image library for slight performance boost


CARTO Mobile SDK 4.3.1
-------------------

### Changes/fixes:

* Fixed a crashing issue with ClusteredVectorLayer
* Minor documentation updates


CARTO Mobile SDK 4.3.1RC1
-------------------

### Changes/fixes:

* Added HillshadeRasterTileLayer to the SDK. It can be used to add additional height-based shading to the map.
* Added getTileFilterMode/setTileFilterMode methods to RasterTileLayer. This allows to choose raster tile filtering mode between point, bilinear and bicubic filters.
* Changed lighting vector calculation for globe mode - the lighting vector is now always based on the local tangent frame of focus point
* Deprecated compressToPng method in Bitmap class, use compressToPNG instead
* Fixed issues with HTTPTileDataSource when multiple tile download threads were used on iOS, by making HTTPClient thread safe
* Fixed potential native crash when thread pool is downsized
* Fixed thread race between layers and renderers when GL context was lost
* Fixed compatibility issues with older GPUs not supporting high precision in fragment shaders
* Slightly better error reporting for CartoCSS errors
* Slightly better error reporting for PNG reading errors


CARTO Mobile SDK 4.3.0
-------------------

### Changes/fixes:

* Fixed linking issue with Xamarin iOS build
* Minor optimizations for Android build when using JNI
* Minor logging cleanup
* Documentation changes


CARTO Mobile SDK 4.3.0RC3
-------------------

### Changes/fixes:

* Changed shading of building symbolizers in VT renderer. The lighting is now NOT applied to the top of the building. This matches the behavior of Mapnik.
* Enabled support for rendering buildings with negative height in VT renderer
* Fixed cyclical resource manager referencing, causing memory leaks on Android
* Fixed potential timing related crashes happening when disconnecting layers from MapRenderer
* Fixed a deadlock regression in 4.3.0RC2 when bitmap texture cache was released
* Fixed an issue in layer removal code that could cause removing more layers than intended


CARTO Mobile SDK 4.3.0RC2
-------------------

### Changes/fixes:

* Fixed memory leak when switching render projection
* Thread safety fixes when adding/removing layers to the map
* Fixed memory leak in iOS implementation of HTTPClient
* Throw exception instead of crashing when null ptr is used as an argument for Bitmap constructor
* Fixed very high memory consumption when calling getServerPackages method in PackageManager class
* Optimized internal representation of tilemask, reduced memory usage by 5x
* Fixed RasterTileListener not working (regression in SDK 4.2.x vs 4.1.x)
* Fixed issue with font shaping when '\n' symbol is used in text
* Fixed texture coordinate artifacts when using dashed lines in VT renderer
* Removed unneeded error from the log when translating CartoCSS expressions ('Unsupported text expression type ..')
* Updated the way CartoCSS 'marker-feature-id' and 'text-feature-id' properties treat null/zero values and empty strings: now using these special values forces SDK to generate 'auto id'
* Fixed potential vector tile rendering issues on devices that supported OpenGL Vertex Array extension
* Optimized resource usage when layers are removed from the map, the resources are released sooner than before, resulting in smaller application memory footprint
* Fixed rare display corruption issues when OpenGL surface was lost and layers were being removed from the map
* Fixed styling issues with VectorLayers when bitmaps were shared between different vector element styles
* Implemented proper 'line-cap' support for dashed lines
* Added 'custom parameters' option to GeocodingRequest and ReverseGeocodingRequest classes. Custom parameters can be used to customize specific parameters of geocoding engines.


CARTO Mobile SDK 4.3.0RC1
-------------------

This version is a major update and brings several new features and optimizations. Note that due to the inclusion of Valhalla 3,
then binaries of the SDK are considerably larger on Android compared to SDK 4.2.x.

### Key highlights:

* Valhalla 3 routing support. Valhalla 2 routing was supported in SDK 4.1.x and removed from SDK 4.2.x. This release brings Valhalla back but with new major version and lots of improvements. Note that previous Valhalla 2 offline packages are incompatible with Valhalla 3 and can not be used.
* Support for building the SDK with Metal rendering backend on iOS, instead of OpenGLES. This is currently still experimental, as it generates larger binaries and is a bit slower.

### New features:

* A fully featued matchRoute API for matching points to routing network and extracting routing attributes. 
* Added custom metadata support for Layer class (getMetaData, setMetaData, containsMetaDataKey methods in Layer class)
* Support for rendering basemap Point-of-Interests, API for directly controlling POI/building rendering mode (setPOIRenderMode/getPOIRenderMode methods in CartoVectorTileLayer class)
* API for controlling the render style of basemap buildings (setBuildingRenderMode/getBuildingRenderMode methods in CartoVectorTileLayer class)
* Added 'custom parameters' option to RoutingRequest and RouteMatchingRequest classes. Custom parameters can be used to customize routing schemas of specific routing engines.
* New helper classes FeatureBuilder and VectorTileFeatureBuilder
* Moved matchRoute method to base RoutingService interface
* Moved setProfile/getProfile methods to base RoutingService interface
* Moved setLanguage and setAutocomplete methods to base GeocodingService interface.
* Added setMaxResults and getMaxResults methods to base GeocodingService interface.
* Moved setLanguage method to base ReverseGeocodingService interface.
* Added setClickRadius and getClickRadius methods to VectorTileLayer
* Added setMaxResults and getMaxResults methods to all search services. Note that searches are now capped, thus applications may need to configure the limit appropriately.
* Added 'uppercase', 'lowercase', 'length', 'concat', 'match', 'replace' functions to CartoCSS compiler.
* Added 'regexp_ilike' operator to the search API query language to perform case-insensitive substring matching
* Added support for ARM64 UWP target, removed deprecated ARM UWP target.

### Changes/fixes:

* setGeometryTagFilters, getGeometryTagFilters methods in RoutingRequest are deprecated and will be removed in future versions. Instead use more general setPointParameter/getPointParameter methods with 'geometry_tag_filter' parameter name.
* Labels from different VectorTileLayer instances that have 'allow-overlap' flag set to false no longer overlap each other. This changes previous behavior where each VectorTileLayer did not affect other layers.
* SDK does not throw exception anymore when package manager device keys do not match, this fixes issues with TestFlight on iOS
* Tweaked and optimized offline geocoder, mostly affects autocomplete mode
* Better reporting of online Valhalla routing errors
* Added ferry instruction types (enter/leave ferry) to RoutingAction enum
* Fixed search API issues with tiles and non-closed polygons
* Tweaked rendering of lines with round join types to look smoother, especially when used with thin lines
* Suppressed GLKView deprecation warnings on iOS
* Additional NPE safety in OnlineNMLModelLODTreeDataSource
* Fixed native crash when loading 0-sized image files
* Minor improvements to CartoCSS error reporting.
* Made Mapnik-level string expression parsing recursive, fixes subtle issues with complex expressions
* Better SVG compatibility with RGBA color support


CARTO Mobile SDK 4.2.2
-------------------

### Changes/fixes:

* Fixed iOS specific compilation warning in NTExceptionWrapper.h ("This function declaration is not a prototype")
* Disabled LTO on iOS builds (fixes issue with bitcode generation on iOS platform)


CARTO Mobile SDK 4.2.2RC2
-------------------

### Changes/fixes:

* Fixed vector tile click radius of points if 'allow-overlapping' flag was set to true
* Fixed name wrapping of setWatermarkPadding method in Options class on iOS (was setWatermarkPaddingX, now setWatermarkPadding)
* Clearer error reporting when parsing CartoCSS styles
* Improvements and tweaks to text-on-line rendering in vector tiles


CARTO Mobile SDK 4.2.2RC1
-------------------

### Changes/fixes:

* Additional synchronization for iOS events to prevent potential GL calls while app is paused
* Fixed wrong rendering of vector tile labels using 'point-placement' mode 
* Fixed vector tile label transformation handling
* Speed and memory usage optimizations for vector tile labels
* Minor improvements to CartoCSS error reporting


CARTO Mobile SDK 4.2.1
-------------------

### Changes/fixes:

* Optimized symbol tables in Android .so libraries so SDK is now 5% smaller
* Fixed a potential NPE crash in VT glyph rendering code


CARTO Mobile SDK 4.2.1RC2
-------------------

### Changes/fixes:

* Tweaks to built-in styles to better prioritise rendering of low rank street names
* Better Mapnik compatibility by supporting linestring geometry in PolygonSymbolizer, PolygonPatternSymbolizer and BuildingSymbolizer
* Minor tweaks to line placement clipping against frustum in VT renderer
* Use constant padding around labels, fixes obscure issues with label click area for long texts
* Fixed issue with label click handling - due to label geometry merging wrong geometry was returned in certain cases


CARTO Mobile SDK 4.2.1RC1
-------------------

### Changes/fixes:

* The SDK can now be used without calling registerLicense method of MapView class if CARTO basemap services are not needed. In 4.2.1 and later versions we are showing normal CARTO watermark instead of evaluation watermark in this case.
* Added MapTilerOnlineTileDataSource class that can be used for MapTiler or OpenMapTiles tiles
* Added getGeometryTagFilters/setGeometryTagFilters methods to RoutingRequest; they can be used to filter routing endpoints. This is currently supported only when using SGRE routing engine.
* ValhallaOnlineRoutingService is now included in the standard SDK build. It was available in 4.1.x versions but removed from 4.2.0.
* Added clear methods to VariantArrayBuilder and VariantObjectBuilder classes
* Changed the behavior or MapView screenToMap and mapToScreen methods if called before view size is initialized - the SDK now returns NaNs
* CartoPackageManager constructor now throws an exception if it is instantiated without a valid license
* protected loadConfiguration method in CartoOnlineTileDataSource is no longer exposed
* Fixed MapView background clearing issue with Android Q beta versions


CARTO Mobile SDK 4.2.0
-------------------

### Changes/fixes:

* Added support for 'marker-feature-id', 'text-feature-id' and 'shield-feature-id' CartoCSS properties for uniquely identifying labels
* Fixed regression in 4.2.0RC2 vs RC1 regarding VectorTile hit results ordering
* Fixed render projection switching issues in 4.2.0RC1/RC2
* Fixed kinetic rotation clamping issue in 4.2.0RC1/RC2
* Fixed culling related performance issue in ClusteredVectorLayer
* Guards against null pointer exceptions in ClusteredVectorLayer when interfacing with custom builder
* Better handling of horizontal offsetting in TileRenderer


CARTO Mobile SDK 4.2.0RC2
-------------------

### Changes/fixes:

* Added BalloonPopupButton and related classes so that basic interactivity can be added to BalloonPopups
* Major SGRE optimizations: replaced one-to-one routing engine with many-to-many routing engine, using optimized data structures for routing
* Fixed/improved label ordering in vector tile renderer: prefer bigger labels over smaller ones
* Fixed geometry simplifier attached to LocalVectorDataSource causing a crash
* Fixed multiple issues with billboard sorting and ray casting.
* When calculating actual ray hit with billboard or point, SDK now uses actual bitmap to detect if the clicked pixel is transparent
* Implemented more consistent ordering of vector elements
* Changed billboard rendering to ignore depth testing. Better fit with 3D objects.
* Fixed potential rendering issue with GeometryCollections when switching between planar/spherical rendering mode
* Fixed ray-intersection code with Polygon3D, use the closest intersection point, not the first found point
* Fixed subtle flickering in ClusteredVectorLayer animations
* Minor performance optimization by using platform-optimized zlib
* Fixed getElementClickPos method of PopupClickInfo to return click coordinates as pixel coordinates, not normalized-to-size coordinates
* Fixed issue in SDK4.2.0RC1 that caused map rotation to change when setting focus position in globe view mode
* Fixed GeometryCollectionRenderer to accept both clockwise and counterclockwise oriented polygons
* Documentation fixes


CARTO Mobile SDK 4.2.0RC1
-------------------

This version is a major update and brings lots of new features and optimizations. Some features present in older releases are removed or deprecated in this version.

### Key highlights:

* Globe view support. Maps can be displayed in planar mode (as in previous versions) or in globe view mode.
* EPSG4326 support. WGS84 coordinates can be directly used without needing to convert them to EPSG3857.
* Indoor 3D routing by using GeoJSON input and custom routing profiles. We pulled experimental versions with this into 4.1.x releases, but have since made some changes and stabilized it.
* On-the-fly conversion GeoJSON to vector tiles, so that CartoCSS can be used for styling.
* Faster basemaps with several rendering optimizations.
* Better compatibility with Swift on iOS. SDK does not require bridging header anymore and can be simply 'imported'.
* Faster networking on iOS, by better utilizing OS-provided caching.
* Increased security, all basemap services use HTTPS connection by default.
* Startup time on Android has been significantly reduced. Previously low-end devices required more than a second to load the native SDK component. This loading time is reduced by at least 5 times.
* Basemap style parsing and loading is now faster due to smaller font assets and due to internal optimizations.
* SDK is considerable smaller due to several factors:
  - We have removed offline Valhalla routing support from the SDK. It is still available in the repository and SDK can be built with it.
  - We have removed some font assets from the SDK, so Arabic and few other scripts need external fonts.
  - We use carefully tuned compilation flags that produce smaller native binaries on all platforms.
* All SDK components are now open-source. In previous versions we kept one small component (LicenseManager) private, so custom builds could not connect to online services provided by CARTO. Now this restriction is removed.
* Improvements to build scripts, making compiling the SDK easier and less frustrating experience.


### New features:

* Added EPSG4326 projection. This allows to use longitude/latitude coordinates in the SDK directly, without the need to convert them first.
* New class GeoJSONVectorTileDataSource - provides on-the-fly conversion from GeoJSON layers to vector tiles. This is useful for indoor mapping and allows to use SDKs vector tile renderer with CartoCSS styling.
* New class SGRERoutingService for indoor routing. Additional details can be found in Wiki.
* New class MergedMBVTTileDataSource that merges two MapBox Vector Tile sources into one.
* Added addFallbackFont method to VectorTileDecoder class. This can be used to supply universal fallback font (as binary .TTF asset) for basemaps.
* Added setRenderProjection/getRenderProjection methods to Options class, for switching between planar and globe mode.
* Implemented 3D coordinate support for VectorElements. Previously only billboards handled Z coordinate properly, while using non-zero Z coordinate for polygons or lines produced undefined and usually wrong results.
* Added setZBuffering/isZBuffering methods to VectorLayer. Z buffering may be needed if 3D coordinates are used for lines or polygons.
* Added NMLModelStyle and NMLModelStyleBuilder classes for constructing style instances for NMLModels.
* New HTTP connection class for iOS that works better with device proxy settings and provides better download concurrency.
* Added setSkyColor, getSkyColor to Options class
* Added getMidrange method to MapRange
* Added shrinkToIntersection method to MapBounds
* CartoCSS improvements, 'marker-clip' support, 'north-pole-color', 'south-pole-color' map settings support

### Deprecated features:

* NMLModel constructors with explicit model assets are now deprecated. Use constructors with NMLModelStyle argument instead.


### Removed features:

* Built-in map styles are now smaller and load faster due to fewer built-in fonts. Arabic and few eastern scripts that were displayed in previous versions now require custom font assets. These can be supplied to VectorTileDecoder using addFallbackFont method.
* Removed setSkyBitmap/getSkyBitmap methods from Options class. Sky bitmap usage was poorly documented and relied too much on internal implementation. Use setSkyColor instead of setSkyBitmap.
* simplify method is no longer exposed in GeometrySimplifier class and its subclasses.
* Frustum class is removed from the SDK.
* ViewState class does not expose getCameraPos, getFocusPos, getUpVec, getFrustum methods starting from version 4.2.
* setProjectionMode/getProjectionMode methods are removed ViewState class. Setting projection mode never really worked.
* Removed fromInternalScale method from Projection. This method was never expected to be part of public API and was not useful for applications.
* ValhallaOnlineRoutingService, ValhallaOfflineRoutingService and PackageManagerValhallaRoutingService classes are removed from the public build. SDK used customized version of Valhalla that is not compatible with the latest official Valhalla versions and the library made SDK binaries considerably larger. Valhalla support is still present in the code, it is possible to build a custom version supporting these classes.
* CartoVisBuilder and CartoVisLoader classes are removed from the SDK. These classes provided experimental 'vizjson' support, but were never really complete. 'vizjson' is now deprecated by CARTO.


### Changes:

* All online connections to CARTO services are secure by default. Previously some non-critical services used plaintext connections, causing problems with some newer devices (Android 9) having strict security settings.
* MapView screenToMap now returns NaNs in coordinates if mapping from a given pixel is not possible (tilted map when using sky coordinates, for example)
* EPSG3857 toWgs84 does not return longitude in range -180..180 if the input X coordinate is outside of projection bounds.
* Default panning bounds is now ((-inf, -inf), (inf, inf)) instead of EPSG3857 bounds as in previous versions.
* Sky rendering implementation and default sky color has changed
* Restricted panning mode implementation and behaviour has slightly changed
* All internal fields of wrapped SDK classes on Android are now marked 'transient' and are never serialized. In previous versions trying to serialize/deserialize SDK classes caused native crashes during subsequent GC cycle. The new behviour should result in NPEs and not hard crashes.
* Algorithm for placing text on lines in vector tile renderer is re-implemented and should fix previously distorted placements
* iOS HTTP network stack now uses NSURLSession API for better performance and compatibility. Note that this may cause issues with custom HTTP datasources that do not use secure protocol.
* Much faster handling of [view::zoom] parameter in CartoCSS expressions
* Slightly more compact internal vector tile representation for rendering, gives better tile cache utilization and faster performance


### Fixes:

* setColor, setBitmap, setBitmapScale methods in SolidLayer class properly update the view when called.
* Fixed a memory leak in Java-specific BinaryData constructor taking byte array argument
* Fixed setPreserveEGLContextOnPause not properly invoked in Android MapView class
* Improved compatibility with Android devices with very old GPUs
* Minor search API query language fixes, better support for unicode strings
* Fixed vertex array binding issues with NMLModel rendering
* Fixed minor glyph rendering issues causing glyphs to be slightly blurry under tilted view.
* Minor CartoCSS fixes related to patterned symbolizer support
* Fixed OrderedTileDataSource handling of 'replace with parent' flag


CARTO Mobile SDK 4.1.6
-------------------

This update includes performance and stability improvements,
bug fixes and some minor new features. A new routing engine is introduced 
as an experimental feature.

### New features:

* Added experimental indoor routing support via SGREOfflineRoutingService class.


### Fixes/changes:

* A reworked implementation of HTTP connection worker for iOS that fixes airplane mode switching issues.
* ValhallaOnlineRoutingService now connects to MapBox online service instead of defunct MapZen online service
* Added matchRoute method to ValhallaOnlineRoutingServices
* Added 'wheelchair' routing profile support for Valhalla routing services
* Optimized MBTilesTileDataSource constructor with no explicit minZoom and maxZoom arguments, zoom range is now read first from 'metadata' table. If this fails, full table scan is performed.
* getDataExtent method of MBTilesTileDataSource is now more robust for bad values in 'metadata' table
* GeometryCollectionStyle can now be used when importing FeatureCollection consisting of normal points, lines, polygons to LocalVectorDataSource
* Fixed OrderedTileDataSource getMaxZoom method implementation
* AssetPackage class can now be subclassed from applications
* SDK now handles empty vector tiles as a general case, renders them with background color, not as transparent tiles. 
* Compatibility fix for CartoOnlineVectorTileDataSource by handling 404 code according to server changes (display empty ground tile)
* Added missing header to iOS umbrella header (NTCombinedTileDataSource.h)


CARTO Mobile SDK 4.1.4
-------------------

This update includes performance and stability improvements,
bug fixes and some minor new features.

### New features:

* Exposed TileUtils class with several static methods as part of public API
* SDK now supports custom service URLs as online source ids


### Fixes/changes:

* Fixed Android HTTP connection class to use specified request method (previously always GET)
* Fixed JNI local reference overflows in Android HTTP connection class (with HTTP servers returning very long lists of headers).
* Removed unneeded iOS dependency of libstdc++.6 in Cocoapod, fixes build issues with iOS 12
* Fixed the issue with delayed layer initialization, layers were not automatically rendered
* Fixed several options not correctly reflected in renderer state when changed after the MapView was initialized
* Fixed infinite loop in TileLayer update method when called with inconsistent state (zero view dimensions)
* Fixed value clamping issue with Torque tiles (all floating point numbers were rounded to integers)
* Optimized CartoCSS compiler with 10% reduced map initialization time and faster tile loading time
* Better error reporting of CartoCSS issues
* SDK now uses default background bitmap in case of vector basemap with no background defined
* Bitmap class decoder now supports automatic ungzipping. This is a fix for wrongly configured HTTP servers that send gzipped images even when this is not included in accepted encodings.
* Fixed CartoNamedMapsService ignoring template parameter values when instantiating named maps
* Fixed several grouped marker symbolizers being represented by a single marker
* Fixed threading issue with online license management causing potential API token missing from initial HTTP requests
* Fixed WebP library embedding on iOS targets (Xamarin/native), WebP symbols were previously exported, causing potential linking conflicts
* Made Xamarin.iOS build compatible with 'Linker behaviour = Link All' mode by explictly preserving symbols used through reflection


CARTO Mobile SDK 4.1.3
-------------------

This update includes performance and stability improvements,
bug fixes and some minor new features.

### New features:

* Added support for TomTom online geocoding services (TomTomOnlineGeocodingService and TomTomOnlineReverseGeocodingService)
* Implemented multilanguage support for offline geocoding classes (getLanguage, setLanguage methods in OSMOfflineGeocodingService and PackageManagerGeocodingService classes)
* Implemented localization support for Pelias geocoding results (getLanguage, setLanguage methods in PeliasOnlineGeocodingService)
* Implemented proper location bias for all geocoding services, 'location radius' is no longer needed for bias to work
* Implemented opacity attribute for layers (setOpacity, getOpacity). Note that when used (opacity < 1.0), then this feature may have significant performance impact.
* Implemented background color and border support for Text vectorelements (TextStyleBuilder class)
* Implemented ‘break lines’ flag for texts (TextStyleBuilder class)
* Added online API key interface to CartoMapsService and CartoSQLService
* Added NTExceptionWrapper class for catching/handling SDK exceptions in Swift


### Fixes/changes:

* Min API level on Android is now 10 for Xamarin
* Performance fix for CARTO Maps API - use cacheable requests when instantiating named and anonymous maps
* Fixed regression in SDK 4.1.x vs 4.0.x - packages with incomplete zoom levels had wrong tilemasks after serialized/deserialized in database
* Fixed bounds calculation for NML models
* Fixed zoom level handling in ‘restricted panning’ mode
* Fixed ‘restricted panning’ mode when tilt is applied
* Fixed tile cache invalidation issue when all packages are removed from PackageManager
* BalloonPopupStyleMargins class getters were not wrapped as properties for dotnet platforms previously, fixed now
* Optimized label handling in VT renderer for zoom levels > 14
* Optimized 3D buildings and transparent layers in VT renderer on GPUs that use tiled rendering
* Distance based filtering in search API is more robust now (for coordinate wrapping, etc)
* Fixed WKTGeometryWriter to NOT use scientific encoding


CARTO Mobile SDK 4.1.2
-------------------

This is a maintenance release for SDK 4.1.x containing mostly fixes
but also some new features. This version deprecates support
for external MapZen services due to the services being closed.

### New features:

* SDK has support for MapBox online geocoding services.
  New classes MapBoxOnlineGeocodingService and MapBoxOnlineReverseGeocodingService can be used for this.
* All MapZen online service (Pelias and Valhalla) wrappers now include additional methods for specifying custom service URLs.
  This feature was added as MapZen closes all online services as of February 2018.
* Added optional ‘restricted panning’ support to avoid zooming/panning outside world map area. If turned on, then  map area is restricted to maximize visible map. This can be turned on/off using Options.setRestrictedPanning method
* Added custom service URL support for Pelias and Mapbox geocoders and Valhalla routing
* API documentation for iOS is using Jazzy tool, instead of Doxygen. This allows us to show both ObjectiveC and Swift syntax for the API.


### Fixes/changes:

* Implemented fine-grained clipping in VT loader - reduces drawing of invisible geometry and improves performance 
* Removed MapZen-specific handling from CartoOnlineTileDataSource
* Smaller built-in style asset due to optimized fonts
* Proper handling of line-placement of markers and texts with polygon geometry
* Fixed C#-specific API wrapping issue: Polygon3DStyleBuilder and Polygon3DStyle SideColor property was not properly wrapped
* SDK includes latest version of CARTO styles, with minor fixes
* Improved text placement along lines in vector tile renderer
* Fixed text wrapping in vector tile renderer when ‘wrap-before: true’ mode was used
* MapZen-specific code is removed from CartoOnlineVectorLayer
* Minor optimizations in vector tile renderer for faster rendering of transparent features


CARTO Mobile SDK 4.1.1
-------------------

This is a maintenance release for SDK 4.1.x containing mostly fixes
but also some new features.

### New features:

* Implemented route matching support in ValhallaOfflineRoutingService and PackageManagerValhallaRoutingService classes
* Included NMLModelLODTree in the build (missing from all previous 4.x builds)
* Added postcode to geocoding responses
* Implemented building-min-height parameter for CartoCSS
* Improved support for offline Valhalla routing with multimodal profile


### Fixes/changes:

* Improved text placement in vector tile renderer with texts that have non-zero vertical offsets
* Improved tilting gesture handling on UWP
* Performance optimizations for MB vector tile decoder
* Pelias Online geocoding fixes
* Text rendering quality improvements
* Improvement of Mapnik XML styling reader
* Fixed building height issue with built-in basemaps when 3d buildings are enabled
* Fixed vector tile layer elements missing at zoom level 24
* Fixed http:// and https:// handling when accessing CartoCSS external resources
* Fixed subtle background rendering issues on iOS (PowerVR) due to insufficient precision in fragment shaders
* Fixed UWP specific issue - do not try to create EGL context when panel size is 0
* Fixed custom HTTP headers being ignored when using HTTPTileDataSource
* Fixed basemap 3D building height calculation
* Fixed z-fighting/flickering issue with overlapping basemap 3D buildings
* Fixed minor rendering issues with NMLModelLODTreeLayer
* Fixed a small memory leak with vector layers containing NMLModels
* Documentation fixes


CARTO Mobile SDK 4.1.0
-------------------

This is a major release containing many new features, fixes and performance
optimizations.

### Key highlights:

* SDK now supports **geocoding** and **reverse geocoding**. For offline geocoding, custom geocoding packages can be used through PackageManager. We have provided country-based packages (bigger countries like US, Germany have split packages) but custom packages based on bounding box can be also used. For online geocoding, SDK includes wrapper class for MapZen Pelias geocoder; your MapZen API key is required for that.
* SDK has optional support for **MapZen Valhalla routing**. This feature requires a special SDK build as the routing engine is fairly complex and makes compiled SDK binaries approximately 30% larger. Compared to the custom built-in routing Valhalla routing packages are univeral -  single package can be used for car, bicycle or walking profiles. We have prepared country-based packages that can be downloaded  using PackageManager. Also, custom packages based on bounding box are supported. For online Valhalla routing, SDK includes wrapper class that uses MapZen Mobility API.
* New **built-in styles** and vector tile structure. This change is backward-incompatible due to two reasons: the old styles are removed from the SDK and new styles require different tile and offline package sources. New styles are better optimized for lower-end devices and have more consistent information density on all zoom levels. Also, new styles are based on view-dependent zoom parameters instead of tile-based zoom parameters, which gives much more pleasant zooming experience and cleaner visuals at fractional zoom levels.
* SDK supports **offline searching** features from various sources (VectorTileDataSource, FeatureCollection, VectorDataSource) via unified search API. The search API supports search requests based on geometry and distance, metadata and custom SQL-like query language.
* The VectorElements appearing on the map can now have **transitioning animations**. This is currently supported for billboards only (markers, texts, popups). Different animations styles are supported and the effects can be customized.
* SDK 4.1 has major **speed and memory usage improvements** when using ClusteredVectorLayer class. Performance can be up to 10x better compared to SDK 4.0.x and memory usage 2x lower.
* Lots of lower level performance and memory usage optimizations, mostly related to vector tiles.

### API changes:

* The new built-in styles (Voyager, Positron, Darkmatter) use different data schema and are not compatible with *nutiteq.osm* source. Instead, "**carto.streets**"  source must be used. This applies to both online tiles and offline map packages. The old styles (Dark, Grey, Nutibright) and data source continue to work for now, but are no longer included in the SDK and must be downloaded/applied separately. Offline map packages are not updated for nutiteq.osm source.
* The old nutibright, dark and grey styles are no longer included in the SDK and as a result the following CartoBaseMapStyles are removed:  CARTO_BASEMAP_STYLE_DEFAULT, CARTO_BASEMAP_STYLE_GREY, CARTO_BASEMAP_STYLE_DARK. Instead, new styles CARTO_BASEMAP_STYLE_VOYAGER,  CARTO_BASEMAP_STYLE_POSITRON, CARTO_BASEMAP_STYLE_DARKMATTER should be used.
* Public constructors from various vector element Style classes are now hidden, these classes can now be instanced only through corresponding StyleBuilders.
* Removed unsafe clone method from StyleBuilder.
* Removed public constructors for internal 'UI info' classes.
* Removed public constructors for Frustum class
* New CartoStyles package with following changes:
  1) default language is now "en" (before "local")
  2) 'buildings3d' style parameter is no longer used, instead 'buildings' style parameter can be used to control rendering of buildings (0=no buildings, 1=2D buildings, 2=3D buildings)
* Tilemasks used by the offline packages have stricter semantics now and PACKAGE_TILE_STATUS_PARTIAL tile status  is now deprecated (never used by the SDK) and will be removed in the later versions.


### Detailed list of new features:

* New 'geocoding' module that includes following generic classes/interfaces: GeocodingRequest, GeocodingResult, GeocodingService, ReverseGeocodingRequest, ReverseGeocodingService. The module also includes several classes for offline geocoding/reverse geocoding: OSMOfflineGeocodingService, OSMOfflineReverseGeocodingService, PackageManagerGeocodingService, PackageManagerReverseGeocodingService.  For online geocoding the module includes PeliasGeocodingService and PeliasReverseGeocodingService classes.
* The routing module includes three new classes for Valhalla routing: PackageManagerValhallaRoutingService, ValhallaOfflineRoutingService, ValhallaOnlineRoutingService.  These classes are only included in Valhalla-supporting builds.
* New 'search' module for searching features from various sources. The module includes following classes: SearchRequest, FeatureCollectionSearchService, VectorElementSearchService and VectorTileSearchService.  These classes can be used to search features from loaded geojson collections, vector data sources and vector tile data sources.
* Billboards now support fade-in/fade-out animations. AnimationStyle objects can be now attached to billboard   StyleBuilder objects and the specified animations will be used when billboard appear/disappear.
* PackageManager now includes two additional methods: isAreaDownloaded and suggestPackage. These methods can be used to detect is the view area is downloaded for offline use and if not, to get the best package for the area.
* SDK now support optional zoom gestures. Options class includes setZoomGestures/isZoomGestures methods,  when zoom gestures are turned on, SDK automatically interprets double tap as a zoom-in action and two finger tap as a zoom-out action. By default, zoom gestures are not enabled.
* Implemeted RasterTileClickEventListener class for receiving click events on raster tile layers. SDK provides  click coordinates and the raster tile color at the click point.
* Implemented simulateClick method for Layer class. This method can be used to programatically call event handlers of the layer.
* Implemented automatic background/sky color calculation for VectorTileLayers. If background/sky image is not explicitly defined using Options, then appropriate background/sky image is generated by the SDK.  This provides much better experience with dark styles compared vs SDK 4.0.x.
* Implemented setClearColor/getClearColor for Options class to specify background color of the MapView. This can be used to enable partially transparent map views.
* CartoOnlineDataSource has now support for 'water masks' and coarse water tiles are automatically detected and no longer requested from the server, thus reducing latency and providing better user experience.
* Added getDataExtent method TileDataSource class.  SDK uses the datasource extent information when generating tiles and this results in much lower memory usage in some cases (local raster overlays, for example).
* Added getDataExtent method VectorDataSource class.
* Exposed screenToMap and mapToScreen methods of MapRenderer with explicit ViewState argument.
* Added new helper classes VariantArrayBuilder and VariantObjectBuilder for building Variant instances.
* Added containsObjectKey method to Variant
* The performance of the clustering (ClusteredVectorLayer) is improved up to 10x. Also, the memory usage  of the clustering is now 2x lower. Due to the improvements, clusters of 100k points should works well  even on lowend devices.
* Optimized memory usage of LocalVectorDataSource setAll/addAll methods.
* ClusteredVectorLayer now monitors which attributes of elements change and avoids unnecessary costly reclustering.
* Added option to disable clustering animations via setAnimatedClusters method
* Added new option for faster clustering: ClusterElementBuilder includes additional buildClusterElement method (with cluster position and 'count' arguments). ClusterElementBuilder can specify ClusterBuilderMode which determines which of the two buildClusterElement method gets called.
* Lower level vector tile text rendering uses now SDF (Signed Distance Field) glyph representation which gives
  crisper texts especially on high-DPI devices. Also, memory usage of glyph atlas textures is reduced.   Additionally, the rendering artifacts of vector tile texts with large halos and overlapping glyphs are now fixed.
* Better support for shared dictionaries for offline packages to reduce package sizes.
* Added addFeatureCollection method to LocalVectorDataSource
* CartoVectorTileLayer includes static createTileDecoder method that can used to instantiate VectorTileDecoder from built-in styles.
* Added isOpen method to PersistentCacheTileDataSource.
* PersistentCacheTileDataSource now support asynchronous tile download/cache prefill (startDownloadArea method). An optional listener can be used to monitor tile download progress.
* Implemented setVectorTileBufferSize method for CartoMapsService. This method can be used to tweak tile sizes/fix rendering artifacts  when using vector tiles from CARTO Maps API.
* Reduced memory consumption when large vector tiles are used
* iOS: added support for converting 16 bits-per-component UIImages
* UWP: Added mouse wheel support for zooming.
* Optimizations for GeoJSONGeometryReader, loading large geojson files is now approximately 2x faster
* Implemented ClickSize property for MarkerStyle, this allow enlarging of the click area when very small markers are used.
* Faster loading of complex vector tiles, SDK now optimizes CartoCSS styling rules.
* Optimized memory usage of complex Polygon vector elements (up to 25% in complex cases).
* New classes VectorTileFeature and VectorTileFeatureCollection that are used by the new search API
* CartoCSS: Implemented 'pow' operator
* CartoCSS: Added support for metavariables
* Implemented more optimizations in CartoCSS for various degenerate rendering rules: empty text expressions, zero size features, etc
* Added SideColor property to Polygon3DStyle/builder classes. Previously single Color was always used for all faces of the 3D polygon.
* Added toString method to BinaryData
* CartoCSS feature: comp-op support for markers
* CartoCSS: text-size attribute is now evaluated per-frame, allowing to use smooth text size interpolation based on zoom level
* CartoCSS: enabled PointSymbolizer support
* CartoCSS: parser now supports meta-variables


### Fixes:

* Fixed equals/hash implementation for several built-in classes. Previously both methods provided unreliable results.
* Tile layer preloading tweaks - avoid cache trashing and constant refreshing in rare cases, reduce preloading dataset size
* SDK does not show harmless 'failed to decode tile' warning for empty tiles anymore
* Fixed subtle case of duplicate Layer instance handling in Layers container
* SDK allows vector element to be attached to only a single data source, violating this results in an exception now
* Fixed Windows Phone/UWP related pointer handling, previous version assumed MapView control to be at (0, 0) coordinates in the window
* Fixed touch handling issues on Windows Phone when more than 2 fingers are used
* Fixed regression in SDK 4.0.2 vs 4.0.0 when rendering vector tile lines with null width
* CartoCSS: fixed handling of shield-text-opacity and shield-text-transform
* Fixed multigeometry bounds calculations
* Fixed alpha channel handling when translating color interpolation expressions from CartoCSS to rendering library


CARTO Mobile SDK 4.0.2
-------------------

Maintenance release for CARTO Mobile SDK 4.0.x

### Fixes/Changes:

* Enabled stack protector for Android builds for better app security
* Implemented null pointer checks throwing exceptions for various Layers methods, previously such cases could result in native level crashes
* Implemented workaround for Xamarin/Android multithreading issues - native threads were sometimes not automatically registered when managed delegates are called from multiple threads
* Fixed issues with online licenses when license server was unreliable and took long time to respond
* Fixed app token issues with CARTO named map services
* Fixed SDK log filters being ignored/not working
* Fixed CartoCSS marker-transform handling for non-overlapping points
* Fixed VectorTileLayer click detection when custom transform was applied
* Fixed layer background not being properly set when VectorTileDecoder was updated


CARTO Mobile SDK 4.0.1
-------------------

This is a maintenance release for 4.0.x that includes several important reliability and performance fixes, in addition to
some minor new features.

### New features and changes:

* Added Layer visibility control API to CartoVectorTileDecoder (setLayerVisible, isLayerVisible methods)
* Implemented 'screen' and ‘clear’ comp-op support for CartoCSS/vector tile rendering
* Rendering of vector tile layers with multiple line/polygon symbolisers is now optimized as a special case, this is usually done with a single draw call
* Changed moveToFitBounds behaviour - from now SDK does not change zoom level if single point is used for MapBounds
* Better error reporting for CARTO SQL API, including error logging and error parsing
* Minor optimizations in vector tile renderer
* implemented timeout for online license update procedure
* forward-compatible changes for future features in online tile service and offline packages
* Exposed CartoVectorTileDecoder constructor for better integration with CARTO vector overlays
* Added additional CartoOnlineVectorTile constructor with explicit source and built-in style enumeration parameters
* Added countVisibleFeatures method to TorqueTileLayer
* Added comp-op support to points, markers, texts and shields
* Increased internal visible tile cache size by 4x, for really large overlay datasets (does not affect memory usage in normal cases)
* MBTilesDataSource and OfflineNMLModelLODTreeDataSource classes now open database in read-only mode (previously in read-write mode)
* More precise label coverage analysis for transformed labels

### Fixes:

* Fixed Torque tile usage  in MapsService API due to malformed URL
* Fixed deadlock with indirect texts fields in Text and BalloonPopup objects
* Fixed feature batching related issue in vector tile renderer that caused high number of draw calls and low performance
* Fixed 'multiply' comp-op handling with non-opaque alpha values
* Fixed parameter name typo in CartoCSS (instead of 'polygon-pattern-comp-op', 'polygon-pattern-op' was used)
* Fixed performance issue on iOS with empty Text objects
* CartoCSS compatibility fixes for handling negative line widths and marker sizes
* Minor memory usage, speed optimizations
* Added missing NTCartoVectorTileDecoder to iOS umbrella header
* Fixed CartoVectorTileDecoder layer ordering issues
* Fixed regression regarding VisJSON vector sublayer grouping; visibility and attribute info was previously lost
* Fixed handling of zero size ellipse markers in CartoCSS
* Fixed vector tile click detection issues  
* Fixed rare cases on iOS when screen remained black after returning from background state
* Heavily distorted texts are no longer displayed on the map
* Fixed bad_weak_ptr exception when using PersistentCacheTileDataSource
* Fixed crash with some Xamarin Android versions when MapView finalizer is called
* Fixed license registration issues on Windows Phone targets
* Fixed vector tile layers in layergroup ignoring 'visibility' attribute
* Fixed billboard sorting issues causing flickering with overlapping markers/texts/popups
* Implemented clamping for CartoCSS opacity values for better compatibility


CARTO Mobile SDK 4.0.0
-------------------

CARTO Mobile SDK is built on top of [*Nutiteq Maps SDK 3.3*](http://developer.nutiteq.com), and includes over 100 API related improvements, performance updates and fixes. The new API is not compatible with Nutiteq SDK 3.3, but most apps can be converted relatively quickly and most changes are only related to class/module naming. See [Upgrading from Nutiteq SDK](https://github.com/CartoDB/mobile-sdk/wiki/Upgrading-from-Nutiteq) for more details.

Release notes for next releases can be found from [Releases section](https://github.com/CartoDB/mobile-sdk/releases).

### New features and improvements:

* New 'services' module that gives integration with CARTO online services (Maps services, SQL API, high level VisJSON map configuration)
* JSON serializing/deserializing support and JSON based vector element metadata
* Revamped tile layer support, with more shared features between all tile layers including generic UTF grid support for vector/raster tile layers and many other tweaking options
* Vector editing is now available in all builds (Nutiteq SDK included this only in special GIS builds)
* Improved GeoJSON support, supporting GeoJSON features and feature collections
* Improved and more compliant CartoCSS support for vector tiles with 2 times faster CartoCSS parsing/compiling speed
* Additional styling options for vector overlays (lines, 3D polygons)
* Event handling by layer specific listeners
* Full Collada standard material support in NML models
* Usage of exceptions to signal about most common error cases, for example, file access errors, null pointers, out of range indexing
* Faster vector basemap rendering with better text quality
* Faster and higher quality vector overlay rendering (especially lines)
* Click detection and feature introspection for vector tiles

### Removed features:

* Windows Phone 8.1 is no longer supported, as the platform is generally deprecated, only Windows Phone 10 is now supported
* Basic CartoCSS styling support is removed from styles module, full CartoCSS is available for vector tiles
[v5.0.0-rc.4]: https://github.com/Akylas/mobile-sdk/compare/v5.0.0-rc.2...v5.0.0-rc.4
[v5.0.0-rc.6]: https://github.com/Akylas/mobile-sdk/compare/v5.0.0-rc.5...v5.0.0-rc.6
[v5.0.0-rc.7]: https://github.com/Akylas/mobile-sdk/compare/v5.0.0-rc.6...v5.0.0-rc.7
[v5.0.0-rc.8]: https://github.com/Akylas/mobile-sdk/compare/v5.0.0-rc.7...v5.0.0-rc.8
[vv5.0.0-rc.9]: https://github.com/Akylas/mobile-sdk/compare/v5.0.0-rc.8...vv5.0.0-rc.9
[v5.0.0-rc.10]: https://github.com/Akylas/mobile-sdk/compare/vv5.0.0-rc.9...v5.0.0-rc.10
[v5.0.0-rc.11]: https://github.com/Akylas/mobile-sdk/compare/v5.0.0-rc.10...v5.0.0-rc.11
[v5.0.0-rc.12]: https://github.com/Akylas/mobile-sdk/compare/v5.0.0-rc.11...v5.0.0-rc.12
[v5.0.0-rc.13]: https://github.com/Akylas/mobile-sdk/compare/v5.0.0-rc.12...v5.0.0-rc.13
[v6.0.0]: https://github.com/massif-maps/MassifMaps/compare/v5.2.3...v6.0.0
[v6.0.1]: https://github.com/massif-maps/MassifMaps/compare/v6.0.0...v6.0.1
[v6.1.0]: https://github.com/massif-maps/MassifMaps/compare/v6.0.2...v6.1.0
[v6.1.1]: https://github.com/massif-maps/MassifMaps/compare/v6.1.0...v6.1.1
[v6.1.2]: https://github.com/massif-maps/MassifMaps/compare/v6.1.1...v6.1.2
