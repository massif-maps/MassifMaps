/*
 * The map background's light in a drape fingerprint (vt/DrapeLightTerm.h).
 *
 * Why it exists: the style's background emissive follows the zoom, and the fingerprint used to take
 * the emissive itself. Under a neutral radiance (every converted style's ground) it multiplies the
 * background by exactly 1, yet each 1/16 step it crossed re-baked the whole cover: four times in a
 * 4.4 km Crosscall pan, ~50 of 241 bakes producing the picture already there.
 *
 * NOT covered: that the baked background colour matches (renderTileBackground, GL).
 */

#include "DrapeLightTerm.h"

#include "TestCheck.h"

using namespace massif::vt;

namespace {
    const float STEPS = 16.0f;

    void testNeutralRadianceIgnoresTheEmissive() {
        std::size_t reference = DrapeLightTerm::backgroundStep(1.0f, 1.0f, STEPS);
        for (float emissive : { 0.0f, 0.187f, 0.188f, 0.249f, 0.25f, 0.9f }) {
            TEST_CHECK(DrapeLightTerm::backgroundStep(emissive, 1.0f, STEPS) == reference, "radiance 1 applies a factor of 1 whatever the emissive");
        }
    }

    void testAFactorOfOneSurvivesFloatError() {
        // 0.249 + 0.751 * 1 does not land on 1.0 in float; a floor would put it a step down.
        TEST_CHECK(DrapeLightTerm::backgroundStep(0.249f, 0.9999999f, STEPS) == 16, "a factor within float error of 1 is still step 16");
    }

    void testEmissiveOneAppliesNothing() {
        TEST_CHECK(DrapeLightTerm::backgroundStep(1.0f, 0.2f, STEPS) == 16, "emissive 1 leaves the background untouched whatever the radiance");
    }

    void testADarkRadianceStillFollowsTheEmissive() {
        std::size_t dim = DrapeLightTerm::backgroundStep(0.25f, 0.2f, STEPS);
        std::size_t bright = DrapeLightTerm::backgroundStep(0.75f, 0.2f, STEPS);
        TEST_CHECK(dim == 6 && bright == 13, "under a dark radiance the factor (0.4, 0.8) follows the emissive");
        TEST_CHECK(dim != bright, "a visible change of the applied factor re-bakes");
    }

    void testOutOfRangeInputsAreClamped() {
        TEST_CHECK(DrapeLightTerm::backgroundStep(-1.0f, 2.0f, STEPS) == 16, "emissive below 0 clamps to 0, radiance above 1 to 1");
        TEST_CHECK(DrapeLightTerm::backgroundStep(2.0f, 0.0f, STEPS) == 16, "emissive above 1 clamps to 1");
    }
}

void testDrapeLightTerm() {
    testNeutralRadianceIgnoresTheEmissive();
    testAFactorOfOneSurvivesFloatError();
    testEmissiveOneAppliesNothing();
    testADarkRadianceStillFollowsTheEmissive();
    testOutOfRangeInputsAreClamped();
}
