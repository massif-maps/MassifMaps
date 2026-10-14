/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DISTANCETILELOD_H_
#define _MASSIF_DISTANCETILELOD_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace massif {

    /**
     * The free roam raster cut (02-tiles.md, "Free roam: the zoom follows the distance"). A tile splits while it spans
     * more than a pixel budget on screen as seen from its NEAREST point, so the level falls one per doubling of the
     * distance whatever way the camera looks. Every tile carries the budget scale it stops splitting at (`critical`),
     * and the scale that fits the tile budget is read off the finished tree: a pure function of the view, with no
     * state carried between culls.
     */
    struct DistanceTileLOD {
        struct Node {
            double critical; // splits while the scale is under this; 0 never splits, infinity always does
            double parentCritical; // the parent's: this node exists while the scale is under it (infinity for a root)
            int bucket; // 0 = visible, which is what the budget counts
        };

        static constexpr double MAX_SCALE = 32.0; // five levels coarser than the rule
        static constexpr std::size_t MAX_NODES = 16384; // a runaway tree is cut short, not walked
        static constexpr std::size_t DEFAULT_BUDGET = 112;
        static constexpr std::size_t MIN_BUDGET = 24;

        /** The scale a tile stops splitting at: its edge on screen, over the pixels a tile may span. */
        static double criticalScale(double tileEdge, double nearestDistance, double focalPixels, double maxEdgePixels) {
            if (!(maxEdgePixels > 0) || !(nearestDistance > 0)) {
                return std::numeric_limits<double>::infinity();
            }
            return tileEdge * focalPixels / (nearestDistance * maxEdgePixels);
        }

        static bool isLeaf(const Node& node, double scale) {
            return node.critical <= scale && scale < node.parentCritical;
        }

        static std::size_t countVisible(const std::vector<Node>& nodes, double scale) {
            std::size_t count = 0;
            for (const Node& node : nodes) {
                count += (node.bucket == 0 && isLeaf(node, scale)) ? 1 : 0;
            }
            return count;
        }

        /** The finest scale (1 = the rule itself) whose cut keeps at most `budget` visible tiles. */
        static double fitScale(const std::vector<Node>& nodes, std::size_t budget) {
            if (countVisible(nodes, 1.0) <= budget) {
                return 1.0;
            }
            // The count only changes where some tile stops splitting.
            std::vector<double> scales;
            for (const Node& node : nodes) {
                if (node.critical > 1.0 && node.critical <= MAX_SCALE) {
                    scales.push_back(node.critical);
                }
            }
            std::sort(scales.begin(), scales.end());
            scales.erase(std::unique(scales.begin(), scales.end()), scales.end());
            std::size_t low = 0, high = scales.size();
            while (low < high) {
                std::size_t middle = (low + high) / 2;
                if (countVisible(nodes, scales[middle]) <= budget) {
                    high = middle;
                } else {
                    low = middle + 1;
                }
            }
            return low < scales.size() ? scales[low] : MAX_SCALE;
        }

        /**
         * How many visible tiles the drape cover may take so that it fits the drape cache, which holds colour drapes
         * of `resolution` texels in `cacheMegabytes`, at most `entryCap` of them. Under a third of them stay free for
         * the stand-ins and the generation behind the cover.
         */
        static std::size_t budget(int resolution, int cacheMegabytes, std::size_t entryCap) {
            if (resolution <= 0 || cacheMegabytes <= 0) {
                return DEFAULT_BUDGET;
            }
            std::size_t texels = static_cast<std::size_t>(std::min(2048, std::max(128, resolution)));
            std::size_t bytesPerEntry = texels * texels * 4;
            std::size_t entries = std::min(entryCap, static_cast<std::size_t>(cacheMegabytes) * 1024 * 1024 / bytesPerEntry);
            return std::max(MIN_BUDGET, entries * 7 / 10);
        }
    };

}

#endif
