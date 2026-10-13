/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_RENDERTILEBLEND_H_
#define _MASSIF_VT_RENDERTILEBLEND_H_

#include <algorithm>
#include <utility>

namespace massif::vt {

    /**
     * Whether a retained render layer fades out. It holds only while it alone paints that ground and
     * its replacement has not arrived. See docs/internals/rendering/03-vt-renderer.md.
     */
    inline bool retainedLayerFades(bool replaced, bool tileVisible, bool anyActiveLayer, bool tileCurrent) {
        return !replaced && !(tileVisible && !anyActiveLayer && !tileCurrent);
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
