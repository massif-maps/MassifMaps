/*
 * Tests for the shadow map's rule during the 2D/3D ramp (all/native/terrain/ShadowRamp.h): building
 * shadows stay through it, re-cast per STEP of the exaggeration, and the ground's own shadows wait
 * for the settled relief.
 *
 * NOT covered here: the cast itself and how the shadow reads on the moving ground, which live in
 * MapRenderer::applyShadows and are device checks.
 */

#include "terrain/ShadowRamp.h"

using namespace massif;

#include "TestCheck.h"

namespace {

    void testTheRampScalesTheExaggeration() {
        TEST_CHECK(ShadowRamp::exaggeration(2.0f, 1.0f) == 0.0f, "flat is no relief");
        TEST_CHECK(ShadowRamp::exaggeration(2.0f, 0.0f) == 2.0f, "3D is the configured relief");
        TEST_CHECK(ShadowRamp::exaggeration(2.0f, 0.25f) == 1.5f, "a ramp step is the share left");
    }

    void testTheGroundCastsOnlyOnceSettled() {
        TEST_CHECK(ShadowRamp::castsGround(0.0f), "settled 3D casts the ground");
        TEST_CHECK(!ShadowRamp::castsGround(0.01f) && !ShadowRamp::castsGround(1.0f), "a ramp or a flat map does not");
    }

    void testARampIsRationed() {
        TEST_CHECK(!ShadowRamp::needsCast(1.0f, 0.99f, 0.0f), "a hundredth of the relief is not a new cast");
        TEST_CHECK(ShadowRamp::needsCast(1.0f, 0.9f, 0.0f), "a tenth is");
        TEST_CHECK(!ShadowRamp::needsCast(1.0f, 0.5f, 0.5f), "the cast value needs no recast");
    }

    void testASettledValueIsExact() {
        TEST_CHECK(ShadowRamp::needsCast(1.0f, 0.0f, 0.99f), "the ramp ending recasts for the full relief");
        TEST_CHECK(ShadowRamp::needsCast(1.0f, 1.0f, 0.02f), "the ramp ending flat recasts for none");
        TEST_CHECK(ShadowRamp::needsCast(1.01f, 0.0f, 1.0f), "an app changing the exaggeration recasts");
        TEST_CHECK(!ShadowRamp::needsCast(1.0f, 1.0f, 0.0f), "a settled flat map has nothing to recast");
    }

}

void testShadowRamp() {
    testTheRampScalesTheExaggeration();
    testTheGroundCastsOnlyOnceSettled();
    testARampIsRationed();
    testASettledValueIsExact();
}
