#ifndef _MASSIF_PREFETCHPLAN_H_
#define _MASSIF_PREFETCHPLAN_H_

#include <algorithm>
#include <cstddef>

namespace massif {

    struct PrefetchPlan {
        std::size_t frames;
        std::size_t cacheBytes;
    };

    // How many steps ahead fit the budget for this many visible tiles, and the cache that holds them: a step evicted
    // before the loop comes round is fetched again, and the loop stalls on it.
    inline PrefetchPlan PlanPrefetch(std::size_t visibleTiles, std::size_t wantedFrames, std::size_t stepCount, std::size_t tileBytes, std::size_t maxBytes, std::size_t minCacheBytes) {
        if (visibleTiles == 0 || stepCount < 2) {
            return { 0, minCacheBytes };
        }
        std::size_t affordable = maxBytes / (visibleTiles * tileBytes);
        std::size_t frames = std::min(std::min(wantedFrames, stepCount - 1), affordable > 2 ? affordable - 2 : 0);
        return { frames, std::max(visibleTiles * (frames + 2) * tileBytes, minCacheBytes) };
    }

}

#endif
