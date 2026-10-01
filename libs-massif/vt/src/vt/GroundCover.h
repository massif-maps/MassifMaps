/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_GROUNDCOVER_H_
#define _MASSIF_VT_GROUNDCOVER_H_

#include "TileId.h"

#include <algorithm>
#include <unordered_set>

namespace massif::vt {

    /**
     * Whether the tiles paint all the ground the view sees: a tile is painted when it or an ancestor is
     * in the set, or all four of its children are. Ground outside the root tiles is never painted, and
     * the view is taken to be one connected piece of ground holding a tile of the set.
     * tileBBox(TileId) gives a tile's bounds, visible(bbox) whether the view sees any of a box.
     */
    template <typename TileBBoxFunc, typename VisibleFunc>
    bool coversVisibleGround(const std::unordered_set<TileId>& tiles, const TileBBoxFunc& tileBBox, const VisibleFunc& visible) {
        if (tiles.empty()) {
            return false;
        }
        int maxZoom = 0;
        int minRootX = 0, maxRootX = 0;
        for (const TileId& tile : tiles) {
            maxZoom = std::max(maxZoom, tile.zoom);
            int rootX = tile.x >> tile.zoom; // arithmetic shift: floor, so a wrapped copy keeps its world
            minRootX = std::min(minRootX, rootX);
            maxRootX = std::max(maxRootX, rootX);
        }

        auto covered = [&](const auto& self, const TileId& tile) -> bool {
            if (tiles.count(tile) > 0 || !visible(tileBBox(tile))) {
                return true;
            }
            if (tile.zoom >= maxZoom) {
                return false;
            }
            for (int i = 0; i < 4; i++) {
                if (!self(self, tile.getChild(i % 2, i / 2))) {
                    return false;
                }
            }
            return true;
        };
        // The ring of roots around them too, at tile granularity: one box past the world's edge is so
        // large that frustum3::inside reports it seen under any tilt.
        for (int rootY = -1; rootY <= 1; rootY++) {
            for (int rootX = minRootX - 1; rootX <= maxRootX + 1; rootX++) {
                if (!covered(covered, TileId(0, rootX, rootY))) {
                    return false;
                }
            }
        }
        return true;
    }

}

#endif
