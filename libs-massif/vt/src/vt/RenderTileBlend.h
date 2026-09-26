/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_RENDERTILEBLEND_H_
#define _MASSIF_VT_RENDERTILEBLEND_H_

namespace massif::vt {

    /**
     * Whether a retained render layer fades out. It holds only while it alone paints that ground and
     * its replacement has not arrived. See docs/internals/rendering/03-vt-renderer.md.
     */
    inline bool retainedLayerFades(bool replaced, bool tileVisible, bool anyActiveLayer, bool tileCurrent) {
        return !replaced && !(tileVisible && !anyActiveLayer && !tileCurrent);
    }

}

#endif
