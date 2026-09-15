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
        static inline std::atomic<long long> demEncodeTexels{0}; // padded texels per encode, to size the cost against mapbox's 258 squared
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
