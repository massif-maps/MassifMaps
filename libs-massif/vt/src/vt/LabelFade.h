/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_LABELFADE_H_
#define _MASSIF_VT_LABELFADE_H_

#include <algorithm>

namespace massif::vt {

    /** maplibre's fadeDuration; the placement worker waits it out between passes. See docs/internals/rendering/06-labels.mdx. */
    inline constexpr int LABEL_FADE_DURATION_MS = 300;

    /** Default label blending speed, in full fades per second. */
    inline constexpr float DEFAULT_LABEL_BLENDING_SPEED = 1000.0f / LABEL_FADE_DURATION_MS;

    /** Opacity a label moves in one frame of `dt` seconds. A snapped placement or a speed <= 0 does not fade. */
    inline float labelOpacityStep(float dt, float speed, bool snap) {
        return (snap || speed <= 0.0f) ? 1.0f : dt * speed;
    }

    /** A label covered by 3D content hides this many times faster than a fade step, one clearing shows this many times slower. */
    inline constexpr float LABEL_OCCLUSION_FALL_SPEED = 4.0f;
    inline constexpr float LABEL_OCCLUSION_RISE_SPEED = 0.5f;

    /**
     * Moves a label's occlusion factor toward a frame's verdict. A label whose occlusion was never judged, or not
     * since it last stood hidden, takes the verdict outright: easing it from "clear" drew a covered label for a
     * few frames as it faded in. Then it hides fast and shows slowly, so a verdict that flips for a frame or two
     * while building tiles load (an unresolved base blocks nothing) cannot flash a label.
     */
    inline float easeLabelOcclusion(float current, float target, float step, bool judged) {
        if (!judged || step >= 1.0f) {
            return target;
        }
        if (target > current) {
            return std::min(target, current + step * LABEL_OCCLUSION_RISE_SPEED);
        }
        return std::max(target, current - step * LABEL_OCCLUSION_FALL_SPEED);
    }

}

#endif
