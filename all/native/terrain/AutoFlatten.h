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
     * The auto-flatten rule and its ramp, free of the renderer so the host tests can check it.
     * See docs/internals/rendering/04-terrain.md.
     */
    struct AutoFlatten {
        // Restore 3D at 1.5x the parallax threshold, and 2 degrees below the tilt one.
        static constexpr float PARALLAX_HYSTERESIS = 1.5f;
        static constexpr float TILT_HYSTERESIS = 2.0f;

        /**
         * On-screen displacement of the highest ground in view, in pixels. heightRange and
         * cameraDistance share internal units, so the metres-to-internal latitude scale cancels.
         */
        static double parallax(double halfDiagonalPixels, double heightRange, double cameraDistance) {
            if (!(cameraDistance > 0)) {
                return 0;
            }
            return halfDiagonalPixels * heightRange / cameraDistance;
        }

        /**
         * Whether the terrain should be flat. A threshold of 0 disables that half of the rule.
         * flattening (the current state) widens the thresholds, so a camera parked on one does not oscillate.
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
         * Fires on an edge, not a level: written every frame, the rule would overwrite an app's explicit
         * setFlattened on the next frame; this way it is left alone until the camera crosses a threshold.
         */
        struct Trigger {
            int last = -1; // -1 until the rule has answered once

            /**
             * cameraPlaced: whether the app has placed the camera yet. The SDK's default view (top-down,
             * world zoom) is not judged, or a map about to tilt would start flat.
             */
            bool changed(bool decision, bool cameraPlaced = true) {
                if (!cameraPlaced) {
                    return false;
                }
                int value = decision ? 1 : 0;
                if (last == value) {
                    return false;
                }
                last = value;
                return true;
            }
        };

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
