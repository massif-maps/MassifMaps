/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DRAPETUNING_H_
#define _MASSIF_DRAPETUNING_H_

#include <cstddef>

namespace massif {

    /**
     * What a drape tile is baked AT: the texture resolution the screen asks for, capped by what the
     * cache's byte budget allows a working cover to hold. Free of the renderer on purpose, so it is
     * testable on the host. See docs/internals/rendering/04-terrain.md.
     */
    struct DrapeTuning {
        /**
         * The bake resolution, a power of two in [minResolution, maxResolution].
         * @param tileDrawSize The style's nominal tile size in points (Options::getTileDrawSize).
         * @param dpiScale The screen's scale over the unscaled DPI.
         * @param workingSet How many tiles of that size a cover is assumed to need at once.
         * @param budgetBytes The cache's byte budget; 0 for no budget at all.
         */
        static int resolution(double tileDrawSize, double dpiScale, std::size_t workingSet, std::size_t budgetBytes, int minResolution, int maxResolution) {
            // From the SCREEN: the tile LOD refines a tile until it covers at most a 2x2 block of
            // nominal tiles, so 2 * tileDrawSize * dpiScale is the widest any tile ever gets.
            double edge = 2.0 * tileDrawSize * dpiScale;
            int size = minResolution;
            while (size < edge && size < maxResolution) {
                size *= 2;
            }
            // ... then down to what the budget holds a working cover of.
            while (budgetBytes > 0 && size > minResolution && bytesPerTile(size) * workingSet > budgetBytes) {
                size /= 2;
            }
            return size;
        }

        static std::size_t bytesPerTile(int size) {
            return static_cast<std::size_t>(size) * size * 4;
        }
    };

}

#endif
