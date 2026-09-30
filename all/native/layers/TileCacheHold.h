/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILECACHEHOLD_H_
#define _MASSIF_TILECACHEHOLD_H_

#include <unordered_set>

namespace massif {

    /**
     * Keeps the tiles this frame uses in the visible cache and moves the rest to the preloading one.
     * A used tile left in the bounded preloading cache is refetched every cull once the set outgrows
     * it (docs/internals/rendering/02-tiles.md).
     */
    template <typename Cache>
    void holdTilesInUse(Cache& visibleCache, Cache& preloadingCache, const std::unordered_set<long long>& usedTileIds) {
        std::unordered_set<long long> unusedTileIds = visibleCache.keys();
        for (long long tileId : usedTileIds) {
            unusedTileIds.erase(tileId);
            if (!visibleCache.exists(tileId) && preloadingCache.exists(tileId)) {
                preloadingCache.move(tileId, visibleCache);
            }
        }
        for (long long tileId : unusedTileIds) {
            visibleCache.move(tileId, preloadingCache);
        }
    }

}

#endif
