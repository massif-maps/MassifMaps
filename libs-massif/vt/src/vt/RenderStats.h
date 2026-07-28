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

        // Culling
        static inline std::atomic<long long> cullerPasses{0};
        static inline std::atomic<long long> cullerVisibilityFlips{0}; // labels that appeared or disappeared
    };
}

#define VT_STAT_INC(name) (carto::vt::RenderStats::name++)
#define VT_STAT_ADD(name, value) (carto::vt::RenderStats::name += (value))
#define VT_STAT_SET(name, value) (carto::vt::RenderStats::name = (value))

#else

#define VT_STAT_INC(name) ((void)0)
#define VT_STAT_ADD(name, value) ((void)0)
#define VT_STAT_SET(name, value) ((void)0)

#endif

#endif
