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
     * The zoom a slot's style config is RESOLVED at, quantised.
     *
     * Resolving one walks the style and evaluates its rules - 8 ms a slot, and the render thread asks
     * for every slot, every frame. Memoising it on the exact view zoom looks sufficient and is not:
     * with 3D terrain the camera clearance bound is rebuilt each frame from the ground height under
     * the camera, so panning over a slope drifts the zoom by a hair and misses every time. Measured
     * on the Crosscall: a 254 ms frame, all of it in that walk. In 2D the zoom is bit-stable while
     * panning, which is the whole reason 2D never showed this.
     *
     * So the zoom is rounded DOWN to a step. That is exact in the one place it must be: rule
     * selection uses the INTEGER zoom (mapnikvt's resolveLayerConfig), and flooring cannot move it.
     * What does step is a config property that interpolates with the view zoom, by at most one step.
     *
     * The same trade the drape already makes for its bake fingerprint (DrapeTuning::bakeZoomTerm) and
     * applyConfig for its decode-affecting values. mapbox has no equivalent because it needs none:
     * a per-frame recalculate walks a pre-parsed property array, not the style.
     *
     * Free of the renderer on purpose, so it is testable on the host.
     */
    struct StyleConfigZoom {
        /** Steps per zoom level. 16 is ~0.06 of a level, well under a visible change in a width or a colour. */
        static constexpr float STEPS_PER_LEVEL = 16.0f;

        static float quantise(float viewZoom) {
            if (!std::isfinite(viewZoom)) {
                return viewZoom; // never a usable key, but must not become one that can never match
            }
            return std::floor(viewZoom * STEPS_PER_LEVEL) / STEPS_PER_LEVEL;
        }
    };

}

#endif
