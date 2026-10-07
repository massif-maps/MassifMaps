/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILECACHESTASH_H_
#define _MASSIF_TILECACHESTASH_H_

#include <stdext/timed_lru_cache.h>

#include <chrono>
#include <utility>

namespace massif {

    /**
     * A terrain decode swap: puts the visible tiles aside and brings back the set put aside earlier, so a
     * repeat 2D/3D switch reuses it (docs/internals/rendering/04-terrain.md). Outgoing visible tiles the
     * restored set lacks stay drawn as invalid stand-ins until their refetch lands.
     */
    template <typename Tile, typename SizeOf>
    void swapStashedTiles(cache::timed_lru_cache<long long, Tile>& visibleCache, cache::timed_lru_cache<long long, Tile>& preloadingCache, cache::timed_lru_cache<long long, Tile>& stashedVisibleCache, SizeOf sizeOf) {
        std::swap(visibleCache, stashedVisibleCache);
        // Not stashed: up to the whole tile cache capacity per layer, and the switch waits on visible tiles only.
        preloadingCache.clear();
        auto now = std::chrono::steady_clock::now();
        for (long long tileId : stashedVisibleCache.keys()) {
            if (visibleCache.exists(tileId)) {
                continue;
            }
            Tile tile;
            stashedVisibleCache.peek(tileId, tile);
            visibleCache.put(tileId, tile, sizeOf(tile));
            visibleCache.invalidate(tileId, now);
        }
    }

}

#endif
