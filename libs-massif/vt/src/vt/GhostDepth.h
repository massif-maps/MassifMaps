#ifndef _MASSIF_VT_GHOSTDEPTH_H_
#define _MASSIF_VT_GHOSTDEPTH_H_

#include <algorithm>

namespace massif::vt {
    /** The (proxy - layer) depth offset a line's ghost draw is made at (01-frame.md, "The ghost pass").
     *  The ghost is compared GL_GREATER, so it must sit nearer than every decal of the stack, or a
     *  later layer's line passes the test and the ghost paints over it: it is drawn as the stack's top
     *  ordinal. A top not published yet falls back to the layer's own ordinal. */
    inline float ghostLayerOffset(float proxyDepth, int layerOrdinal, int ordinalTop) {
        return proxyDepth - static_cast<float>(std::max(layerOrdinal, ordinalTop));
    }
}

#endif
