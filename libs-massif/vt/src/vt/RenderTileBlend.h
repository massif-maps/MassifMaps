/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_RENDERTILEBLEND_H_
#define _MASSIF_VT_RENDERTILEBLEND_H_

#include <algorithm>
#include <cmath>
#include <utility>

namespace massif::vt {

    /**
     * Whether a retained render layer fades out. It holds only while it alone paints that ground and
     * its replacement has not arrived. See docs/internals/rendering/03-vt-renderer.md.
     */
    inline bool retainedLayerFades(bool replaced, bool tileVisible, bool anyActiveLayer, bool tileCurrent, bool tooCoarse = false) {
        return !replaced && (tooCoarse || !(tileVisible && !anyActiveLayer && !tileCurrent));
    }

    // TileLayer's parent search (6) plus one for a 512 px tile: past it a layer stands in for nothing.
    constexpr int MAX_RETAINED_ZOOM_DELTA = 7;

    // The zoom asked of a tile falls with its distance from the camera: a horizon tile is coarse by design.
    inline bool retainedLayerTooCoarse(int sourceZoom, float viewZoom, double tileDistance, double focusDistance) {
        if (!(focusDistance > 0)) {
            return false;
        }
        double wantedZoom = viewZoom - std::log2(std::max(1.0, tileDistance / focusDistance));
        return sourceZoom < static_cast<int>(std::floor(wantedZoom)) - MAX_RETAINED_ZOOM_DELTA;
    }

    /**
     * Adds a retained render layer unless the same layer already covers the same target there; then
     * only the higher blend is kept. Returns whether it was added.
     */
    template <typename RenderLayers, typename RenderLayer>
    bool insertRetainedLayer(RenderLayers& renderLayers, int layerIdx, RenderLayer&& renderLayer) {
        auto range = renderLayers.equal_range(layerIdx);
        for (auto it = range.first; it != range.second; it++) {
            if (it->second.layer == renderLayer.layer && it->second.targetTileId == renderLayer.targetTileId) {
                it->second.blend = std::max(it->second.blend, renderLayer.blend);
                return false;
            }
        }
        renderLayers.insert({ layerIdx, std::forward<RenderLayer>(renderLayer) });
        return true;
    }

}

#endif
