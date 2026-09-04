/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_SHADOWCASTERRING_H_
#define _MASSIF_SHADOWCASTERRING_H_

#include <algorithm>
#include <cstddef>

namespace massif {

    /**
     * The zoom the shadow caster RING is generated at, and the tile grid that follows from it.
     * Free of the renderer on purpose, so the bound is testable on the host. See
     * MapRenderer::applyTerrainShadows and docs/internals/rendering/09-shadows.md.
     */
    struct ShadowCasterRing {
        /** The cover's footprint at one zoom, before the margin is added. */
        struct Grid {
            int zoom = 0;
            int minX = 0, minY = 0, maxX = 0, maxY = 0;
        };

        /** How many tiles the ring generates for this footprint, margin included. */
        static std::size_t tileCount(const Grid& grid, int margin) {
            std::size_t width = static_cast<std::size_t>(grid.maxX - grid.minX + 1 + 2 * margin);
            std::size_t height = static_cast<std::size_t>(grid.maxY - grid.minY + 1 + 2 * margin);
            return width * height;
        }

        /**
         * Coarsens the ring until its grid fits maxTiles. The ring's zoom is otherwise set by the
         * THROW (relief / tan(sun altitude)), and over flat ground the throw is 0, so it stays at
         * the cover's finest zoom - while a tilted cover reaches the horizon and mixes zooms, so
         * its footprint expressed at that zoom is thousands of tiles a side. Coarsening holds the
         * same ground and drops the resolution, which is what the throw rule does already; the
         * caller's subdivision brings the resolution back where the cover is finer.
         */
        static Grid fit(const Grid& grid, int margin, std::size_t maxTiles) {
            Grid fitted = grid;
            while (fitted.zoom > 0 && tileCount(fitted, margin) > maxTiles) {
                fitted.zoom--;
                fitted.minX >>= 1; fitted.maxX >>= 1;
                fitted.minY >>= 1; fitted.maxY >>= 1;
            }
            return fitted;
        }
    };

}

#endif
