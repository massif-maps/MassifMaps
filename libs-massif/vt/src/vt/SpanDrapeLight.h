/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_SPANDRAPELIGHT_H_
#define _MASSIF_VT_SPANDRAPELIGHT_H_

#include <algorithm>
#include <cglib/vec.h>

namespace massif::vt {
    /**
     * The ground's light on a flat, up-facing drape texel (a bridge deck roof), so a road and its deck agree
     * at every hour. Resolved on the CPU: for a horizontal surface N.L is just the sun's height.
     */
    struct SpanDrapeLight final {
        /**
         * backgroundFsh's expression verbatim: ambient is the floor and the sun fills the remaining
         * headroom, so a sun-facing surface lands at 1. A `colors-prelit` style resolves to exactly 1.
         *
         * @param enabled whether the ground is lit (TERRAIN_LIGHT); off returns 1, the drape untouched
         * @param sunDir east, north, up - only the up component reaches a flat deck
         */
        static cglib::vec3<float> resolve(bool enabled, const cglib::vec3<float>& sunDir, const cglib::vec3<float>& sunColor, float sunIntensity, const cglib::vec3<float>& ambientColor, float ambientIntensity) {
            if (!enabled) {
                return cglib::vec3<float>(1.0f, 1.0f, 1.0f);
            }
            float ndl = std::max(0.0f, sunDir(2));
            float sun = (1.0f - ambientIntensity) * ndl * sunIntensity;
            return ambientColor * ambientIntensity + sunColor * sun;
        }
    };
}

#endif
