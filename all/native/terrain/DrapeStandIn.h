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
     * Which cached drape tiles stand in for a tile that has no picture of its own yet.
     *
     * A tile zoomed OUT from is covered by the finer tiles it replaces, and those are what it
     * shows until its own bake lands. Walking the tile tree down a fixed number of levels to find
     * them costs 4^depth lookups, so it was capped at two levels - and a pinch that crosses three
     * or more levels then found nothing, leaving the leaf painted in the flat clear colour. The
     * cache is a bounded list, so the search runs over IT instead, at any depth.
     *
     * Free of the renderer and of GL on purpose, so it is testable on the host. See
     * docs/internals/rendering/04-terrain.md.
     */
    struct DrapeStandIn {
        /**
         * Whether a cached texture shows this tile's ground. A bake with NO layer in it is not a
         * picture: the tile's own content had not arrived and the finer proxies covering it are
         * left out of the bake (they do not cover the target), so drawing that texture puts the
         * flat clear colour over ground the cached finer generation still shows - measured as one
         * leaf drawn blank for 4 s across a zoom out, over cached descendants that had the map.
         * A seed is a picture (it IS the finer generation, copied in).
         */
        static bool hasPicture(bool baked, bool seeded, std::size_t layerMask) {
            return (baked && layerMask != 0) || seeded;
        }

        /**
         * Whether a tile's own bake has everything that was asked for, so nothing has to stand in
         * over it. Never true of an empty bake, whatever was wanted: the descendants are drawn over
         * it until a bake with content lands.
         */
        static bool isComplete(bool baked, std::size_t wantedMask, std::size_t bakedMask) {
            return baked && bakedMask != 0 && (wantedMask & ~bakedMask) == 0;
        }

        /**
         * The candidates that together cover `tileId` once: those inside it, minus any whose own
         * ancestor is already among them. Returns indices into `candidates`, in the order given.
         *
         * Coarsest wins, so `candidates` must be ordered coarsest first (the drape cache is keyed
         * on (zoom, x, y), which gives exactly that). Drawing a tile AND its children would paint
         * the same ground twice, and the two are separate surfaces at different tesselations - the
         * finer one reads as sitting slightly off the terrain.
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
