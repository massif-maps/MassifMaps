/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DAYCYCLELIGHT_H_
#define _MASSIF_DAYCYCLELIGHT_H_

#include <algorithm>
#include <cmath>

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

        static float smoothStep(float edge0, float edge1, float x) {
            float t = std::max(0.0f, std::min(1.0f, (x - edge0) / (edge1 - edge0)));
            return t * t * (3.0f - 2.0f * t);
        }

        /**
         * The light the sun's own height implies, blended between the four setups above.
         *
         * The presets are artistic, not physical, but each is anchored on a sun height: day states
         * it 70 degrees up, dawn 40, dusk 10, and night puts it away. Reading the height back gives
         * a CONTINUOUS curve through them, so an hour of 12 lands on `day` and 19 on `dusk` without
         * a style stating either. Mixed in LINEAR space, or the midpoints go muddy.
         *
         * `rising` picks dawn over dusk at the same height; nothing else distinguishes them.
         */
        static Setup atSunHeight(float altitudeDegrees, bool rising) {
            const Setup& twilight = rising ? DAWN : DUSK;
            float day = smoothStep(12.0f, 38.0f, altitudeDegrees);
            float night = 1.0f - smoothStep(-9.0f, 3.0f, altitudeDegrees);
            float dusk = std::max(0.0f, 1.0f - day - night);
            Setup out = { { 0, 0, 0 }, 0, { 0, 0, 0 }, 0 };
            auto mixIn = [&out](const Setup& light, float weight) {
                if (weight <= 0) {
                    return;
                }
                for (int i = 0; i < 3; i++) {
                    out.ambient[i] += std::pow(light.ambient[i], 2.2f) * weight;
                    out.direct[i] += std::pow(light.direct[i], 2.2f) * weight;
                }
                out.ambientIntensity += light.ambientIntensity * weight;
                out.directIntensity += light.directIntensity * weight;
            };
            mixIn(DAY, day);
            mixIn(twilight, dusk);
            mixIn(NIGHT, night);
            for (int i = 0; i < 3; i++) {
                out.ambient[i] = std::pow(out.ambient[i], 1.0f / 2.2f);
                out.direct[i] = std::pow(out.direct[i], 1.0f / 2.2f);
            }
            return out;
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
