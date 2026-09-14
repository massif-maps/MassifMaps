/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_FOGPITCHFADE_H_
#define _MASSIF_FOGPITCHFADE_H_

#include <algorithm>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

namespace massif {

    /**
     * How much of the fog a camera at this tilt sees: none looking straight down, all of it once
     * the view is 25 degrees off the horizon. Mapbox's smoothstep(45, 65, pitch)
     * (src/style/fog_helpers.ts), with their pitch measured from the vertical where our tilt is
     * measured from the horizontal. A top-down map has no distance to fog.
     *
     * Free of FogOptions so the host tests can reach it; applied in resolveFog.
     */
    inline float fogPitchOpacity(float tilt) {
        float pitch = 90.0f - tilt;
        float t = std::min(1.0f, std::max(0.0f, (pitch - 45.0f) / 20.0f));
        return t * t * (3.0f - 2.0f * t);
    }

#ifdef __ANDROID__
    /** Turns the fade off for an A/B without a rebuild: adb shell setprop debug.massif.fogpitch 0 */
    inline bool isFogPitchFadeEnabled() {
        static const bool enabled = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            return !(__system_property_get("debug.massif.fogpitch", property) > 0 && property[0] == '0');
        }();
        return enabled;
    }
#else
    inline bool isFogPitchFadeEnabled() {
        return true;
    }
#endif

}

#endif
