/*
 * Tests for the auto-flatten rule (all/native/terrain/AutoFlatten.h): the parallax arithmetic, the
 * two thresholds, the hysteresis that keeps a camera parked on one from oscillating, and the ramp.
 *
 * NOT covered here: everything that reads the rule. MapRenderer::updateTerrainFlatten needs the
 * renderer, and TerrainOptions::setFlattenRatio needs ElevationManager - both far past what
 * tests/README.md allows the host link to carry. That the ramp really scales the heights on the
 * GPU without a re-decode, that the terrain passes really drop at ratio 1, and that the switch is
 * invisible, are device checks. See docs/internals/rendering/04-terrain.md.
 */

#include "terrain/AutoFlatten.h"

#include <cmath>

using namespace massif;

#include "TestCheck.h"

namespace {

    bool nearly(double value, double expected) {
        return std::fabs(value - expected) < 1.0e-6 * std::max(1.0, std::fabs(expected));
    }

    void testParallax() {
        // 1300 px half-diagonal, a 4 km height range, a 40 km camera: 130 px.
        TEST_CHECK(nearly(AutoFlatten::parallax(1300, 4000, 40000), 130.0),
                   "the parallax is the height range scaled by the screen radius over the camera distance");
        // The same terrain from 15x further out is 15x less parallax - the whole point of the rule
        // being a ratio rather than a zoom threshold.
        TEST_CHECK(nearly(AutoFlatten::parallax(1300, 4000, 600000), 130.0 / 15.0),
                   "and it falls with the camera distance");
        TEST_CHECK(AutoFlatten::parallax(1300, 4000, 0) == 0,
                   "a zero camera distance does not divide by zero");
        TEST_CHECK(AutoFlatten::parallax(1300, 4000, -1) == 0,
                   "and neither does a negative one");
        TEST_CHECK(AutoFlatten::parallax(1300, 0, 40000) == 0,
                   "flat ground has no parallax at any distance");
    }

    void testThresholdsOff() {
        // Both thresholds 0 is the default, and has to mean 'never', not 'always': a parallax of 0
        // is below every threshold, so a rule that forgot this would flatten every map on frame one.
        TEST_CHECK(!AutoFlatten::shouldFlatten(0, 0, 90, 0, false),
                   "with both thresholds off nothing flattens, even at zero parallax and tilt 90");
        TEST_CHECK(!AutoFlatten::shouldFlatten(0, 0, 90, 0, true),
                   "and not even once it is already flattening");
    }

    void testParallaxThreshold() {
        TEST_CHECK(AutoFlatten::shouldFlatten(1.9, 2.0f, 0, 0, false), "under the parallax threshold flattens");
        TEST_CHECK(!AutoFlatten::shouldFlatten(2.1, 2.0f, 0, 0, false), "over it does not");
        TEST_CHECK(!AutoFlatten::shouldFlatten(2.0, 2.0f, 0, 0, false), "and the threshold itself is 3D");
    }

    void testTiltThreshold() {
        TEST_CHECK(AutoFlatten::shouldFlatten(1000, 0, 88.0f, 88.0f, false),
                   "at the tilt threshold it flattens whatever the parallax");
        TEST_CHECK(!AutoFlatten::shouldFlatten(1000, 0, 87.9f, 88.0f, false), "just below it does not");
        // The tilt half must not be masked by a large parallax, which is exactly the z13 top-down
        // case the threshold exists for.
        TEST_CHECK(AutoFlatten::shouldFlatten(130, 2.0f, 89.0f, 88.0f, false),
                   "and a large parallax does not veto the tilt rule");
    }

    void testHysteresis() {
        // Already flattening: hold until 1.5x the parallax threshold, so a camera drifting around
        // the boundary does not re-decode... nothing, but does not thrash the tile LOD either.
        TEST_CHECK(AutoFlatten::shouldFlatten(2.9, 2.0f, 0, 0, true),
                   "while flattened, a parallax just over the threshold stays flat");
        TEST_CHECK(!AutoFlatten::shouldFlatten(3.1, 2.0f, 0, 0, true),
                   "and 3D returns only past 1.5x it");
        TEST_CHECK(AutoFlatten::shouldFlatten(3.1, 2.0f, 0, 0, false) == false,
                   "coming the other way the plain threshold applies, so the two do not overlap");

        TEST_CHECK(AutoFlatten::shouldFlatten(1000, 0, 86.5f, 88.0f, true),
                   "while flattened, tilting back 1.5 degrees stays flat");
        TEST_CHECK(!AutoFlatten::shouldFlatten(1000, 0, 85.9f, 88.0f, true),
                   "and 3D returns 2 degrees below the threshold");
    }

    void testRamp() {
        // 0.3 s at 60 fps: 1/18 per frame, 18 frames to flat.
        float ratio = 0;
        int frames = 0;
        while (ratio < 1.0f && frames < 1000) {
            ratio = AutoFlatten::step(ratio, true, 1.0f / 60.0f, 0.3f);
            frames++;
        }
        TEST_CHECK(frames == 18, "a 0.3 s ramp at 60 fps reaches flat in 18 frames");
        TEST_CHECK(ratio == 1.0f, "and lands exactly on 1, not past it");

        frames = 0;
        while (ratio > 0.0f && frames < 1000) {
            ratio = AutoFlatten::step(ratio, false, 1.0f / 60.0f, 0.3f);
            frames++;
        }
        TEST_CHECK(frames == 18 && ratio == 0.0f, "and comes back to exactly 0 in as many");

        TEST_CHECK(AutoFlatten::step(0.0f, true, 1.0f / 60.0f, 0.0f) == 1.0f,
                   "a zero duration switches in one frame");
        TEST_CHECK(AutoFlatten::step(1.0f, true, 1.0f / 60.0f, 0.3f) == 1.0f,
                   "a ramp already at its target does not move, which is what stops the per-frame work");
        // A frame after a long stall must not overshoot, and a clock that went backwards must not
        // walk the ramp the wrong way.
        TEST_CHECK(AutoFlatten::step(0.5f, true, 10.0f, 0.3f) == 1.0f, "a huge delta clamps at the target");
        TEST_CHECK(AutoFlatten::step(0.5f, true, -1.0f, 0.3f) == 0.5f, "and a negative delta does not move it");
    }

}

void testAutoFlatten() {
    testParallax();
    testThresholdsOff();
    testParallaxThreshold();
    testTiltThreshold();
    testHysteresis();
    testRamp();
}
