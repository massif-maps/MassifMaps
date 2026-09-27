/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_STYLECONFIGZOOM_H_
#define _MASSIF_STYLECONFIGZOOM_H_

#include <cmath>

namespace massif {

    /**
     * The zoom a slot's style config is resolved at, floored to a step so terrain's per-frame zoom
     * jitter still hits the memo. Exact for rule selection, which uses the integer zoom;
     * interpolated properties move by at most one step.
     */
    struct StyleConfigZoom {
        /** Steps per zoom level (~0.06 of a level, below a visible change). */
        static constexpr float STEPS_PER_LEVEL = 16.0f;

        static float quantise(float viewZoom) {
            if (!std::isfinite(viewZoom)) {
                return viewZoom; // must not become a key that never matches
            }
            return std::floor(viewZoom * STEPS_PER_LEVEL) / STEPS_PER_LEVEL;
        }
    };

}

#endif
