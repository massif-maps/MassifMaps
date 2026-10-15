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

namespace massif::vt {
    /** Which sweeps of the ghost pass can show anything (01-frame.md, "The ghost pass"). A ghost is wanted behind an
     *  extrusion or a ridge: with no extrusion drawn this frame and no terrain, nothing can hide a line and the whole
     *  pass is skipped. With a stencil buffer the extrusions' stamp serves the buildings (GHOST_SWEEP_STAMP) and the
     *  depth test only the ridges (GHOST_SWEEP_DEPTH); without one the depth test serves both. */
    enum GhostSweep { GHOST_SWEEP_STAMP = 1, GHOST_SWEEP_DEPTH = 2 };

    inline int ghostSweeps(bool stencil, bool extrusionsDrawn, bool terrain) {
        if (!stencil) {
            return (extrusionsDrawn || terrain) ? GHOST_SWEEP_DEPTH : 0;
        }
        return (extrusionsDrawn ? GHOST_SWEEP_STAMP : 0) | (terrain ? GHOST_SWEEP_DEPTH : 0);
    }
}

#endif