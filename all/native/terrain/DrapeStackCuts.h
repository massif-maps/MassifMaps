/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DRAPESTACKCUTS_H_
#define _MASSIF_DRAPESTACKCUTS_H_

#include <cstddef>
#include <map>
#include <vector>

namespace massif {

    /**
     * Which occlusion mask a live (no-drape) style layer is drawn through, so draped layers above it in
     * the style still cover it although the drape is drawn first. See docs/internals/rendering/04-terrain.md.
     */
    struct DrapeStackCuts {
        struct Unit {
            std::size_t layerIndex; // the drape layer this style layer belongs to
            int styleLayerIdx;      // vt::TileLayer::getLayerIndex
            bool draped;            // in the bake, as opposed to drawn live
        };
        // The mask covers every draped unit from here on: styleLayerIdx and up in layerIndex, then all later layers.
        struct Cut {
            std::size_t layerIndex;
            int styleLayerIdx;
        };

        /**
         * units must be in draw order; layerMasks (per drape layer) gets styleLayerIdx -> mask index, one mask
         * per live->draped transition. A live layer left out draws unmasked. Returns true when maxMasks capped it.
         */
        static bool compute(const std::vector<Unit>& units, std::size_t maxMasks, std::vector<Cut>& cuts, std::vector<std::map<int, int> >& layerMasks) {
            bool capped = false;
            // Backwards, so each live unit's nearest later draped unit is known; its position is the mask's identity.
            int nearestDrapedUnit = -1;
            std::map<int, int> maskIndexByUnit;
            for (int k = static_cast<int>(units.size()) - 1; k >= 0; k--) {
                if (units[k].draped) {
                    nearestDrapedUnit = k;
                    continue;
                }
                if (nearestDrapedUnit < 0) {
                    continue; // nothing draped after it: already on top
                }
                auto maskIt = maskIndexByUnit.find(nearestDrapedUnit);
                if (maskIt == maskIndexByUnit.end()) {
                    if (cuts.size() >= maxMasks) {
                        capped = true;
                        continue;
                    }
                    maskIt = maskIndexByUnit.emplace(nearestDrapedUnit, static_cast<int>(cuts.size())).first;
                    cuts.push_back(Cut { units[nearestDrapedUnit].layerIndex, units[nearestDrapedUnit].styleLayerIdx });
                }
                if (units[k].layerIndex < layerMasks.size()) {
                    layerMasks[units[k].layerIndex][units[k].styleLayerIdx] = maskIt->second;
                }
            }
            return capped;
        }

        /**
         * A hash of the cut positions, folded into the mask fingerprints so a style reorder that
         * leaves the tile content alone still re-bakes them.
         */
        static std::size_t signature(const std::vector<Cut>& cuts) {
            std::size_t signature = 0;
            for (const Cut& cut : cuts) {
                std::size_t cutHash = cut.layerIndex * 1000003 + static_cast<std::size_t>(cut.styleLayerIdx + 1);
                signature ^= cutHash + 0x9e3779b9 + (signature << 6) + (signature >> 2);
            }
            return signature;
        }
    };

}

#endif
