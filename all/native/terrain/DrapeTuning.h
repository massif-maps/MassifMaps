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
     * The drape bake resolution: what the screen asks for, capped by the cache's byte budget.
     * See docs/internals/rendering/04-terrain.md.
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
            // The tile LOD refines a tile to at most 2x2 nominal tiles, so this is the widest one gets on screen.
            double edge = 2.0 * tileDrawSize * dpiScale;
            int size = minResolution;
            while (size < edge && size < maxResolution) {
                size *= 2;
            }
            while (budgetBytes > 0 && size > minResolution && bytesPerTile(size) * workingSet > budgetBytes) {
                size /= 2;
            }
            return size;
        }

        static std::size_t bytesPerTile(int size) {
            return static_cast<std::size_t>(size) * size * 4;
        }

        /**
         * The bake zoom quantised by threshold, folded into the content fingerprint so a moved term re-bakes.
         * Clamped at 0: a negative zoom (free roam) would wrap the cast into a term that never repeats.
         */
        static std::size_t bakeZoomTerm(float zoom, float threshold) {
            if (!(zoom > 0) || !(threshold > 0)) {
                return 0;
            }
            return static_cast<std::size_t>(zoom / threshold);
        }
    };

}

#endif
