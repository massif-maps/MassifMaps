/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_RENDERTILEBLEND_H_
#define _MASSIF_VT_RENDERTILEBLEND_H_

namespace massif::vt {

    /**
     * Whether a retained (inactive) render layer fades out, or holds. Free of the renderer so the
     * host tests can reach it; see docs/internals/rendering/03-vt-renderer.md.
     *
     * It holds in one case only: this layer is the only thing painting that ground and the tile
     * that will take over has not arrived. Fading then is a hole - a fetch does not fit in the ten
     * frames a fade lasts. Everything else fades, including a render tile of the CURRENT set that
     * decoded without this layer, which is an answer rather than a gap.
     */
    inline bool retainedLayerFades(bool replaced, bool tileVisible, bool anyActiveLayer, bool tileCurrent) {
        return !replaced && !(tileVisible && !anyActiveLayer && !tileCurrent);
    }

}

#endif
