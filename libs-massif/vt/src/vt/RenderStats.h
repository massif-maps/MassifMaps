/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_RENDERSTATS_H_
#define _CARTO_VT_RENDERSTATS_H_

/**
 * Diagnostic counters for label and tile churn, and the single switch that turns them on.
 *
 * They live in hot paths - every label placement update, every label built, every culled
 * label - so they must cost nothing when unused: with CARTO_VT_RENDER_STATS at 0 the
 * counters do not exist, the VT_STAT_* macros expand to nothing, their arguments are never
 * evaluated, and the SDK's per-second printout in MapRenderer is not compiled either.
 *
 * Set it to 1 here, or define it as a build flag, to get the printout back.
 */
#ifndef CARTO_VT_RENDER_STATS
#define CARTO_VT_RENDER_STATS 0
#endif

#if CARTO_VT_RENDER_STATS

#include <atomic>
#include <chrono>

namespace carto::vt {
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
        static inline std::atomic<long long> labelsAllocated{0};       // new vt::Label objects built in buildLabelMaps
        static inline std::atomic<long long> labelsReused{0};          // labels kept because every contributing tile geometry was unchanged
        static inline std::atomic<long long> labelsLive{0};            // labels alive after the last buildLabelMaps (gauge, not a delta)
        static inline std::atomic<long long> labelElevationReanchors{0}; // updateElevation calls that actually moved a label

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
        // Line-label runs laid out from scratch. The layout is keyed on the view-projection
        // (Label::updateLineVertexData), so this is one per visible line label per frame while
        // the camera moves, plus one per culler pass - the price of a run that follows the line
        // as the CURRENT camera projects it.
        static inline std::atomic<long long> lineLayoutBuilds{0};
        static inline std::atomic<long long> labelTransformNs{0}; // world transform of the glyph quads (what a GPU billboard would remove)
        static inline std::atomic<long long> labelAttribNs{0};    // normals / uvs / attribs / indices plumbing into the batch arrays
        // The three calls the 3D pass makes, in order.
        static inline std::atomic<long long> pass3DLabels2DNs{0};
        static inline std::atomic<long long> pass3DGeometryNs{0};
        static inline std::atomic<long long> pass3DLabels3DNs{0};
        // Label pass: the glyph quads are rebuilt from scratch for every visible label
        // every frame, then uploaded as one batch.
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
        // Terrain drape bakes: how many a frame gets through, how many were waiting, and what
        // one costs. This is what decides how fast 3D content appears.
        static inline std::atomic<long long> drapeBakes{0};
        static inline std::atomic<long long> drapeBakeQueued{0};
        static inline std::atomic<long long> drapeBakeNs{0};
        static inline std::atomic<long long> geometrySkips{0};   // renderTileGeometry calls that set up and then bailed out (invisible)

        // Elevation texture pipeline (the SDK's ElevationTextureCache, which feeds the terrain
        // texture provider). Extra DEM detail multiplies the tiles by four a level, and these say
        // which end of the pipeline pays for it: the encode worker, the per-frame upload budget,
        // or simply having more distinct textures to bind.
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
    };
}

#define VT_STAT_INC(name) (carto::vt::RenderStats::name++)
#define VT_STAT_ADD(name, value) (carto::vt::RenderStats::name += (value))
#define VT_STAT_SET(name, value) (carto::vt::RenderStats::name = (value))
// A clock read is ~30 ns here, so a handful per draw is affordable; 'var' is reset to the
// current time so the same variable can walk through consecutive sections of one draw.
#define VT_STAT_CLOCK(var) std::chrono::steady_clock::time_point var = std::chrono::steady_clock::now()
#define VT_STAT_SPLIT(name, var) do { \
        std::chrono::steady_clock::time_point vtStatNow = std::chrono::steady_clock::now(); \
        carto::vt::RenderStats::name += std::chrono::duration_cast<std::chrono::nanoseconds>(vtStatNow - (var)).count(); \
        (var) = vtStatNow; \
    } while (false)

#else

#define VT_STAT_INC(name) ((void)0)
#define VT_STAT_ADD(name, value) ((void)0)
#define VT_STAT_SET(name, value) ((void)0)
#define VT_STAT_CLOCK(var) ((void)0)
#define VT_STAT_SPLIT(name, var) ((void)0)

#endif

#endif
