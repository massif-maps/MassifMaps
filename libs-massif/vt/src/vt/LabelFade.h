/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_LABELFADE_H_
#define _MASSIF_VT_LABELFADE_H_

namespace massif::vt {

    /** maplibre's fadeDuration; the placement worker waits it out between passes. See docs/internals/rendering/06-labels.mdx. */
    inline constexpr int LABEL_FADE_DURATION_MS = 300;

    /** Default label blending speed, in full fades per second. */
    inline constexpr float DEFAULT_LABEL_BLENDING_SPEED = 1000.0f / LABEL_FADE_DURATION_MS;

    /** Opacity a label moves in one frame of `dt` seconds. A snapped placement or a speed <= 0 does not fade. */
    inline float labelOpacityStep(float dt, float speed, bool snap) {
        return (snap || speed <= 0.0f) ? 1.0f : dt * speed;
    }

}

#endif
