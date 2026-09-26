// How much fog a camera at a given tilt sees (components/FogPitchFade.h), mapbox's pitch fade
// (src/style/fog_helpers.ts). Not covered: resolveFog applying it to alpha.

#include "components/FogPitchFade.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif;

void testFogPitchFade() {
    // tilt 90 is mapbox's pitch 0.
    TEST_CHECK(fogPitchOpacity(90.0f) == 0.0f, "a top-down map has no fog");
    TEST_CHECK(fogPitchOpacity(45.0f) == 0.0f, "and neither has one tilted to mapbox's pitch 45");
    TEST_CHECK(fogPitchOpacity(25.0f) == 1.0f, "past pitch 65 the fog is all there");
    TEST_CHECK(fogPitchOpacity(0.0f) == 1.0f, "and it stays there down to the horizon");

    // mapbox's smoothstep: x*x*(3-2*x).
    TEST_CHECK(std::abs(fogPitchOpacity(35.0f) - 0.5f) < 1e-5f, "the middle of the band is half the fog");
    TEST_CHECK(fogPitchOpacity(40.0f) < 0.5f * fogPitchOpacity(30.0f), "and the ends are eased, not linear");

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

    // The 2D/3D switch animation can overshoot tilt 90 for a frame.
    TEST_CHECK(fogPitchOpacity(95.0f) == 0.0f, "a tilt past top-down is still no fog");
    TEST_CHECK(fogPitchOpacity(-5.0f) == 1.0f, "and one past the horizon is still all of it");
}
