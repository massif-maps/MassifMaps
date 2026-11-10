/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DRAPECOVERSEEDS_H_
#define _MASSIF_DRAPECOVERSEEDS_H_

#include <map>
#include <set>
#include <vector>

#include <vt/TileId.h>

namespace massif {

    /**
     * Seeds no deeper than the deepest collected tile are dropped unless they overlap no collected tile: that ground
     * would draw nothing (startup blink, 04-terrain.md).
     */
    struct DrapeCoverSeeds {
        template <typename Collected>
        static std::vector<vt::TileId> select(const std::vector<vt::TileId>& seeds, const Collected& collected, int dataMaxZoom) {
            std::set<vt::TileId> collectedAncestors;
            for (auto it = collected.begin(); it != collected.end(); it++) {
                for (vt::TileId tileId = it->first; tileId.zoom > 0; ) {
                    tileId = tileId.getParent();
                    if (!collectedAncestors.insert(tileId).second) {
                        break;
                    }
                }
            }
            std::vector<vt::TileId> selected;
            for (const vt::TileId& seed : seeds) {
                if (seed.zoom > dataMaxZoom || !overlaps(seed, collected, collectedAncestors)) {
                    selected.push_back(seed);
                }
            }
            return selected;
        }

    private:
        template <typename Collected>
        static bool overlaps(const vt::TileId& seed, const Collected& collected, const std::set<vt::TileId>& collectedAncestors) {
            if (collectedAncestors.find(seed) != collectedAncestors.end()) {
                return true;
            }
            for (vt::TileId tileId = seed; ; tileId = tileId.getParent()) {
                if (collected.find(tileId) != collected.end()) {
                    return true;
                }
                if (tileId.zoom == 0) {
                    return false;
                }
            }
        }
    };

}

#endif
