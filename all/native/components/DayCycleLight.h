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
#include <vector>

namespace massif {

    /**
     * The light an hour implies, and what it does to flat ground; dependency-free so it runs in host tests.
     * See resolveLighting in StyleEnvironment.cpp and docs/internals/rendering/08-lighting-sky-fog.md.
     */
    struct DayCycleLight {
        /** One of MapBox Standard's light setups, as its `lights` block states them. sRGB 0-1. */
        struct Setup {
            float ambient[3];
            float ambientIntensity;
            float direct[3];
            float directIntensity;
        };

        // Standard's values: dawn hsl(28,98%,93%)/hsl(33,98%,77%), day white/white, dusk hsl(228,27%,29%)/hsl(30,98%,76%).
        static constexpr Setup DAY   = { { 1.0000f, 1.0000f, 1.0000f }, 0.80f, { 1.0000f, 1.0000f, 1.0000f }, 0.20f };
        static constexpr Setup DAWN  = { { 0.9986f, 0.9254f, 0.8614f }, 0.75f, { 0.9954f, 0.7925f, 0.5446f }, 0.50f };
        static constexpr Setup DUSK  = { { 0.2117f, 0.2430f, 0.3683f }, 0.80f, { 0.9952f, 0.7600f, 0.5248f }, 0.20f };
        // Not Standard's hsl(217,100%,11%): its night keeps a moon light 30 deg up that a real sun
        // direction drops, so the moon is folded into the ambient to match what Standard renders.
        static constexpr Setup NIGHT = { { 0.2745f, 0.3010f, 0.4115f }, 0.50f, { 0.2465f, 0.2683f, 0.3335f }, 0.50f };

        // Standard's night directional light, [270, 20]: a moon in the west, 70 deg up. Buildings are lit
        // by it once the sun is down, over Standard's own night ambient hsl(217,100%,11%) - the ground
        // keeps NIGHT's folded ambient. Without it every face of a building took the same tone.
        static constexpr float MOON_DIR[3] = { -0.3420f, 0.0f, 0.9397f };
        static constexpr float NIGHT_BUILDING_AMBIENT[3] = { 0.0f, 0.0843f, 0.22f };

        /** How far the buildings have gone over to the moon: 0 with the sun up, 1 from 8.6 deg under. */
        static float moonWeight(float sunUp) {
            return smoothStep(0.0f, 0.15f, -sunUp);
        }

        /** One light anchored on a sun height. A list of these is the whole curve. */
        struct Stop {
            float altitude;
            Setup light;
        };

        // MapBox Standard's curve. The doubled twilight stop holds the preset flat from 3 to 12 degrees.
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
         * The light a sun height implies, from stops sorted by altitude: clamped at the ends, smoothstep
         * between, mixed in linear colour space (sRGB midpoints go muddy).
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
         * The built-in curve, anchored where the sun really is at each preset's hour, not at Standard's
         * stated light directions. `rising` picks dawn over dusk at the same height.
         */
        static Setup atSunHeight(float altitudeDegrees, bool rising) {
            const Stop* curve = rising ? DAWN_CURVE : DUSK_CURVE;
            return atSunHeight(curve, 4, altitudeDegrees);
        }

        static bool sameLight(const Setup& a, const Setup& b) {
            for (int i = 0; i < 3; i++) {
                if (a.ambient[i] != b.ambient[i] || a.direct[i] != b.direct[i]) {
                    return false;
                }
            }
            return a.ambientIntensity == b.ambientIntensity && a.directIntensity == b.directIntensity;
        }

        /**
         * The curve `view::brightness` reads: a hold ramps towards the next light instead, so a palette ramped
         * over 0.25-0.3 flips inside the hold (near the horizon), not where the hold ends (mid-afternoon).
         */
        static std::vector<Stop> brightnessCurve(const Stop* stops, std::size_t count) {
            std::vector<Stop> curve;
            for (std::size_t i = 0; i < count; i++) {
                std::size_t end = i;
                while (end + 1 < count && sameLight(stops[end + 1].light, stops[i].light)) {
                    end++;
                }
                curve.push_back(stops[i]);
                if (end > i && end + 1 < count) {
                    curve.push_back({ stops[end].altitude, stops[end + 1].light });
                } else if (end > i) {
                    curve.push_back(stops[end]);
                }
                i = end;
            }
            return curve;
        }

        static float brightnessAtSunHeight(const Stop* stops, std::size_t count, float altitudeDegrees) {
            std::vector<Stop> curve = brightnessCurve(stops, count);
            float sunUp = std::sin(altitudeDegrees * (3.14159265358979323846f / 180.0f));
            return brightness(atSunHeight(curve.data(), curve.size(), altitudeDegrees), sunUp);
        }

        /**
         * mapbox's `calculateLightsBrightness` (3d-style/style/style.ts), read by `["measure-light", "brightness"]`.
         * `sunUp` is the sun direction's z. Must land on their day 0.478 / dawn 0.396 / dusk 0.027 / night 0.014.
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
         * The direct share of the light, all a shadow can remove: complement of mapbox's
         * `calculateGroundShadowFactor` (3d-style/render/shadow_utils.ts), as one scalar.
         * `sunUp` is the sun direction's z, so this is 0 below the horizon.
         */
        static float directShare(const Setup& light, float sunUp) {
            auto luminance = [](const float channels[3], float intensity) {
                float linear[3];
                for (int i = 0; i < 3; i++) {
                    linear[i] = std::pow(std::max(0.0f, std::min(1.0f, channels[i])), 2.2f);
                }
                return (0.2126f * linear[0] + 0.7152f * linear[1] + 0.0722f * linear[2]) * std::max(0.0f, intensity);
            };
            float ambient = luminance(light.ambient, light.ambientIntensity);
            float direct = luminance(light.direct, light.directIntensity) * std::max(0.0f, sunUp);
            return ambient + direct > 0.0f ? direct / (ambient + direct) : 0.0f;
        }

        // The strength the shaders take: they darken sRGB colours, and the share is linear light.
        // gl-js's linearTosRGB(ambient / (ambient + direct)) for a fully shadowed ground.
        static float srgbShadowStrength(const Setup& light, float sunUp, float strength) {
            float linear = std::min(1.0f, strength * directShare(light, sunUp));
            return 1.0f - std::pow(1.0f - linear, 1.0f / 2.2f);
        }

        /**
         * mapbox's `calculateGroundRadiance` (3d-style/render/lights.ts) for an upward-facing surface.
         * `sunUp` is the sun direction's z. Returned in sRGB: the sum is linear but multiplies an sRGB colour.
         */
        static void groundRadiance(const Setup& light, float sunUp, float radiance[3]) {
            float ambient[3], direct[3];
            for (int i = 0; i < 3; i++) {
                ambient[i] = std::pow(light.ambient[i], 2.2f) * light.ambientIntensity;
                direct[i] = std::pow(light.direct[i], 2.2f) * light.directIntensity;
            }
            // Sky is brighter near the sun; for a ground normal this is 1 whenever the sun is up.
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
