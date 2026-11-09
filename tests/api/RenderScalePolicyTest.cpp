/*
 * Tests for the render scale policy (all/native/ui/RenderScalePolicy.h): the clamp a bad value goes
 * through, which scale 2D and 3D ask for, the tilt gap, and what holds a switch back.
 *
 * NOT covered here: that the surface really is resized, that the DPI compensation keeps the same
 * ground, and what a scaled frame looks like. Those are device checks, see
 * docs/internals/rendering/21-render-scale.md.
 */

#include "ui/RenderScalePolicy.h"

using namespace massif;

#include "TestCheck.h"

namespace {

    using Policy = RenderScalePolicy;

    Policy::Config config(float scale2D, float scale3D) {
        Policy::Config result;
        result.scale2D = scale2D;
        result.scale3D = scale3D;
        return result;
    }

    Policy::Input input(float tilt, double now, bool terrain3D = false, bool gesture = false) {
        Policy::Input result;
        result.tilt = tilt;
        result.nowSeconds = now;
        result.terrain3D = terrain3D;
        result.gestureActive = gesture;
        return result;
    }

    void testClamp() {
        TEST_CHECK(Policy::clamp(0.75f) == 0.75f, "a scale in range is kept");
        TEST_CHECK(Policy::clamp(1.0f) == 1.0f, "1 is native");
        TEST_CHECK(Policy::clamp(2.0f) == 1.0f, "above 1 is native, never an upscale");
        TEST_CHECK(Policy::clamp(0.0f) == 1.0f, "0 is native, not an empty surface");
        TEST_CHECK(Policy::clamp(-0.5f) == 1.0f, "negative is native");
        TEST_CHECK(Policy::clamp(std::nanf("")) == 1.0f, "NaN is native");
        TEST_CHECK(Policy::clamp(0.01f) == Policy::MIN_SCALE, "a tiny scale stops at the floor");
    }

    void testFlatMapKeepsTheFlatScale() {
        Policy::Result result = Policy::step(Policy::State(), config(1.0f, 0.75f), input(90, 100));
        TEST_CHECK(result.scale == 1.0f && !result.state.mode3D && !result.pending, "straight down is 2D");
    }

    void testTiltEntersAndLeaves3D() {
        Policy::Config cfg = config(1.0f, 0.75f);
        Policy::Result tilted = Policy::step(Policy::State(), cfg, input(60, 100));
        TEST_CHECK(tilted.state.mode3D && tilted.scale == 0.75f, "a tilted view is 3D");
        Policy::Result back = Policy::step(tilted.state, cfg, input(90, 200));
        TEST_CHECK(!back.state.mode3D && back.scale == 1.0f, "straight down again is 2D once the hold is over");
    }

    void testTerrainIs3DAtAnyTilt() {
        Policy::Result result = Policy::step(Policy::State(), config(1.0f, 0.75f), input(90, 100, true));
        TEST_CHECK(result.state.mode3D && result.scale == 0.75f, "terrain that is not flattened is 3D even looking straight down");
    }

    void testTheTiltGap() {
        Policy::Config cfg = config(1.0f, 0.75f);
        Policy::State flat;
        TEST_CHECK(!Policy::step(flat, cfg, input(86, 100)).state.mode3D, "86 degrees is not enough to enter 3D");
        TEST_CHECK(Policy::step(flat, cfg, input(84, 100)).state.mode3D, "84 degrees enters it");
        Policy::State tilted;
        tilted.mode3D = true;
        TEST_CHECK(Policy::step(tilted, cfg, input(87, 100)).state.mode3D, "87 degrees stays in 3D, inside the gap");
        TEST_CHECK(!Policy::step(tilted, cfg, input(89, 100)).state.mode3D, "89 degrees leaves it");
    }

    void testNoSwitchMidGesture() {
        Policy::Config cfg = config(1.0f, 0.75f);
        Policy::Result held = Policy::step(Policy::State(), cfg, input(40, 100, false, true));
        TEST_CHECK(!held.state.mode3D && held.scale == 1.0f && held.pending, "a finger down holds the switch and asks for a frame");
        Policy::Result released = Policy::step(held.state, cfg, input(40, 100.1));
        TEST_CHECK(released.state.mode3D && released.scale == 0.75f && !released.pending, "it happens once the finger lifts");
    }

    void testHoldBetweenSwitches() {
        Policy::Config cfg = config(1.0f, 0.75f);
        Policy::Result in3D = Policy::step(Policy::State(), cfg, input(40, 100));
        Policy::Result tooSoon = Policy::step(in3D.state, cfg, input(90, 100.5));
        TEST_CHECK(tooSoon.state.mode3D && tooSoon.pending, "half a second after a switch the next one waits");
        Policy::Result later = Policy::step(tooSoon.state, cfg, input(90, 101.0));
        TEST_CHECK(!later.state.mode3D && !later.pending, "and goes through once the hold is over");
        Policy::Result flicker = Policy::step(later.state, cfg, input(40, 101.2));
        TEST_CHECK(!flicker.state.mode3D && flicker.pending, "a tilt right after leaving 3D waits too");
    }

    void testSameScaleNeverAsksForAChange() {
        Policy::Config cfg = config(1.0f, 1.0f);
        TEST_CHECK(Policy::step(Policy::State(), cfg, input(40, 100)).scale == 1.0f, "equal scales leave the surface alone");
        Policy::Config lowered = config(0.8f, 0.8f);
        TEST_CHECK(Policy::step(Policy::State(), lowered, input(90, 100)).scale == 0.8f, "a lowered 2D scale applies flat");
    }

    void testBadConfigIsNative() {
        Policy::Config cfg = config(0.0f, 7.0f);
        TEST_CHECK(Policy::step(Policy::State(), cfg, input(90, 100)).scale == 1.0f, "a bad 2D scale is native");
        TEST_CHECK(Policy::step(Policy::State(), cfg, input(40, 100)).scale == 1.0f, "a bad 3D scale is native");
    }

}

void testRenderScalePolicy() {
    testClamp();
    testFlatMapKeepsTheFlatScale();
    testTiltEntersAndLeaves3D();
    testTerrainIs3DAtAnyTilt();
    testTheTiltGap();
    testNoSwitchMidGesture();
    testHoldBetweenSwitches();
    testSameScaleNeverAsksForAChange();
    testBadConfigIsNative();
}
