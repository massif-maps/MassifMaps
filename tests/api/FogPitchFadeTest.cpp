/*
 * How much fog a camera at a given tilt sees (components/FogPitchFade.h).
 *
 * The case this exists for: the fog range is measured in multiples of the camera-to-focus distance,
 * so a top-down map fogged its own ground at a few kilometres out with no distance in the frame to
 * justify it. Mapbox fades the fog out below pitch 45 entirely (src/style/fog_helpers.ts), and this
 * is that rule with their pitch read off our tilt.
 *
 * NOT covered here: resolveFog applying it to the colour's alpha - StyleEnvironment.cpp pulls in too
 * much for the host suite. The device check is the 2D state of the 2D/3D example, which must show no
 * haze at all.
 */

#include "components/FogPitchFade.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif;

void testFogPitchFade() {
    // tilt 90 is straight down in this SDK, which is mapbox's pitch 0.
    TEST_CHECK(fogPitchOpacity(90.0f) == 0.0f, "a top-down map has no fog");
    TEST_CHECK(fogPitchOpacity(45.0f) == 0.0f, "and neither has one tilted to mapbox's pitch 45");
    TEST_CHECK(fogPitchOpacity(25.0f) == 1.0f, "past pitch 65 the fog is all there");
    TEST_CHECK(fogPitchOpacity(0.0f) == 1.0f, "and it stays there down to the horizon");

    // Halfway through the band is mapbox's smoothstep, not a straight line: x*x*(3-2*x) at 0.5.
    TEST_CHECK(std::abs(fogPitchOpacity(35.0f) - 0.5f) < 1e-5f, "the middle of the band is half the fog");
    TEST_CHECK(fogPitchOpacity(40.0f) < 0.5f * fogPitchOpacity(30.0f), "and the ends are eased, not linear");

    // Monotone across the whole tilt range: tilting further down never brings fog back.
    float previous = fogPitchOpacity(0.0f);
    for (float tilt = 0.0f; tilt <= 90.0f; tilt += 1.0f) {
        float opacity = fogPitchOpacity(tilt);
        if (opacity > previous || opacity < 0.0f || opacity > 1.0f) {
            TEST_CHECK(false, "the fade is monotone in tilt and stays within 0..1");
            return;
        }
        previous = opacity;
    }
    TEST_CHECK(true, "the fade is monotone in tilt and stays within 0..1");

    // Out-of-range tilts are clamped rather than extrapolated - the 2D/3D switch drives tilt through
    // an animation, and one frame overshooting 90 must not make the fog negative.
    TEST_CHECK(fogPitchOpacity(95.0f) == 0.0f, "a tilt past top-down is still no fog");
    TEST_CHECK(fogPitchOpacity(-5.0f) == 1.0f, "and one past the horizon is still all of it");
}
