/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DRAPECOVERLEAVES_H_
#define _MASSIF_DRAPECOVERLEAVES_H_

#include <cstddef>
#include <set>
#include <vector>

#include <vt/TileId.h>

namespace massif {

    /**
     * The drape cover as a quadtree partition: each top split down to the finest collected tile inside it, never past
     * zoomLimit. A split-off child the camera cannot see is dropped, as maplibre's terrain tiles stop at the frustum: a
     * coarse stand-in split down to the view otherwise left three off-screen siblings per level, each fetching its DEM
     * (04-terrain.md). Over maxTiles, what is left of the stack stays coarse.
     */
    template <typename InView>
    std::vector<vt::TileId> buildDrapeCoverLeaves(const std::vector<vt::TileId>& tops, const std::set<vt::TileId>& collectedAncestors, int zoomLimit, std::size_t maxTiles, InView inView) {
        std::vector<vt::TileId> leaves;
        std::vector<vt::TileId> stack = tops;
        while (!stack.empty() && leaves.size() + stack.size() <= maxTiles) {
            vt::TileId tileId = stack.back();
            stack.pop_back();
            bool finerInside = collectedAncestors.find(tileId) != collectedAncestors.end();
            if (!finerInside || tileId.zoom >= zoomLimit) {
                leaves.push_back(tileId);
                continue;
            }
            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    vt::TileId child = tileId.getChild(dx, dy);
                    if (inView(child)) {
                        stack.push_back(child);
                    }
                }
            }
        }
        leaves.insert(leaves.end(), stack.begin(), stack.end());
        return leaves;
    }

}

#endif
