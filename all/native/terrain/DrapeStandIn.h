/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DRAPESTANDIN_H_
#define _MASSIF_DRAPESTANDIN_H_

#include <cstddef>
#include <vector>

#include <vt/TileId.h>

namespace massif {

    /**
     * Which cached drape tiles stand in for a tile that has no picture of its own yet. Searches the
     * bounded cache list rather than walking the tile tree (4^depth), so any zoom-out depth is found.
     * See docs/internals/rendering/04-terrain.md.
     */
    struct DrapeStandIn {
        /**
         * Whether a cached texture shows this tile's ground. A bake with no layer is not a picture: it would
         * paint the clear colour over the finer cached generation. A seed is one (it is that generation, copied in).
         */
        static bool hasPicture(bool baked, bool seeded, std::size_t layerMask) {
            return (baked && layerMask != 0) || seeded;
        }

        /**
         * Whether a tile's own bake has everything asked for, so nothing has to stand in over it.
         * Never true of an empty bake: the descendants are drawn over it until one with content lands.
         */
        static bool isComplete(bool baked, std::size_t wantedMask, std::size_t bakedMask) {
            return baked && bakedMask != 0 && (wantedMask & ~bakedMask) == 0;
        }

        /**
         * Indices of the candidates that cover `tileId` once: those inside it, minus any with an ancestor kept.
         * `candidates` must be coarsest first (the drape cache's (zoom, x, y) order); a tile and its children
         * drawn together are two surfaces at different tesselations, and the finer one sits off the terrain.
         */
        static std::vector<std::size_t> coarsestCover(const vt::TileId& tileId, const std::vector<vt::TileId>& candidates) {
            std::vector<std::size_t> kept;
            for (std::size_t i = 0; i < candidates.size(); i++) {
                const vt::TileId& candidate = candidates[i];
                if (candidate == tileId || !tileId.covers(candidate)) {
                    continue;
                }
                bool covered = false;
                for (std::size_t j = 0; j < kept.size() && !covered; j++) {
                    covered = candidates[kept[j]].covers(candidate);
                }
                if (!covered) {
                    kept.push_back(i);
                }
            }
            return kept;
        }
    };

}

#endif
