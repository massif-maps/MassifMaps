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

        // NOT Standard's stated ambient. Their night keeps a directional light 30 degrees up
        // whatever the hour; a day cycle has the sun under the ground and drops it, so the moon's
        // half is folded into the ambient instead. What is pinned is the RESULT - see below.
        DayCycleLight::Setup night = DayCycleLight::atSunHeight(-20.0f, false);
        TEST_CHECK(night.ambient[2] > night.ambient[0] && night.ambient[0] > 0.0f,
                   "a sun below the horizon is a blue night ambient with a little of every channel");
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

        // The night ground, which is where the two models diverge and had to be brought back:
        // mapbox-gl draws it at (0.20, 0.22, 0.30). Ours reached (0.00, 0.06, 0.16) while the
        // NIGHT ambient was Standard's own, because their below-horizon moon is dropped by a real
        // day cycle. This is the number the fold was chosen to land on.
        DayCycleLight::groundRadiance(DayCycleLight::NIGHT, sunUp(-20.0f), radiance);
        TEST_CHECK(nearly(radiance[0], 0.20f, 0.01f) && nearly(radiance[1], 0.22f, 0.01f) &&
                   nearly(radiance[2], 0.30f, 0.01f),
                   "and night lands on mapbox's own night ground radiance");
    }

    void testRadianceCarriesColourNotOnlyBrightness() {
        // A per-channel factor is what moves the hue: dusk removes blue from nothing and adds it,
        // which is how a night map goes cold rather than merely dim. A scalar brightness cannot.
        float dusk[3], night[3];
        DayCycleLight::groundRadiance(DayCycleLight::DUSK, sunUp(10.0f), dusk);
        DayCycleLight::groundRadiance(DayCycleLight::NIGHT, sunUp(-10.0f), night);
        TEST_CHECK(dusk[2] > dusk[0] && dusk[0] > dusk[1],
                   "dusk's radiance is bluest and least green, as its ambient is");
        // Blue-LEANING, not overwhelmingly blue: mapbox's night ground is (0.20, 0.22, 0.30), a
        // cold grey rather than a blue wash. It read as a wash while the moon's half was missing.
        TEST_CHECK(night[2] > night[1] && night[1] > night[0],
                   "and night's leans blue, coldest in red");
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

    void testShadowsAreWorthTheDirectLightAndNoMore() {
        // A shadow can only take the direct light away, so its depth is that light's share:
        // mapbox's calculateGroundShadowFactor states the complement. With Standard's day preset -
        // ambient 0.8 against a directional 0.2, both white - a sun straight overhead makes the
        // share exactly the directional intensity.
        TEST_CHECK(nearly(DayCycleLight::directShare(DayCycleLight::DAY, 1.0f), 0.2f),
                   "a white 0.8/0.2 light at zenith leaves a fifth of itself to a shadow");

        // The bug this exists for: below the horizon there is no direct light, so there is no
        // shadow to cast, at any strength. Without it the shadow map was still drawn all night -
        // from a sun altitude floored at 15 degrees - and swung round with the azimuth.
        TEST_CHECK(DayCycleLight::directShare(DayCycleLight::NIGHT, sunUp(-34.0f)) == 0.0f,
                   "a sun 34 degrees under the map casts no shadow");
        TEST_CHECK(DayCycleLight::directShare(DayCycleLight::DAY, sunUp(0.0f)) == 0.0f,
                   "and neither does one exactly on the horizon");

        // Monotone in the sun's height for one light, which is what makes the fade a fade rather
        // than a switch: the shadows thin out over the last degrees instead of vanishing at once.
        float previous = -1.0f;
        for (float altitude = 0.0f; altitude <= 60.0f; altitude += 5.0f) {
            float share = DayCycleLight::directShare(DayCycleLight::DAY, sunUp(altitude));
            if (share < previous) {
                TEST_CHECK(false, "the share only grows as the sun rises");
                return;
            }
            previous = share;
        }
        TEST_CHECK(true, "the share only grows as the sun rises");

        // Recorded because it looks like a bug and is not: Standard's dusk ambient is a DARK blue,
        // so its warm sun is a bigger share of a much smaller total than the white sun is at noon.
        // Dusk shadows are therefore relatively deeper than midday ones - mapbox's palette says so
        // - even though both are far below the strength an application sets.
        TEST_CHECK(DayCycleLight::directShare(DayCycleLight::DUSK, sunUp(10.0f)) >
                   DayCycleLight::directShare(DayCycleLight::DAY, sunUp(41.0f)),
                   "dusk's share beats midday's, because its ambient collapses faster than its sun");
    }

    void testBrightnessMatchesMapboxsOwnPresets() {
        // A Standard label's emissive is a ramp over ["measure-light", "brightness"] with stops at
        // 0.25 and 0.5, so our value has to land where mapbox's does or the ramp reads the wrong
        // end. These four are gl-js's own, from Style.calculateLightsBrightness.
        TEST_CHECK(nearly(DayCycleLight::brightness(DayCycleLight::DAY, sunUp(70.0f)), 0.4778f, 0.01f),
                   "day is mapbox's 0.478");
        TEST_CHECK(nearly(DayCycleLight::brightness(DayCycleLight::DAWN, sunUp(40.0f)), 0.3965f, 0.01f),
                   "dawn is mapbox's 0.396");
        TEST_CHECK(nearly(DayCycleLight::brightness(DayCycleLight::DUSK, sunUp(10.0f)), 0.0270f, 0.01f),
                   "dusk is mapbox's 0.027");
        // NOT night's 0.0135. Theirs is a light 30 degrees above the horizon whatever the hour -
        // an artistic moon - and a day cycle has the sun genuinely down, so the directional term
        // is 0 here. Both are far below the 0.25 stop every ramp starts at, so no ramp can tell
        // them apart; what matters is that night stays the darkest of the four.
        TEST_CHECK(DayCycleLight::brightness(DayCycleLight::NIGHT, sunUp(-20.0f)) <
                   DayCycleLight::brightness(DayCycleLight::DUSK, sunUp(10.0f)),
                   "night is darker than dusk, though not mapbox's own 0.014");
        // Which is the property the ramps depend on: day sits above their upper stop of 0.5's
        // neighbourhood and dusk far below the lower one of 0.25, so a label really does swing
        // from one end of its ramp to the other over a day.
        TEST_CHECK(DayCycleLight::brightness(DayCycleLight::DUSK, sunUp(10.0f)) < 0.25f &&
                   DayCycleLight::brightness(DayCycleLight::DAY, sunUp(70.0f)) > 0.25f,
                   "and the two straddle the 0.25 stop Standard's label ramps start at");
    }

    void testACustomCurveReplacesTheBuiltInOne() {
        // The built-in curve IS a stop list, so feeding its own stops back must reproduce it
        // exactly - that is what makes an app-supplied curve a replacement rather than a variant.
        for (float altitude = -30.0f; altitude <= 80.0f; altitude += 2.5f) {
            DayCycleLight::Setup builtIn = DayCycleLight::atSunHeight(altitude, false);
            DayCycleLight::Setup viaStops = DayCycleLight::atSunHeight(DayCycleLight::DUSK_CURVE, 4, altitude);
            if (!nearly(builtIn.ambient[2], viaStops.ambient[2], 0.0005f) ||
                !nearly(builtIn.directIntensity, viaStops.directIntensity, 0.0005f)) {
                TEST_CHECK(false, "the built-in curve read as stops is the built-in curve");
                return;
            }
        }
        TEST_CHECK(true, "the built-in curve read as stops is the built-in curve");

        // A curve of one stop is a fixed light at every hour - the simplest replacement there is.
        DayCycleLight::Stop flat[1] = { { 0.0f, DayCycleLight::NIGHT } };
        TEST_CHECK(DayCycleLight::atSunHeight(flat, 1, 80.0f).ambient[2] == DayCycleLight::NIGHT.ambient[2],
                   "a one-stop curve holds at every sun height");

        // And the curve HOLDS past its ends rather than extrapolating off the palette.
        DayCycleLight::Stop pair[2] = { { 0.0f, DayCycleLight::NIGHT }, { 10.0f, DayCycleLight::DAY } };
        TEST_CHECK(DayCycleLight::atSunHeight(pair, 2, -50.0f).ambient[2] == DayCycleLight::NIGHT.ambient[2] &&
                   DayCycleLight::atSunHeight(pair, 2, 90.0f).ambient[2] == DayCycleLight::DAY.ambient[2],
                   "and holds past both ends instead of extrapolating");
        // Midway is a blend of the two, not either of them.
        float mid = DayCycleLight::atSunHeight(pair, 2, 5.0f).ambientIntensity;
        TEST_CHECK(mid > DayCycleLight::NIGHT.ambientIntensity && mid < DayCycleLight::DAY.ambientIntensity,
                   "and interpolates between two stops");
    }

    /** The 8-bit spelling of a light channel - what a hand-written curve writes as #rrggbb. */
    int byteOf(float channel) {
        return static_cast<int>(std::max(0.0f, std::min(1.0f, channel)) * 255.0f + 0.5f);
    }

    void testAPresetIsReachedExactlyOrNotAtAll() {
        // What the `day-cycle-light` example depends on: an HOUR only reproduces a MapBox
        // lightPreset where the curve is FLAT - past its ends, or inside the doubled twilight stop.
        // Anywhere else it is a blend of two, which is why an arbitrary hour never matches a
        // lightPreset screenshot. These four altitudes are the example's own preset hours.
        DayCycleLight::Setup dawn = DayCycleLight::atSunHeight(DayCycleLight::DAWN_CURVE, 4, 6.5f);
        TEST_CHECK(dawn.ambient[0] == DayCycleLight::DAWN.ambient[0] &&
                   dawn.directIntensity == DayCycleLight::DAWN.directIntensity,
                   "a rising sun 6.5 degrees up is Standard's dawn exactly");
        DayCycleLight::Setup dusk = DayCycleLight::atSunHeight(DayCycleLight::DUSK_CURVE, 4, 7.1f);
        TEST_CHECK(dusk.ambient[0] == DayCycleLight::DUSK.ambient[0] &&
                   dusk.directIntensity == DayCycleLight::DUSK.directIntensity,
                   "a setting sun 7.1 degrees up is its dusk exactly");
        TEST_CHECK(DayCycleLight::atSunHeight(DayCycleLight::DUSK_CURVE, 4, 41.1f).ambientIntensity ==
                   DayCycleLight::DAY.ambientIntensity,
                   "41.1 degrees is past the 38 stop, so it is day exactly");
        TEST_CHECK(DayCycleLight::atSunHeight(DayCycleLight::DUSK_CURVE, 4, -33.9f).ambient[2] ==
                   DayCycleLight::NIGHT.ambient[2],
                   "and -33.9 is below the -9 stop, so it is night exactly");

        // The other half of the same claim: 30 degrees up is NOT a preset, whatever the hour.
        DayCycleLight::Setup between = DayCycleLight::atSunHeight(DayCycleLight::DUSK_CURVE, 4, 30.0f);
        TEST_CHECK(between.ambientIntensity != DayCycleLight::DAY.ambientIntensity ||
                   between.ambient[0] != DayCycleLight::DUSK.ambient[0],
                   "a sun 30 degrees up is a blend of dusk and day, not either of them");
    }

    void testTheSetupsSurviveBeingWrittenAsHex() {
        // An app-supplied curve carries its colours as 8-bit Colors, so a hand-written list can
        // only be byte-for-byte the built-in one if these are the bytes. The `day-cycle-light`
        // example spells exactly these out; a Setup edited without them is the drift this catches.
        TEST_CHECK(byteOf(DayCycleLight::DAWN.ambient[0]) == 0xff &&
                   byteOf(DayCycleLight::DAWN.ambient[1]) == 0xec &&
                   byteOf(DayCycleLight::DAWN.ambient[2]) == 0xdc,
                   "dawn's ambient is #ffecdc");
        TEST_CHECK(byteOf(DayCycleLight::DAWN.direct[0]) == 0xfe &&
                   byteOf(DayCycleLight::DAWN.direct[1]) == 0xca &&
                   byteOf(DayCycleLight::DAWN.direct[2]) == 0x8b,
                   "and its sun #feca8b");
        TEST_CHECK(byteOf(DayCycleLight::DUSK.ambient[0]) == 0x36 &&
                   byteOf(DayCycleLight::DUSK.ambient[2]) == 0x5e &&
                   byteOf(DayCycleLight::DUSK.direct[1]) == 0xc2,
                   "dusk is #363e5e over #fec286");
        TEST_CHECK(byteOf(DayCycleLight::NIGHT.ambient[0]) == 0x46 &&
                   byteOf(DayCycleLight::NIGHT.direct[2]) == 0x55,
                   "and night #464d69 over #3f4455");
    }

}

void testDayCycleLight() {
    testACustomCurveReplacesTheBuiltInOne();
    testAPresetIsReachedExactlyOrNotAtAll();
    testTheSetupsSurviveBeingWrittenAsHex();
    testBrightnessMatchesMapboxsOwnPresets();
    testTheAnchorsAreReachedExactly();
    testRisingPicksDawnOverDusk();
    testTheCurveIsContinuous();
    testGroundRadianceMatchesTheConverter();
    testRadianceCarriesColourNotOnlyBrightness();
    testTheSunBelowTheHorizonAddsNothing();
    testShadowsAreWorthTheDirectLightAndNoMore();
}
