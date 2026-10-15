/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_LABELFADE_H_
#define _MASSIF_VT_LABELFADE_H_

#include <algorithm>
#include <cmath>

namespace massif::vt {

    /** maplibre's fadeDuration; the placement worker waits it out between passes. See docs/internals/rendering/06-labels.mdx. */
    inline constexpr int LABEL_FADE_DURATION_MS = 300;

    /** Default label blending speed, in full fades per second. */
    inline constexpr float DEFAULT_LABEL_BLENDING_SPEED = 1000.0f / LABEL_FADE_DURATION_MS;

    /** Opacity a label moves in one frame of `dt` seconds. A snapped placement or a speed <= 0 does not fade. */
    inline float labelOpacityStep(float dt, float speed, bool snap) {
        return (snap || speed <= 0.0f) ? 1.0f : dt * speed;
    }

    /** Rates, in 1/fade-steps, of the low-pass a label's occlusion follows its verdict with: a covered label hides about
     *  100 ms (time constant) after the verdict, one clearing shows about 330 ms after it. */
    inline constexpr float LABEL_OCCLUSION_FALL_RATE = 3.0f;
    inline constexpr float LABEL_OCCLUSION_RISE_RATE = 0.9f;
    /** Frames an occlusion judgement stays current, and how often a hidden label's is renewed, at most this many a frame. */
    inline constexpr unsigned int LABEL_OCCLUSION_STALE_FRAMES = 60;
    inline constexpr unsigned int LABEL_OCCLUSION_REJUDGE_FRAMES = 30;
    inline constexpr int LABEL_OCCLUSION_JUDGE_BUDGET = 96;

    /** Verdict noise below this fraction of the label is ignored. */
    inline constexpr float LABEL_OCCLUSION_DEAD_BAND = 0.15f;

    /**
     * The verdict (fraction of the label in the clear) a label's occlusion follows. Looking around moves the rays by
     * a pixel and the occluder set by a tile, so the fraction wobbles by a tap or two: a change under the dead band
     * is ignored, while fully hidden or fully clear is always taken. A label not judged yet takes it outright.
     */
    inline float holdLabelOcclusionVerdict(float held, float verdict, bool judged) {
        if (!judged || verdict <= 0.0f || verdict >= 1.0f) {
            return verdict;
        }
        return std::abs(verdict - held) >= LABEL_OCCLUSION_DEAD_BAND ? verdict : held;
    }

    /**
     * Moves a label's occlusion factor toward its target, a first-order low-pass that hides faster than it shows.
     * A label whose occlusion was never judged, or not since it last stood hidden, takes the target outright: easing
     * it from "clear" drew a covered label for a few frames as it faded in. The low-pass keeps a verdict that flips
     * for a frame or two (an unresolved base blocks nothing, an occluder tile swaps) from flashing or pulsing it.
     */
    inline float easeLabelOcclusion(float current, float target, float step, bool judged) {
        if (!judged || step >= 1.0f) {
            return target;
        }
        float rate = target > current ? LABEL_OCCLUSION_RISE_RATE : LABEL_OCCLUSION_FALL_RATE;
        float next = current + (target - current) * (1.0f - std::exp(-step * rate));
        return std::abs(target - next) < 0.02f ? target : next;
    }

}

#endif
