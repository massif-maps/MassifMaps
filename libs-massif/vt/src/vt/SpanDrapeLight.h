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
     * What the GROUND's light does to one drape texel on a FLAT, up-facing surface - which is what
     * a bridge deck is. The deck's roof shows the span drape, and a draped pixel is a finished
     * ground pixel: it has to take the ground's light, not the extrusion's facade model, or a road
     * and the deck carrying it disagree at every hour but noon.
     *
     * Resolved on the CPU because the deck is horizontal: the ground's per-fragment N.L collapses
     * to the sun's own height, leaving one value for the frame instead of a term in polygon3DFsh.
     */
    struct SpanDrapeLight final {
        /**
         * backgroundFsh's own expression, kept verbatim: the ambient is the FLOOR and the sun
         * fills what is left of the headroom, so a surface facing the sun lands at 1 rather than
         * at ambient + 1.
         *
         * A `colors-prelit` style arrives as white ambient at full intensity with no sun (see
         * TileRenderer::buildTerrainLighting), which makes this exactly 1 - those colours already
         * carry their light, and only the shadow is left for the shader to apply.
         *
         * @param enabled whether the ground is lit at all; with it off the surface samples its
         *                drape untouched (no TERRAIN_LIGHT in backgroundFsh), and so must the deck
         * @param sunDir east, north, up - only the UP component reaches a flat deck
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
