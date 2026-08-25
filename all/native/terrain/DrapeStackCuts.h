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
     * Where a LIVE (no-drape) style layer sits in the drape stack, and which occlusion mask it has
     * to be drawn through.
     *
     * The drape composite is baked and drawn before any live geometry, so a layer kept out of the
     * bake (TerrainOptions::NoDrapeLayerFilter) can only land on top of ALL of it - contours over
     * roads in 3D where 2D draws roads over contours. Flatten the stack into ordered units, one per
     * style layer of each drape layer, and every live unit with a draped unit after it is drawn
     * through a mask holding the accumulated coverage of everything draped above it.
     *
     * Free of the renderer on purpose: this is the ordering rule, so it is the part worth testing
     * on the host. See MapRenderer::onDrawFrame and docs/internals/rendering/04-terrain.md.
     */
    struct DrapeStackCuts {
        struct Unit {
            std::size_t layerIndex; // the drape layer this style layer belongs to
            int styleLayerIdx;      // vt::TileLayer::getLayerIndex
            bool draped;            // in the bake, as opposed to drawn live
        };
        // The mask covers every draped unit from this position on: styleLayerIdx and up inside
        // layerIndex, and all of the drape layers after it.
        struct Cut {
            std::size_t layerIndex;
            int styleLayerIdx;
        };

        /**
         * units must be in draw order. layerMasks is indexed by drape layer and filled with
         * styleLayerIdx -> mask index for every live layer that needs one; a live layer left out
         * draws unmasked, which is both the pre-#175 behaviour and the correct answer when nothing
         * draped comes after it.
         *
         * Live units sharing the same nearest draped unit share a mask, so the number of masks is
         * the number of live->draped transitions - 0 or 1 for every ordinary style. Returns true
         * when maxMasks cut the list short.
         */
        static bool compute(const std::vector<Unit>& units, std::size_t maxMasks, std::vector<Cut>& cuts, std::vector<std::map<int, int> >& layerMasks) {
            bool capped = false;
            // Backwards, so the nearest draped unit after each live one is known when it is
            // reached. Its position IS the mask's identity.
            int nearestDrapedUnit = -1;
            std::map<int, int> maskIndexByUnit;
            for (int k = static_cast<int>(units.size()) - 1; k >= 0; k--) {
                if (units[k].draped) {
                    nearestDrapedUnit = k;
                    continue;
                }
                if (nearestDrapedUnit < 0) {
                    continue; // nothing draped after it: already on top, as it should be
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
