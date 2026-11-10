/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_DRAPELIGHTTERM_H_
#define _MASSIF_VT_DRAPELIGHTTERM_H_

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace massif::vt {
    /**
     * The map background's light in a drape fingerprint: the factor renderTileBackground applies,
     * emissive + (1 - emissive) * radiance, so an emissive moving under a neutral radiance (factor 1) re-bakes nothing.
     */
    struct DrapeLightTerm final {
        /**
         * The factor of one colour channel in `steps` units. Rounded: a factor of exactly 1 may arrive as 0.99999994.
         */
        static std::size_t backgroundStep(float emissive, float radiance, float steps) {
            float e = std::max(0.0f, std::min(1.0f, emissive));
            float r = std::max(0.0f, std::min(1.0f, radiance));
            return static_cast<std::size_t>(std::lround((e + (1.0f - e) * r) * steps));
        }
    };
}

#endif
