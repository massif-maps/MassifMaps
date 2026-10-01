/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DRAPETUNING_H_
#define _MASSIF_DRAPETUNING_H_

#include <algorithm>
#include <cstddef>
#include <vector>

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

        /**
         * One resolution per cover leaf, each the power of two its on-screen footprint asks for, then the largest
         * halved (the most over-sampled of equals first) until the cover fits budgetBytes: an oblique near leaf spans
         * thousands of pixels, a far one a few hundred, and one shared size blurred the first and wasted the second.
         * @param edgePixels Per leaf, its longest projected edge in device pixels.
         * @param budgetBytes What the whole cover may take; 0 for no budget at all.
         */
        static std::vector<int> leafResolutions(const std::vector<double>& edgePixels, std::size_t budgetBytes, int minResolution, int maxResolution) {
            std::vector<int> sizes;
            sizes.reserve(edgePixels.size());
            std::size_t bytes = 0;
            for (double edge : edgePixels) {
                int size = minResolution;
                while (size < edge && size < maxResolution) {
                    size *= 2;
                }
                sizes.push_back(size);
                bytes += bytesPerTile(size);
            }
            while (budgetBytes > 0 && bytes > budgetBytes) {
                std::size_t pick = sizes.size();
                double worst = 0;
                for (std::size_t i = 0; i < sizes.size(); i++) {
                    // Largest first: halving the most over-sampled first starved the cover to feed the one leaf at
                    // the bottom edge asking for 50000 px, which no size satisfies.
                    double excess = sizes[i] / std::max(1.0, edgePixels[i]);
                    if (sizes[i] > minResolution && (pick == sizes.size() || sizes[i] > sizes[pick] || (sizes[i] == sizes[pick] && excess > worst))) {
                        pick = i;
                        worst = excess;
                    }
                }
                if (pick == sizes.size()) {
                    break;
                }
                bytes -= bytesPerTile(sizes[pick]) - bytesPerTile(sizes[pick] / 2);
                sizes[pick] /= 2;
            }
            return sizes;
        }

        /**
         * Whether a cached texture of `current` texels must be replaced for one of `wanted`: at once to sharpen,
         * only past a factor of 4 to shrink, so a leaf near a size boundary does not re-bake as the camera moves.
         */
        static bool needsResize(int current, int wanted) {
            return wanted > current || wanted * 4 <= current;
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
