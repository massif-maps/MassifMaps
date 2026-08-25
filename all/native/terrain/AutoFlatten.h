/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_AUTOFLATTEN_H_
#define _MASSIF_AUTOFLATTEN_H_

#include <algorithm>
#include <cmath>

namespace massif {

    /**
     * The auto-flatten rule: when 3D terrain stops earning its cost, and how fast it sinks flat.
     * Free of the renderer and of TerrainOptions on purpose - this is the part with the state,
     * the hysteresis and the arithmetic, so it is the part worth testing on the host.
     * See TerrainOptions::setAutoFlattenParallax and docs/internals/rendering/04-terrain.md.
     */
    struct AutoFlatten {
        // Restore 3D at 1.5x the parallax threshold, and 2 degrees below the tilt one.
        static constexpr float PARALLAX_HYSTERESIS = 1.5f;
        static constexpr float TILT_HYSTERESIS = 2.0f;

        /**
         * How far the highest ground in view moves on screen because it is displaced, in pixels.
         * heightRange and cameraDistance are in the same units; both are internal units here, so
         * the metres-to-internal latitude scale cancels.
         */
        static double parallax(double halfDiagonalPixels, double heightRange, double cameraDistance) {
            if (!(cameraDistance > 0)) {
                return 0;
            }
            return halfDiagonalPixels * heightRange / cameraDistance;
        }

        /**
         * Whether the terrain should be flat. A threshold of 0 disables that half of the rule;
         * with both disabled the answer is always false. flattening is the state we are already
         * in, and is what widens the thresholds - a camera parked on one would otherwise
         * oscillate between the two modes for as long as it sits there.
         */
        static bool shouldFlatten(double parallaxPixels, float parallaxThreshold, float tilt, float tiltThreshold, bool flattening) {
            if (tiltThreshold > 0 && tilt >= (flattening ? tiltThreshold - TILT_HYSTERESIS : tiltThreshold)) {
                return true;
            }
            if (parallaxThreshold > 0 && parallaxPixels < parallaxThreshold * (flattening ? PARALLAX_HYSTERESIS : 1.0f)) {
                return true;
            }
            return false;
        }

        /**
         * One frame of the ramp, 0 (full 3D) to 1 (flat). A duration of 0 switches instantly.
         */
        static float step(float ratio, bool flatten, float deltaSeconds, float duration) {
            float target = flatten ? 1.0f : 0.0f;
            float delta = duration > 0 ? std::max(0.0f, deltaSeconds) / duration : 1.0f;
            if (target > ratio) {
                return std::min(target, ratio + delta);
            }
            return std::max(target, ratio - delta);
        }
    };

}

#endif
