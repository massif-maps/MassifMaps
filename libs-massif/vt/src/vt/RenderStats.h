/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_RENDERSTATS_H_
#define _MASSIF_VT_RENDERSTATS_H_

/**
 * Diagnostic counters for label and tile churn, and the single switch that turns them on.
 *
 * They live in hot paths - every label placement update, every label built, every culled
 * label - so they must cost nothing when unused: with MASSIF_VT_RENDER_STATS at 0 the
 * counters do not exist, the VT_STAT_* macros expand to nothing, their arguments are never
 * evaluated, and the SDK's per-second printout in MapRenderer is not compiled either.
 *
 * Set it to 1 here, or define it as a build flag, to get the printout back.
 */
#ifndef MASSIF_VT_RENDER_STATS
#define MASSIF_VT_RENDER_STATS 0
#endif

#if MASSIF_VT_RENDER_STATS

#include <atomic>
#include <chrono>

namespace massif::vt {
    struct RenderStats {
        // Tile churn
        static inline std::atomic<long long> cullWorkerUpdates{0};     // CullWorker passes that pushed a cull state to the layers
        static inline std::atomic<long long> tileRecalculations{0};    // TileLayer::update passes that recalculated the visible tile list
        static inline std::atomic<long long> tileLayersSkipped{0};     // TileLayer::update passes that bailed out (hidden / out of zoom range / transparent)
        static inline std::atomic<long long> visibleTileSetChanges{0}; // setVisibleTiles calls that reached buildLabelMaps
        static inline std::atomic<long long> tileSurfacesBuilt{0};     // CPU tile surface tesselations (cache misses)
        static inline std::atomic<long long> tileSurfacesInvalidated{0}; // cached surfaces dropped by elevation changes

        // Label churn
        static inline std::atomic<long long> labelMapRebuilds{0};      // buildLabelMaps calls (only the label-carrying layers reach it)
        static inline std::atomic<long long> labelMapSkips{0};         // ... and the ones an unchanged label tile set skipped
        static inline std::atomic<long long> labelsAllocated{0};       // new vt::Label objects built in buildLabelMaps
        static inline std::atomic<long long> labelsReused{0};          // labels kept because every contributing tile geometry was unchanged
        static inline std::atomic<long long> labelsLive{0};            // labels alive after the last buildLabelMaps (gauge, not a delta)
        static inline std::atomic<long long> labelElevationReanchors{0}; // updateElevation calls that actually moved a label

        // buildLabelMaps, by phase and by why a label could not be reused. Declared here for the
        // SDK's printout; the emit sites inside buildLabelMaps are still to be written, so these
        // read zero until they are.
        static inline std::atomic<long long> labelSignatureNs{0};    // pass 1, the geometry signatures
        static inline std::atomic<long long> labelMergeNs{0};        // building the list and merging geometries
        static inline std::atomic<long long> labelStampNs{0};        // stamping signatures on the fresh labels
        static inline std::atomic<long long> labelReleaseNs{0};      // releasing the old labels
        static inline std::atomic<long long> labelCarryNs{0};        // carrying placements over to the new labels
        static inline std::atomic<long long> labelListNs{0};         // flattening into the visible label list
        static inline std::atomic<long long> labelConstructNs{0};    // time in the Label constructor alone
        static inline std::atomic<long long> labelMergeIterations{0}; // trip count of the merge loop
        static inline std::atomic<long long> labelMissNew{0};        // no previous label with that global id
        static inline std::atomic<long long> labelMissCount{0};      // a different number of contributions
        static inline std::atomic<long long> labelMissHash{0};       // same count, different geometry
        // A label fed by more than one tile - an unclipped label is one, and its signature covers
        // every contributing tile, so any of them changing costs the reuse.
        static inline std::atomic<long long> labelMissSpanning{0};
        static inline std::atomic<long long> labelHitSpanning{0};
        // Old labels dropped by the release pass, split by whether they were ever shown.
        static inline std::atomic<long long> labelRetiredSeen{0};
        static inline std::atomic<long long> labelRetiredUnseen{0};

        // Placement churn. Split by what the label was before the re-anchor: only the
        // 'visible' ones can be seen moving, the rest is wasted work on labels the user
        // can not see.
        static inline std::atomic<long long> placementUpdates{0};
        static inline std::atomic<long long> placementReanchorsNull{0};    // had no placement (off-screen / unplaceable)
        static inline std::atomic<long long> placementReanchorsHidden{0};  // had a placement, was not visible
        static inline std::atomic<long long> placementReanchorsVisible{0}; // had a placement AND was visible
        static inline std::atomic<long long> placementSearches{0};         // re-anchors that ran a clipped search rather than being rejected on bounds

        // Re-snapping on tile-set change (buildLabelMaps -> snapPlacement). 'moved' is the
        // one that matters: a re-snap that lands the anchor somewhere else is a label
        // sliding along its line with the camera standing still.
        static inline std::atomic<long long> snapPlacements{0};
        static inline std::atomic<long long> snapPlacementsMoved{0};

        // Draw calls. The per-frame cost of an ordinary style tracks the DRAW COUNT, not the
        // triangle count: measured on an Adreno 610, a 6-layer style and a 21-layer style
        // submit the same ~300k indices, but 59 draws against 500, and cost 3 ms against 20.
        static inline std::atomic<long long> geometryDraws{0};
        static inline std::atomic<long long> geometryIndices{0};
        static inline std::atomic<long long> labelDraws{0};
        static inline std::atomic<long long> renderTilesDrawn{0};
        static inline std::atomic<long long> styleLayersDrawn{0};
        static inline std::atomic<long long> surfaceDraws{0};     // terrain tile surface draws (depth pre-pass, drape, fill, main) - NOT in geometryDraws
        static inline std::atomic<long long> surfaceIndices{0};
        // TerrainRenderer's OWN meshes (not the vt tile surfaces above): how many were built, how
        // long that took and how many vertices it produced. Built inline on the render thread in
        // collectTileMeshes, so a build is a frame that does not draw until it finishes - which is
        // what a panorama's rotation looks like when its mesh cache cannot hold a full turn.
        static inline std::atomic<long long> terrainMeshBuilds{0};
        static inline std::atomic<long long> terrainMeshBuildUs{0};
        static inline std::atomic<long long> terrainMeshVerts{0};
        static inline std::atomic<long long> terrainMeshEvictions{0};
        static inline std::atomic<long long> terrainMeshCacheHits{0};
        // The SURFACE ATTRIBUTES bake, which is the other half of a mesh rebuild and was invisible:
        // ensureSurfaceAttribs re-derives a normal per vertex, and on the fixed-scale path
        // (TerrainOptions::NormalSampleDistance) it reads the DEM four times per vertex to do it.
        // A 64-cell mesh is 4225 vertices, so one rebuilt tile is ~17000 cached elevation lookups
        // and a frame that rebuilds seventy of them is over a million.
        static inline std::atomic<long long> terrainAttribBakes{0};
        static inline std::atomic<long long> terrainAttribUs{0};
        // ...and the DEM-sampled REFINEMENT, which runs on TerrainRenderer's attribute worker. Read
        // against the wall clock, not against the frame: this one is supposed to be off the render
        // thread, so it costing more than the inline bake is the point rather than a problem.
        static inline std::atomic<long long> terrainAttribRefines{0};
        static inline std::atomic<long long> terrainAttribRefineUs{0};
        // How many vertices the fixed-scale path actually RESOLVED, against how many it tried. The
        // DEM is read CACHED_ONLY, so a vertex whose ±sampleDistance neighbours are not in the grid
        // cache silently keeps the MESH gradient - which is measured over a step that halves every
        // zoom level, so two tiles at different LOD then disagree about their normals along the whole
        // shared edge. That is the "some tiles are shaded differently" symptom, and this is the only
        // way to tell it from a refine that never ran.
        static inline std::atomic<long long> terrainAttribFixedVerts{0};
        static inline std::atomic<long long> terrainAttribTotalVerts{0};
        // Vertices whose baked normal is the ZERO vector. The normal-packing shader turns those into
        // BA = (0.5, 0.5), which the post-process decodes as a straight-up normal - so they draw with
        // no slope shading and no ridge ink while the surface shader, which reads the same attribute
        // without a round trip, still looks correct. Split by grid vs SKIRT, because the skirt run is
        // filled by a separate loop that stops early if skirtSources is short.
        // COUNTED AT THE UPLOAD, not inside the bake. The first version of this counter ran halfway
        // through ensureSurfaceAttribs, BEFORE the skirt fill loop, so it reported every skirt
        // vertex of every mesh as a zero normal and sent a whole round of debugging after skirts.
        // A stat has to measure what leaves the function, not what the middle of it looks like.
        static inline std::atomic<long long> terrainAttribZeroGrid{0};
        static inline std::atomic<long long> terrainAttribZeroSkirt{0};
        // Skirt vertices whose source index does not fit the attribs: the second way the fill loop
        // can leave a zero, and the one the first diagnostic missed.
        static inline std::atomic<long long> terrainAttribSkirtOutOfRange{0};
        // Mesh rebuilds that found (hit) or did not find (miss) a refined sibling to carry the
        // fixed-scale normals from. A miss draws the mesh-gradient stand-in until the worker lands.
        static inline std::atomic<long long> terrainAttribCarryHit{0};
        // Misses by cause: no cached variant of this tile at all, one that exists but has not been
        // refined yet, and one that was refined at a DIFFERENT grid size (a carry declined here).
        static inline std::atomic<long long> terrainAttribCarryMissNew{0};
        static inline std::atomic<long long> terrainAttribCarryMissUnrefined{0};
        static inline std::atomic<long long> terrainAttribCarryMissGrid{0};
        // How much COARSER than normalSampleDistance a tile's normals actually resolved, bucketed by
        // the stretch of the DEM grid it is standing on. A tile on an ancestor draws less ridge ink
        // than its neighbour that resolved its own grid - the visible "lighter tile".
        static inline std::atomic<long long> terrainAttribStretch1{0};
        static inline std::atomic<long long> terrainAttribStretch2{0};
        static inline std::atomic<long long> terrainAttribStretch4{0};
        static inline std::atomic<long long> terrainAttribStretchBig{0};
        // Vertices that came back EXACTLY FLAT at normalSampleDistance and had to retry at the DEM
        // texel. The per-tile clamp this replaces spent that coarser step on every vertex of every
        // tile whose grid was coarse; this counts how often it is genuinely needed.
        static inline std::atomic<long long> terrainAttribTexelRetry{0};
        // Bakes that had to read a DEM coarser than the source could give (provisional) against those
        // that got the data they asked for (final), and how many were redone once more data landed.
        // provisional falling to zero while rebakes stop is what "it has converged" looks like.
        static inline std::atomic<long long> terrainAttribProvisional{0};
        static inline std::atomic<long long> terrainAttribFinal{0};
        static inline std::atomic<long long> terrainAttribRebakes{0};
        // Tiles whose pass found a GPU elevation texture to measure its normal from, against those
        // that fell back to the interpolated mesh normal. A fallback is not an error - the texture
        // is encoded on a worker and lands a frame or two late - but a miss RATE that stays high
        // means the per-fragment path is not the one drawing the picture.
        static inline std::atomic<long long> terrainDemTextureHits{0};
        static inline std::atomic<long long> terrainDemTextureMisses{0};
        // Tiles whose stored normals were baked from a COARSER DEM than they now resolve, by how
        // many zoom levels. Nothing else per-tile distinguishes them - the flags all read correct.
        static inline std::atomic<long long> terrainAttribStaleFresh{0};
        static inline std::atomic<long long> terrainAttribStale1{0};
        static inline std::atomic<long long> terrainAttribStale2{0};
        static inline std::atomic<long long> terrainAttribStale3{0};
        // The cut's MESH DENSITY, bucketed by gridSize: <=1, <=4, <=16, <=48, and the rest. A tile
        // that resolved its own elevation tile gets the full meshResolution; one standing on a
        // cached ANCESTOR covering four or sixteen times the ground gets a quarter or a sixteenth of
        // it (calculateMeshGridSize divides the DEM's texels by the stretch), and one with no grid
        // or a flat one gets a single quad. Neighbours at the SAME zoom can land in different
        // buckets, and then they carry visibly different relief - which no LOD reasoning explains
        // and which is the "some tiles are shaded differently" report.
        static inline std::atomic<long long> terrainMeshGrid1{0};
        static inline std::atomic<long long> terrainMeshGrid4{0};
        static inline std::atomic<long long> terrainMeshGrid16{0};
        static inline std::atomic<long long> terrainMeshGrid48{0};
        static inline std::atomic<long long> terrainMeshGridFull{0};
        // ... split by the pass that issued it, to see how many times a frame the same
        // terrain mesh is pushed through the vertex stage.
        static inline std::atomic<long long> surfShadowDraws{0};
        static inline std::atomic<long long> surfMaskDraws{0};
        static inline std::atomic<long long> surfFillDraws{0};
        static inline std::atomic<long long> surfBlitDraws{0};
        static inline std::atomic<long long> surfDrapeDraws{0};
        static inline std::atomic<long long> surfBackgroundDraws{0};
        static inline std::atomic<long long> surfBitmapDraws{0};
        static inline std::atomic<long long> surfMaskNs{0};   // depth pre-pass over the tile surfaces
        static inline std::atomic<long long> surfDrapeNs{0};  // drape composite over the tile surfaces
        // startFrame, split by loop. All four walk every live label or render tile.
        static inline std::atomic<long long> prepTileBlendNs{0};
        static inline std::atomic<long long> prepElevDirtyNs{0};
        static inline std::atomic<long long> prepElevUpdateNs{0};
        static inline std::atomic<long long> prepLabelBlendNs{0};
        // Label::calculateVertexData, split by what it spends the time on.
        static inline std::atomic<long long> labelPlacementNs{0};
        static inline std::atomic<long long> labelLineBuildNs{0};
        // Line-label runs laid out from scratch. The layout is keyed on the view-projection, so this is
        // one per visible line label per frame while the camera moves, plus one per culler pass - the
        // price of a run that follows the line as the CURRENT camera projects it.
        static inline std::atomic<long long> lineLayoutBuilds{0};
        static inline std::atomic<long long> labelTransformNs{0}; // world transform of the glyph quads (what a GPU billboard would remove)
        static inline std::atomic<long long> labelAttribNs{0};    // normals / uvs / attribs / indices plumbing into the batch arrays
        // The frame profiler's 'sky' section, which is 10-25 ms a frame and holds three things. The
        // clear is the frame's FIRST GL call, so a driver with no free buffer blocks the CPU there:
        // time in frameClearNs is the frame waiting to be presented, not work anyone can remove.
        static inline std::atomic<long long> frameClearNs{0};
        static inline std::atomic<long long> skyDrawNs{0};
        static inline std::atomic<long long> backgroundDrawNs{0};
        // The base pass, which is the frame profiler's 'layers' section - 74-151 ms a frame in the
        // slow 3D intervals, with almost no labels drawn in them. Split at the calls, in order.
        static inline std::atomic<long long> pass2DStateNs{0};     // view state, terrain versions and the drape/grid settings pushed into the renderer
        static inline std::atomic<long long> pass2DLightNs{0};     // lighting, fog and the label occlusion test resolved from the style
        static inline std::atomic<long long> pass2DPrepareNs{0};   // prepareFrameUnsafe, when MapRenderer's prepare phase did not already run it
        static inline std::atomic<long long> pass2DGeometryNs{0};
        static inline std::atomic<long long> pass2DLabels2DNs{0};
        static inline std::atomic<long long> pass2DExtrusionNs{0}; // buildingOrder 0, i.e. extrusions in the base pass
        static inline std::atomic<long long> pass2DLabels3DNs{0};
        // The three calls the 3D pass makes, in order.
        static inline std::atomic<long long> pass3DLabels2DNs{0};
        static inline std::atomic<long long> pass3DGeometryNs{0};
        static inline std::atomic<long long> pass3DLabels3DNs{0};
        // Label pass: the glyph quads are rebuilt from scratch for every visible label
        // every frame, then uploaded as one batch.
        // renderLabelPass, split: the pass costs 54-81 ms an interval while the per-label vertex build
        // and the batch upload together account for 21-27, and the rest was never measured.
        static inline std::atomic<long long> labelPassSortNs{0};    // the per-pass grouped copy and its sort
        static inline std::atomic<long long> labelPassPatternNs{0}; // getBitmapPattern, PER LABEL, on the glyph map's mutex
        static inline std::atomic<long long> labelPassStyleNs{0};   // per-style colour/width evaluation and its table scans
        static inline std::atomic<long long> labelVertexBuildNs{0};
        static inline std::atomic<long long> labelBatchNs{0};
        static inline std::atomic<long long> labelsDrawnVertices{0};
        // endFrame sweeps every compiled-resource map looking for expired owners, once per
        // frame, over everything the tile cache still holds.
        static inline std::atomic<long long> endFrameNs{0};
        static inline std::atomic<long long> endFrameSwept{0}; // entries visited by those sweeps
        // GL thread blocked on the renderer mutex - the label placement worker holds it for
        // the whole of buildLabelMaps.
        static inline std::atomic<long long> mutexWaitNs{0};
        // The tile-set change path, which runs inside the layer draw pass. The first two are the
        // SDK's TileRenderer::refreshTiles, the rest are the phases of setVisibleTiles it calls -
        // so refreshTilesNs contains all of them.
        // How long the cull thread holds the LAYER's mutex across refreshDrawData. Anything on the
        // render thread that wants that mutex waits this out, so it is the ceiling on such a stall.
        static inline std::atomic<long long> layerRefreshHoldNs{0};
        static inline std::atomic<long long> refreshTilesLockNs{0};    // waiting for the tile mutex the tile threads hold
        static inline std::atomic<long long> tileRendererLockNs{0};    // the OTHER side: render thread waiting for that same mutex
        static inline std::atomic<long long> refreshTilesNs{0};        // the changed path only
        static inline std::atomic<long long> setVisibleTilesLockNs{0}; // waiting for the renderer mutex
        static inline std::atomic<long long> terrainCoarseningNs{0};
        static inline std::atomic<long long> tileSurfacesNs{0};
        static inline std::atomic<long long> labelMapsNs{0};
        static inline std::atomic<long long> renderTilesNs{0};
        static inline std::atomic<long long> spanUnionsNs{0};          // bridge chords, reference tiles included
        static inline std::atomic<long long> labelAnchorNs{0};         // new labels onto the terrain, off the render thread
        // Terrain drape bakes: how many a frame gets through, how many were waiting, and what
        // one costs. This is what decides how fast 3D content appears.
        static inline std::atomic<long long> drapeBakes{0};
        static inline std::atomic<long long> drapeBakeQueued{0};
        // WHY a tile is in the queue, which is the difference between "the map is still filling in"
        // and "something invalidates tiles that are already correct". A static scene queueing stale
        // or restack tiles every frame is the second.
        static inline std::atomic<long long> drapeQueuedBlank{0};   // no picture at all: a hole
        static inline std::atomic<long long> drapeQueuedRestack{0}; // the layer stack changed under it
        static inline std::atomic<long long> drapeQueuedStandIn{0}; // an ancestor's picture is showing
        static inline std::atomic<long long> drapeQueuedPartial{0}; // a layer is absent from it
        static inline std::atomic<long long> drapeQueuedStale{0};   // its own picture, older fingerprint
        // What made a baked tile need baking again: its own fingerprint moved, or its coverage masks
        // were evicted on their own. Different faults, and only one of them is about the light.
        static inline std::atomic<long long> drapeStaleFingerprint{0};
        static inline std::atomic<long long> drapeStaleMask{0};
        // The fingerprint terms shared by EVERY tile (scene radiance, background emissive). If these
        // move on a still camera, every cached drape goes stale at once however correct it still is.
        static inline std::atomic<long long> drapeGlobalTermChanges{0};
        // The drape cache itself: what it throws away, and whether a mask could be given a texture
        // at all. A mask that never bakes makes its whole tile re-bake every frame, for ever.
        // What a bake spends its time on, by geometry type: a line drape visibly lags a fill one.
        static inline std::atomic<long long> drapeBakeLineNs{0};
        static inline std::atomic<long long> drapeBakeLineDraws{0};
        static inline std::atomic<long long> drapeBakePolygonNs{0};
        static inline std::atomic<long long> drapeBakePolygonDraws{0};
        static inline std::atomic<long long> drapeBakeOtherNs{0};
        static inline std::atomic<long long> drapeBakeOtherDraws{0};
        static inline std::atomic<long long> drapeEvictColour{0};
        static inline std::atomic<long long> drapeEvictMask{0};
        static inline std::atomic<long long> drapeMaskAcquireFail{0};
        static inline std::atomic<long long> drapeBakeNs{0};
        static inline std::atomic<long long> geometrySkips{0};   // renderTileGeometry calls that set up and then bailed out (invisible)

        // resolveExtrusionBases: a miss re-walks every vertex of the geometry, so what matters is
        // how often the cached answer is NOT taken, and how much of that is a resolve abandoned
        // part-way because a DEM had not decoded (which repeats every frame until it does).
        static inline std::atomic<long long> extrusionResolveCalls{0};
        static inline std::atomic<long long> extrusionResolveHits{0};     // cached, same base version
        static inline std::atomic<long long> extrusionResolveUnresolved{0}; // walked, then gave up
        static inline std::atomic<long long> extrusionResolveVertices{0}; // vertices walked on a miss
        static inline std::atomic<long long> extrusionElevQueries{0};
        static inline std::atomic<long long> extrusionResolveNs{0};
        static inline std::atomic<long long> extrusionVersionBumps{0};   // global invalidations (every geometry)
        static inline std::atomic<long long> extrusionPendingTiles{0};   // elevation tiles queued for a targeted re-resolve
        static inline std::atomic<long long> extrusionBasesCleared{0};   // geometries marked stale by those tiles

        // Elevation texture pipeline (the SDK's ElevationTextureCache). Extra DEM detail multiplies the
        // tiles by four a level, and these say which end pays for it: the encode worker, the per-frame
        // upload budget, or simply more distinct textures to bind.
        // Live ElevationTextureCache instances. One encode THREAD each, so a stale cache kept alive
        // by a lambda that captured its shared_ptr keeps encoding tiles nobody will draw.
        static inline std::atomic<long long> demCachesLive{0};
        // Which detail levels are in use across the caches, as a bitmask of 1<<level: a single bit
        // means one shared cache would serve every layer, several means it must be keyed by level.
        static inline std::atomic<long long> demDetailMask{0};
        static inline std::atomic<long long> demDetailClears{0}; // caches emptied by a detail-level change - each one re-encodes everything
        static inline std::atomic<long long> demEncodeTexels{0};
        // One encode, split three ways. It read 242 ms once and ~5 s another time on ONE thread, and
        // the long ones match the interval almost exactly - so it BLOCKS rather than computes, and
        // this says where. The sampler walking cold neighbour grids is the suspicion, not the answer.
        static inline std::atomic<long long> demEncodeTextureNs{0}; // encodeTextureWithBorders
        static inline std::atomic<long long> demEncodeBitmapNs{0};  // the Bitmap copy mapbox does not make
        static inline std::atomic<long long> demEncodeNodeNs{0};    // the node texture and its own Bitmap
        // Inside the node texture: an EDGE node is recomputed from the neighbours by box-averaging
        // boxX*boxY texels through a std::function, and boxX scales with how much coarser the
        // neighbour is - quadratically. texels/call is the box, and what decides whether that is it.
        static inline std::atomic<long long> demNodeEdgeCalls{0};
        static inline std::atomic<long long> demNodeBoxTexels{0};
        // Where an edge node's box texels are actually ANSWERED FROM, which is what decides whether
        // the unused latticeRuns/latticeSum closed form is worth wiring in: it collapses a run of
        // samples that land in one COARSE neighbour cell, and does nothing for the other two.
        // Own texels are a clamped index read and the summed-area table can answer them in bulk;
        // a same-level neighbour is also a plain index read. Only the coarse bucket pays a full
        // bilinear sampleHeight per texel, so only that bucket is the case for the closed form.
        static inline std::atomic<long long> demNodeTexelsOwn{0};
        static inline std::atomic<long long> demNodeTexelsSameLevel{0};
        static inline std::atomic<long long> demNodeTexelsCoarse{0};
        // The encode's own THREAD CPU time, against the wall time encodeWorkerMs measures. Identical
        // read counts have cost 575 ms and 11886 ms, so the question is whether the thread is
        // computing or waiting - cpu ~ wall means tune the loop, cpu << wall means stop tuning it.
        static inline std::atomic<long long> demEncodeCpuNs{0}; // padded texels per encode, to size the cost against mapbox's 258 squared
        static inline std::atomic<long long> demEncodes{0};      // full padded-texture encodes on the worker
        static inline std::atomic<long long> demBorderPatches{0}; // border-ring-only encodes
        static inline std::atomic<long long> demEncodeNs{0};     // worker time in both
        static inline std::atomic<long long> demUploads{0};      // glTexImage2D uploads on the GL thread
        static inline std::atomic<long long> demUploadNs{0};
        static inline std::atomic<long long> demPatchUploads{0}; // glTexSubImage2D border patches
        static inline std::atomic<long long> demPatchNs{0};
        static inline std::atomic<long long> demTexturesLive{0}; // textures in the cache (gauge)
        static inline std::atomic<long long> demTexturesResolved{0}; // distinct textures a frame resolves (gauge)
        static inline std::atomic<long long> demTileZoomGap{0};      // render tile zoom - elevation tile zoom (gauge)

        // ElevationManager's DECODED GRID cache, which is a different thing from the texture cache
        // above: it is what every height query, label anchor and mesh vertex reads. Every insert
        // bumps the global elevation version, so every insert re-anchors labels and moves the
        // surface. Its capacity is a grid COUNT (MIN_CACHED_GRIDS), and a view whose working set
        // exceeds it evicts grids still in use and reloads them forever - which looks exactly like
        // data still streaming in, except it never ends. These separate the two: inserts that keep
        // arriving with `bytes` pinned at `capacity` are thrash, not streaming.
        static inline std::atomic<long long> elevGridInserts{0};   // grids stored (each one bumps the version)
        static inline std::atomic<long long> elevGridReinserts{0}; // of those, tiles this manager had ALREADY loaded once
        static inline std::atomic<long long> elevGridBytes{0};     // cache bytes in use (gauge)
        static inline std::atomic<long long> elevGridCapacity{0};  // cache capacity in bytes (gauge)
        // Distinct tiles this manager has ever loaded (gauge). Against the cache's grid count
        // (capacity/grid size, MIN_CACHED_GRIDS) this is the one number that says whether the cap
        // is low enough: plateauing well under it means the set fits and the thrash is over,
        // plateauing above it means the cap has to come down further or the cache has to grow.
        static inline std::atomic<long long> elevGridDistinctEver{0};
        // These counters are GLOBAL, and a map has one ElevationManager per TerrainOptions - so a
        // panorama opened over a 3D map has two, both summing in here. Without this, thrash in the
        // map behind reads as thrash in the panorama. Grid size distinguishes them too: one source
        // serving tiles of two resolutions is what grew the cache from 147 to 336 MB.
        static inline std::atomic<long long> elevGridManagers{0};  // live ElevationManager instances (gauge)
        static inline std::atomic<long long> elevGridSizeKB{0};    // last inserted grid's size (gauge)
        // ANCESTOR RESOLUTION. A tile the source could not answer at its own zoom is cached POINTING
        // AT its ancestor, so later lookups stop at that alias instead of asking for the tile again.
        // If the alias is what most ancestor answers come from, a tile that once resolved coarse
        // stays coarse for the session - and since which tiles lose that race is decided by arrival
        // order, the same tile is shaded differently on different runs. 'walk' is the honest case:
        // no entry for the tile at all, so the search climbed to a cached ancestor.
        static inline std::atomic<long long> elevAncestorAliasPuts{0}; // tile ids pointed at an ancestor
        static inline std::atomic<long long> elevAncestorAliasHits{0}; // lookups answered by that alias
        static inline std::atomic<long long> elevAncestorWalkHits{0};  // answered by climbing, no alias
        static inline std::atomic<long long> elevExactHits{0};         // answered by the tile's own grid

        // Where a single renderTileGeometry call goes, in nanoseconds, split at the
        // boundaries a fix would actually move. Only meaningful divided by geometryDraws.
        static inline std::atomic<long long> geomProgramNs{0};   // shader program selection, useProgram, fog uniforms
        static inline std::atomic<long long> geomTerrainNs{0};   // MVP, depth bias, terrain/shadow/translate uniforms
        static inline std::atomic<long long> geomStyleNs{0};     // style parameter uniform uploads (colour/width/offset/pattern tables)
        static inline std::atomic<long long> geomStyleEvalNs{0}; // the colour/width/offset function calls alone
        static inline std::atomic<long long> geomCompileNs{0};   // compiled-geometry map lookup (and the VBO upload on a miss)
        static inline std::atomic<long long> geomCompileMisses{0}; // of which were misses, i.e. actually uploaded a VBO
        static inline std::atomic<long long> geomCompileStale{0};  // lookups that hit a dead geometry's entry at a recycled address
        static inline std::atomic<long long> geomBindNs{0};      // VAO / vertex attribute binding, lighting shader setup
        static inline std::atomic<long long> geomDrawNs{0};      // glDrawElements
        // Cost of one VT_STAT_SPLIT itself, measured back-to-back with no work between. The
        // sections above each carry one of these, so subtract it before believing them.
        static inline std::atomic<long long> geomProbeNs{0};
        // Style function memo: how often a draw asks for a value, and how often the answer had
        // to be computed (a constant counts as neither - it never reaches the cache).
        static inline std::atomic<long long> styleFuncLookups{0};
        static inline std::atomic<long long> styleFuncMisses{0};
        static inline std::atomic<long long> styleFuncConstants{0};
        static inline std::atomic<long long> styleParameters{0}; // sum of parameterCount over the calls, i.e. the loop trip count
        static inline std::atomic<long long> styleFuncEvalNs{0}; // time inside the style function objects themselves (misses only)
        static inline std::atomic<long long> viewStateChanges{0}; // setViewState calls - each one drops the per-frame memos

        // Culling
        static inline std::atomic<long long> cullerPasses{0};
        static inline std::atomic<long long> cullerVisibilityFlips{0}; // labels that appeared or disappeared
        // Wall time inside LabelCuller::process, summed over the layers of a pass - where retrying a
        // label's several sides costs anything. It runs on the placement worker, never on the GL thread,
        // so no frame section shows it.
        static inline std::atomic<long long> cullerNs{0};
        // Labels the perspective cut dropped before they cost a placement - the horizon band.
        static inline std::atomic<long long> cullerDistanceCut{0};
        // Labels hidden by the STYLE's own max-distance (text/shield/marker 'max-distance'), which is
        // a different rule from the perspective cut above and was the one fate a line did not report.
        // Counted because of what its absence cost: a panorama with the app's max-distance left at
        // 10 km showed every summit once - the pass that places a label cannot measure its distance
        // yet - and hid it on the next pass, and the line said only that 'considered' did not add up.
        static inline std::atomic<long long> cullerMaxDistanceCut{0};
        static inline std::atomic<long long> cullerConsidered{0};
        // LabelCuller::process by phase - which one a time budget would have to slice.
        static inline std::atomic<long long> cullerCollectNs{0}; // updatePlacement + variant envelopes, per label
        static inline std::atomic<long long> cullerSortNs{0};
        static inline std::atomic<long long> cullerInsertNs{0};  // greedy grid insertion, per label
        // What becomes of a considered label: cut by distance, thrown out by updatePlacement as
        // off-screen/unplaceable, or carried into the sort - and of those, how many end up drawn.
        static inline std::atomic<long long> cullerInvalid{0};
        static inline std::atomic<long long> cullerSorted{0};
        static inline std::atomic<long long> cullerVisible{0};
        // Why a sorted label did NOT end up drawn: its envelope was refused (off-screen, or a
        // surface-laid label the view meets edge-on), or it lost to a neighbour on the grid.
        static inline std::atomic<long long> cullerNotFacing{0};
        static inline std::atomic<long long> cullerCollided{0};
        // Hidden by 3D content DURING placement, so it reserved no grid slot. Before this, an
        // occluded label still held its slot and suppressed a visible neighbour - mapbox returns an
        // empty collision box for one instead (collision_index.ts).
        static inline std::atomic<long long> cullerOccluded{0};
    };
}

#define VT_STAT_INC(name) (massif::vt::RenderStats::name++)
#define VT_STAT_ADD(name, value) (massif::vt::RenderStats::name += (value))
#define VT_STAT_OR(name, value) (massif::vt::RenderStats::name |= (value))
#define VT_STAT_SET(name, value) (massif::vt::RenderStats::name = (value))
// A clock read is ~30 ns here, so a handful per draw is affordable; 'var' is reset to the
// current time so the same variable can walk through consecutive sections of one draw.
#define VT_STAT_CLOCK(var) std::chrono::steady_clock::time_point var = std::chrono::steady_clock::now()
#define VT_STAT_SPLIT(name, var) do { \
        std::chrono::steady_clock::time_point vtStatNow = std::chrono::steady_clock::now(); \
        massif::vt::RenderStats::name += std::chrono::duration_cast<std::chrono::nanoseconds>(vtStatNow - (var)).count(); \
        (var) = vtStatNow; \
    } while (false)

#else

#define VT_STAT_INC(name) ((void)0)
#define VT_STAT_ADD(name, value) ((void)0)
#define VT_STAT_OR(name, value) ((void)0)
#define VT_STAT_SET(name, value) ((void)0)
#define VT_STAT_CLOCK(var) ((void)0)
#define VT_STAT_SPLIT(name, var) ((void)0)

#endif

#endif
