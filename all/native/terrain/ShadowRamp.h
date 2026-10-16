/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_SHADOWRAMP_H_
#define _MASSIF_SHADOWRAMP_H_

#include <algorithm>
#include <cmath>

namespace massif {

    /**
     * What the shadow map does while the 2D/3D ramp moves the ground. Building shadows stay through it:
     * the map is re-cast every STEP of the exaggeration, not every frame. See docs/internals/rendering/04-terrain.md.
     */
    struct ShadowRamp {
        // Share of the configured exaggeration between two casts: a shadow slides at most this much of the relief.
        static constexpr float STEP = 1.0f / 32.0f;

        static float exaggeration(float configured, float flattenRatio) {
            return configured * (1.0f - std::min(1.0f, std::max(0.0f, flattenRatio)));
        }

        // The ground's own shadows need the settled relief: the cascade fit and the caster tiles follow moving heights.
        static bool castsGround(float flattenRatio) {
            return flattenRatio <= 0.0f;
        }

        // A ramp step is rationed; a settled value (either end, or an app changing the exaggeration) is exact.
        static bool needsCast(float configured, float flattenRatio, float castExaggeration) {
            float wanted = exaggeration(configured, flattenRatio);
            if (flattenRatio > 0.0f && flattenRatio < 1.0f) {
                return std::abs(wanted - castExaggeration) > configured * STEP;
            }
            return wanted != castExaggeration;
        }
    };

}

#endif
