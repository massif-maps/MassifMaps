/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DAYCYCLELIGHT_H_
#define _MASSIF_DAYCYCLELIGHT_H_

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace massif {

    /**
     * The light an hour implies, and what it does to flat ground. Free of Options, of Color and of
     * the renderer on purpose - this is the arithmetic, so it is the part worth testing on the host.
     * See resolveLighting in StyleEnvironment.cpp and
     * docs/internals/rendering/08-lighting-sky-fog.md.
     */
    struct DayCycleLight {
        /** One of MapBox Standard's light setups, as its `lights` block states them. sRGB 0-1. */
        struct Setup {
            float ambient[3];
            float ambientIntensity;
            float direct[3];
            float directIntensity;
        };

        // Standard's own values, verbatim. dawn hsl(28,98%,93%)/hsl(33,98%,77%), day white/white,
        // dusk hsl(228,27%,29%)/hsl(30,98%,76%), night hsl(217,100%,11%)/hsl(225,15%,29%).
        static constexpr Setup DAY   = { { 1.0000f, 1.0000f, 1.0000f }, 0.80f, { 1.0000f, 1.0000f, 1.0000f }, 0.20f };
        static constexpr Setup DAWN  = { { 0.9986f, 0.9254f, 0.8614f }, 0.75f, { 0.9954f, 0.7925f, 0.5446f }, 0.50f };
        static constexpr Setup DUSK  = { { 0.2117f, 0.2430f, 0.3683f }, 0.80f, { 0.9952f, 0.7600f, 0.5248f }, 0.20f };
        static constexpr Setup NIGHT = { { 0.0000f, 0.0766f, 0.2200f }, 0.50f, { 0.2465f, 0.2683f, 0.3335f }, 0.50f };

        /** One light anchored on a sun height. A list of these is the whole curve. */
        struct Stop {
            float altitude;
            Setup light;
        };

        // The built-in curve as stops - MapBox Standard's own, and the shape of any replacement.
        // The doubled twilight stop is what holds the preset flat from 3 to 12 degrees, so the sun
        // passes THROUGH dusk instead of crossing it.
        static constexpr Stop DUSK_CURVE[4] = { { -9.0f, NIGHT }, { 3.0f, DUSK }, { 12.0f, DUSK }, { 38.0f, DAY } };
        static constexpr Stop DAWN_CURVE[4] = { { -9.0f, NIGHT }, { 3.0f, DAWN }, { 12.0f, DAWN }, { 38.0f, DAY } };

        static float smoothStep(float edge0, float edge1, float x) {
            if (edge1 <= edge0) {
                return x < edge0 ? 0.0f : 1.0f;
            }
            float t = std::max(0.0f, std::min(1.0f, (x - edge0) / (edge1 - edge0)));
            return t * t * (3.0f - 2.0f * t);
        }

        /**
         * The light a given sun height implies, read off a curve of stops sorted by altitude.
         *
         * Below the first stop and above the last the curve holds; between two it smoothsteps, in
         * LINEAR colour space - mixed in sRGB the midpoints go muddy. The stops are what an app
         * replaces to change the whole map's palette at every hour: everything downstream (the 2D
         * grade, the 3D lighting, the brightness a style ramps over) is derived from the light this
         * returns, so one list drives all of it.
         */
        static Setup atSunHeight(const Stop* stops, std::size_t count, float altitudeDegrees) {
            if (count == 0) {
                return DAY;
            }
            if (count == 1 || altitudeDegrees <= stops[0].altitude) {
                return stops[0].light;
            }
            if (altitudeDegrees >= stops[count - 1].altitude) {
                return stops[count - 1].light;
            }
            std::size_t upper = 1;
            while (upper < count - 1 && stops[upper].altitude < altitudeDegrees) {
                upper++;
            }
            const Setup& from = stops[upper - 1].light;
            const Setup& to = stops[upper].light;
            float t = smoothStep(stops[upper - 1].altitude, stops[upper].altitude, altitudeDegrees);
            Setup out = { { 0, 0, 0 }, 0, { 0, 0, 0 }, 0 };
            for (int i = 0; i < 3; i++) {
                out.ambient[i] = std::pow(std::pow(from.ambient[i], 2.2f) * (1.0f - t) + std::pow(to.ambient[i], 2.2f) * t, 1.0f / 2.2f);
                out.direct[i] = std::pow(std::pow(from.direct[i], 2.2f) * (1.0f - t) + std::pow(to.direct[i], 2.2f) * t, 1.0f / 2.2f);
            }
            out.ambientIntensity = from.ambientIntensity * (1.0f - t) + to.ambientIntensity * t;
            out.directIntensity = from.directIntensity * (1.0f - t) + to.directIntensity * t;
            return out;
        }

        /**
         * The built-in curve: MapBox Standard's four light setups, anchored on the sun heights it
         * states them at - day 70 degrees up, dawn 40, dusk 10, night away. An hour of 12 then
         * lands on `day` and 19 on `dusk` without a style stating either.
         *
         * `rising` picks dawn over dusk at the same height; nothing else distinguishes them.
         */
        static Setup atSunHeight(float altitudeDegrees, bool rising) {
            const Stop* curve = rising ? DAWN_CURVE : DUSK_CURVE;
            return atSunHeight(curve, 4, altitudeDegrees);
        }

        /**
         * mapbox's `calculateLightsBrightness` (3d-style/style/style.ts), which is what their
         * `["measure-light", "brightness"]` reads: the mean of the two lights' relative luminance,
         * the directional one weighted by how high the sun is. `sunUp` is the sun direction's z.
         *
         * Their own values are day 0.478, dawn 0.396, dusk 0.027, night 0.014 - the numbers a
         * Standard label's emissive ramp is written against, so ours has to land on them.
         */
        static float brightness(const Setup& light, float sunUp) {
            // W3C relative luminance, which is NOT the 2.2 gamma the radiance uses.
            auto relativeLuminance = [](const float channels[3]) {
                float linear[3];
                for (int i = 0; i < 3; i++) {
                    float c = std::max(0.0f, std::min(1.0f, channels[i]));
                    linear[i] = c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
                }
                return 0.2126f * linear[0] + 0.7152f * linear[1] + 0.0722f * linear[2];
            };
            // mapbox weights by 1 - polar/90, and polar is 90 minus the sun's height.
            float height = std::asin(std::max(-1.0f, std::min(1.0f, sunUp))) * (180.0f / 3.14159265358979323846f);
            float directWeight = std::max(0.0f, std::min(1.0f, height / 90.0f));
            return (relativeLuminance(light.direct) * light.directIntensity * directWeight +
                    relativeLuminance(light.ambient) * light.ambientIntensity) / 2.0f;
        }

        /**
         * mapbox's `calculateGroundRadiance` (3d-style/render/lights.ts) with the ground normal:
         * what a light does to a flat, upward-facing surface. `sunUp` is the sun direction's z.
         *
         * Returned in sRGB, as their linearVec3TosRGB does - the sum is LINEAR light and it
         * multiplies an sRGB colour. Left linear it is five times too dark at dusk.
         */
        static void groundRadiance(const Setup& light, float sunUp, float radiance[3]) {
            float ambient[3], direct[3];
            for (int i = 0; i < 3; i++) {
                ambient[i] = std::pow(light.ambient[i], 2.2f) * light.ambientIntensity;
                direct[i] = std::pow(light.direct[i], 2.2f) * light.directIntensity;
            }
            // The sky is brighter near the sun; a ground normal never faces away, so this is 1
            // whenever the sun is up.
            float luminance = 0.2126f * direct[0] + 0.7152f * direct[1] + 0.0722f * direct[2];
            float minFactor = 1.0f - 0.3f * std::min(luminance, 1.0f);
            float ambientDirectional = minFactor + (1.0f - minFactor) * std::min(sunUp + 1.0f, 1.0f);
            float up = std::max(0.0f, sunUp);
            for (int i = 0; i < 3; i++) {
                radiance[i] = std::pow(std::max(0.0f, std::min(1.0f, ambient[i] * ambientDirectional + direct[i] * up)), 1.0f / 2.2f);
            }
        }
    };
}

#endif
