// What tells an animation still moving from one going nowhere (vt/AnimationProgress.h). Not covered:
// that startFrame feeds it the blends and opacities it stepped - GLTileRenderer drags GL in, so that is
// a device check, read off the "what kept the tile renderers asking" log line.

#include "AnimationProgress.h"

using namespace massif::vt;

#include "TestCheck.h"

namespace {
    std::uint64_t stateOf(float blend, float opacity) {
        AnimationState state;
        state.add(blend);
        state.add(opacity);
        return state.value();
    }
}

void testAnimationProgress() {
    TEST_CHECK(stateOf(0.25f, 0.5f) == stateOf(0.25f, 0.5f), "the same values hash the same");
    TEST_CHECK(stateOf(0.25f, 0.5f) != stateOf(0.5f, 0.25f), "the order of the values counts");
    TEST_CHECK(stateOf(0.25f, 0.5f) != stateOf(0.25f, 0.5000001f), "one step of a float is a different state");

    AnimationState noClock;
    AnimationState withClock;
    withClock.add(1.5);
    TEST_CHECK(noClock.value() != withClock.value(), "a spinning model's clock moves the state");

    // A fade only moves forward: every frame is a state the history has not seen.
    AnimationStall fade;
    float blend = 0.0f;
    bool everStalled = false;
    for (int frame = 0; frame < 50 && blend < 1.0f; frame++) {
        blend = std::min(1.0f, blend + 0.03f);
        everStalled = fade.stalled(stateOf(blend, 0.0f)) || everStalled;
    }
    TEST_CHECK(!everStalled, "a fade that progresses is never reported stalled");

    // A step that moved nothing: the same state twice.
    AnimationStall still;
    TEST_CHECK(!still.stalled(stateOf(0.5f, 0.0f)), "the first state is new");
    TEST_CHECK(still.stalled(stateOf(0.5f, 0.0f)), "an unchanged state is a stall");
    TEST_CHECK(still.stalled(stateOf(0.5f, 0.0f)), "and stays one");

    // Two states taking turns, as a label flipping between shown and hidden every frame does.
    AnimationStall flip;
    TEST_CHECK(!flip.stalled(stateOf(0.3f, 0.0f)), "flip: first");
    TEST_CHECK(!flip.stalled(stateOf(0.4f, 0.0f)), "flip: second");
    TEST_CHECK(flip.stalled(stateOf(0.3f, 0.0f)), "flip: back to the first is a stall");

    // The history is short: an old state is no stall.
    AnimationStall old;
    old.stalled(stateOf(0.1f, 0.0f));
    for (int i = 0; i < static_cast<int>(AnimationStall::HISTORY); i++) {
        old.stalled(stateOf(0.2f + 0.1f * i, 0.0f));
    }
    TEST_CHECK(!old.stalled(stateOf(0.1f, 0.0f)), "a state older than the history is new again");

    // An animation that ended starts the next from nothing.
    AnimationStall ended;
    ended.stalled(stateOf(0.5f, 0.0f));
    ended.reset();
    TEST_CHECK(!ended.stalled(stateOf(0.5f, 0.0f)), "after a reset the same state is new");

    RefreshStats& stats = refreshStats();
    stats.stalled++;
    TEST_CHECK(stats.stalled.exchange(0) == 1, "the counters are shared and clear on read");
}
