/*
 * Tests for the hour-driven light (all/native/components/DayCycleLight.h): the curve through
 * MapBox Standard's four light setups, and mapbox's ground radiance on a flat, upward-facing
 * surface - the one number every 2D colour grade is a function of.
 *
 * NOT covered here: that resolveLighting really replaces the stated colours with these (it needs
 * LightOptions and TerrainOptions, past what tests/README.md allows the host link to carry), and
 * that the renderer really grades a colour by them. Both are device checks - see
 * docs/internals/rendering/08-lighting-sky-fog.md.
 */

#include "components/DayCycleLight.h"

#include <cmath>

using namespace massif;

#include "TestCheck.h"

namespace {

    bool nearly(float value, float expected, float tolerance = 0.002f) {
        return std::fabs(value - expected) <= tolerance;
    }

    float sunUp(float altitudeDegrees) {
        return static_cast<float>(std::sin(altitudeDegrees * 3.14159265358979323846 / 180.0));
    }

    void testTheAnchorsAreReachedExactly() {
        // Each preset is anchored on the sun height it states, and the curve has to land ON it
        // there - a blend at the anchor is what would make `day` no longer look like day.
        DayCycleLight::Setup day = DayCycleLight::atSunHeight(70.0f, false);
        TEST_CHECK(nearly(day.ambient[0], 1.0f) && nearly(day.ambient[2], 1.0f),
                   "high sun is Standard's white day ambient");
        TEST_CHECK(nearly(day.ambientIntensity, 0.80f) && nearly(day.directIntensity, 0.20f),
                   "with its intensities");

        DayCycleLight::Setup dusk = DayCycleLight::atSunHeight(10.0f, false);
        TEST_CHECK(nearly(dusk.ambient[0], DayCycleLight::DUSK.ambient[0]) &&
                   nearly(dusk.ambient[2], DayCycleLight::DUSK.ambient[2]),
                   "a sun 10 degrees up is dusk exactly, not a blend of it");

        DayCycleLight::Setup night = DayCycleLight::atSunHeight(-20.0f, false);
        TEST_CHECK(nearly(night.ambient[0], 0.0f) && nearly(night.ambient[2], 0.22f),
                   "a sun below the horizon is Standard's blue night ambient");
    }

    void testRisingPicksDawnOverDusk() {
        DayCycleLight::Setup dawn = DayCycleLight::atSunHeight(10.0f, true);
        DayCycleLight::Setup dusk = DayCycleLight::atSunHeight(10.0f, false);
        TEST_CHECK(dawn.ambient[2] > dusk.ambient[2],
                   "dawn's ambient is far brighter in blue than dusk's at the same height");
        TEST_CHECK(nearly(dawn.ambientIntensity, 0.75f) && nearly(dusk.ambientIntensity, 0.80f),
                   "and the two differ in intensity as Standard states them");
        // Nothing but the direction of travel separates them, which is why sunDir.x carries it.
        TEST_CHECK(!nearly(dawn.ambient[0], dusk.ambient[0], 0.1f),
                   "so the same sun height is two different lights depending on the way it moves");
    }

    void testTheCurveIsContinuous() {
        // The whole point of reading the height back: no step anywhere, or an animated day cycle
        // would jump between presets instead of passing through them.
        float previous = DayCycleLight::atSunHeight(-30.0f, false).ambient[2];
        for (float altitude = -29.0f; altitude <= 90.0f; altitude += 1.0f) {
            float current = DayCycleLight::atSunHeight(altitude, false).ambient[2];
            if (std::fabs(current - previous) > 0.06f) {
                TEST_CHECK(false, "the blue ambient moves smoothly across the whole sun range");
                return;
            }
            previous = current;
        }
        TEST_CHECK(true, "the blue ambient moves smoothly across the whole sun range");
    }

    void testGroundRadianceMatchesTheConverter() {
        // The same formula runs in the style converter (tools/style-cli emissive.ts,
        // groundRadiance), and a style pre-lit there has to land where the renderer lights one
        // live - otherwise the two modes draw different maps. These are that JS implementation's
        // output for the same inputs.
        float radiance[3];
        DayCycleLight::groundRadiance(DayCycleLight::DUSK, sunUp(10.0f), radiance);
        TEST_CHECK(nearly(radiance[0], 0.2797f) && nearly(radiance[1], 0.2667f) &&
                   nearly(radiance[2], 0.3467f),
                   "dusk with the sun 10 degrees up is the converter's own dusk radiance");
        // The regression this pins: the sum is LINEAR light multiplying an sRGB colour, so it has
        // to come back through sRGB. Left linear, dusk was 0.06 - five times too dark, which is
        // what made the water read as black.
        TEST_CHECK(radiance[0] > 0.2f, "and it is returned in sRGB, not left in linear space");

        DayCycleLight::groundRadiance(DayCycleLight::DAY, sunUp(70.0f), radiance);
        TEST_CHECK(nearly(radiance[0], 0.9945f) && nearly(radiance[1], 0.9945f) &&
                   nearly(radiance[2], 0.9945f),
                   "and a high white sun is very nearly no grade at all");
    }

    void testRadianceCarriesColourNotOnlyBrightness() {
        // A per-channel factor is what moves the hue: dusk removes blue from nothing and adds it,
        // which is how a night map goes cold rather than merely dim. A scalar brightness cannot.
        float dusk[3], night[3];
        DayCycleLight::groundRadiance(DayCycleLight::DUSK, sunUp(10.0f), dusk);
        DayCycleLight::groundRadiance(DayCycleLight::NIGHT, sunUp(-10.0f), night);
        TEST_CHECK(dusk[2] > dusk[0] && dusk[0] > dusk[1],
                   "dusk's radiance is bluest and least green, as its ambient is");
        TEST_CHECK(night[2] > night[0] * 2.0f,
                   "and night's is overwhelmingly blue");
        TEST_CHECK(night[0] < dusk[0], "night is darker than dusk in every channel");
    }

    void testTheSunBelowTheHorizonAddsNothing() {
        // max(0, sunUp): a sun under the ground must not subtract light, and its ambient still
        // lights the scene - this is what keeps a night map visible rather than black.
        float below[3], atHorizon[3];
        DayCycleLight::groundRadiance(DayCycleLight::NIGHT, sunUp(-40.0f), below);
        DayCycleLight::groundRadiance(DayCycleLight::NIGHT, sunUp(0.0f), atHorizon);
        TEST_CHECK(below[2] > 0.0f, "a sun well below the horizon still leaves the ambient");
        TEST_CHECK(nearly(below[2], atHorizon[2], 0.05f),
                   "and going further under it changes almost nothing, because the direct term is clamped");
    }

}

void testDayCycleLight() {
    testTheAnchorsAreReachedExactly();
    testRisingPicksDawnOverDusk();
    testTheCurveIsContinuous();
    testGroundRadianceMatchesTheConverter();
    testRadianceCarriesColourNotOnlyBrightness();
    testTheSunBelowTheHorizonAddsNothing();
}
