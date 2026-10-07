/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILECACHEHOLD_H_
#define _MASSIF_TILECACHEHOLD_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
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

    // maplibre's config.MAX_TILE_CACHE_ZOOM_LEVELS: the out-of-view cache holds this many viewports of tiles.
    const int VIEWPORT_CACHE_ZOOM_LEVELS = 5;

    /**
     * maplibre's TileManager.updateCacheSize in bytes: the tiles a viewport holds, times VIEWPORT_CACHE_ZOOM_LEVELS, at
     * the size of the tiles in view now. Never below floorBytes; a 10 MB cap re-fetched every tile turned back to.
     */
    inline std::size_t viewportCacheCapacity(std::size_t visibleBytes, std::size_t visibleTiles, double viewWidth, double viewHeight, double tileSizePixels, std::size_t floorBytes) {
        if (visibleTiles == 0 || !(tileSizePixels > 0)) {
            return floorBytes;
        }
        double tilesInView = (std::ceil(viewWidth / tileSizePixels) + 1) * (std::ceil(viewHeight / tileSizePixels) + 1);
        double bytes = static_cast<double>(visibleBytes) / static_cast<double>(visibleTiles) * tilesInView * VIEWPORT_CACHE_ZOOM_LEVELS;
        return std::max(floorBytes, static_cast<std::size_t>(bytes));
    }

}

#endif
