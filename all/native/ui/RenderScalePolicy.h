/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_RENDERSCALEPOLICY_H_
#define _MASSIF_RENDERSCALEPOLICY_H_

#include <algorithm>
#include <cmath>

namespace massif {

    /**
     * Which render scale the map asks its surface for: a 2D value, a 3D value, and when to switch
     * between them. See docs/internals/rendering/21-render-scale.md.
     */
    struct RenderScalePolicy {
        // Below this the map is unreadable; it also keeps a typo (0.01) from asking for a 2 px surface.
        static constexpr float MIN_SCALE = 0.25f;
        // Tilt is 90 looking straight down. The gap keeps a tilt hovering at the edge from switching back and forth.
        static constexpr float ENTER_3D_TILT = 85.0f;
        static constexpr float LEAVE_3D_TILT = 88.0f;
        // A surface resize costs a frame and a drape re-bake, so one switch must not follow another at once.
        static constexpr double MIN_HOLD_SECONDS = 1.0;

        struct State {
            bool mode3D = false;
            double lastSwitchSeconds = -1.0e9;
        };

        struct Config {
            float scale2D = 1.0f;
            float scale3D = 1.0f;
        };

        struct Input {
            float tilt = 90.0f;
            bool terrain3D = false;   // terrain is drawing and is not flattened
            bool gestureActive = false;
            double nowSeconds = 0.0;
        };

        struct Result {
            State state;
            float scale = 1.0f;
            bool pending = false;     // a switch is due but held back: the caller must ask for another frame
        };

        // Outside (0, 1], or not a number, is native: a bad value must never shrink the map.
        static float clamp(float scale) {
            if (!(scale > 0.0f) || scale >= 1.0f) {
                return 1.0f;
            }
            return std::max(scale, MIN_SCALE);
        }

        static bool wants3D(bool mode3D, float tilt, bool terrain3D) {
            return terrain3D || tilt < (mode3D ? LEAVE_3D_TILT : ENTER_3D_TILT);
        }

        static Result step(const State& state, const Config& config, const Input& input) {
            Result result;
            result.state = state;
            bool wanted = wants3D(state.mode3D, input.tilt, input.terrain3D);
            if (wanted != state.mode3D) {
                // Never mid-gesture: the touches in flight were mapped with the old scale.
                bool held = input.gestureActive || input.nowSeconds - state.lastSwitchSeconds < MIN_HOLD_SECONDS;
                if (held) {
                    result.pending = true;
                } else {
                    result.state.mode3D = wanted;
                    result.state.lastSwitchSeconds = input.nowSeconds;
                }
            }
            result.scale = clamp(result.state.mode3D ? config.scale3D : config.scale2D);
            return result;
        }
    };

}

#endif
