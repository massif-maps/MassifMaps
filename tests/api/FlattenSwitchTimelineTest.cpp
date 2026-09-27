// What a 2D/3D switch cost, split by phase (terrain/FlattenSwitchTimeline.h).
// Not covered: the settle must not poll layers (isUpdateInProgress takes _sourceMutex under
// MapRenderer::_mutex, a deadlock no host test can see).

#include "terrain/FlattenSwitchTimeline.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif;

namespace {
    using Phase = FlattenSwitch::Phase;

    // Returns true when the timeline closed a report on this frame.
    bool frame(FlattenSwitchTimeline& timeline, FlattenSwitchTimeline::Report& report, Phase phase,
               float deltaSeconds, bool bakesQueued = false, int tilesOwed = 0, int bakes = 0,
               bool warmTimedOut = false) {
        FlattenSwitchTimeline::Input input;
        input.phase = phase;
        input.deltaSeconds = deltaSeconds;
        input.bakesQueued = bakesQueued;
        input.tilesOwed = tilesOwed;
        input.bakes = bakes;
        input.warmTimedOut = warmTimedOut;
        return timeline.step(input, report);
    }
}

void testFlattenSwitchTimeline() {
    FlattenSwitchTimeline::Report report;

    // The renderer seeds the phase before the first step; that seed is not a transition.
    {
        FlattenSwitchTimeline timeline;
        bool closed = false;
        for (int i = 0; i < 10; i++) {
            closed = frame(timeline, report, Phase::FLAT, 0.016f) || closed;
        }
        TEST_CHECK(!closed, "a map that never switches reports nothing");
    }

    {
        FlattenSwitchTimeline timeline;
        frame(timeline, report, Phase::FLAT, 0.016f);
        for (int i = 0; i < 10; i++) {
            TEST_CHECK(!frame(timeline, report, Phase::WARMING, 0.1f, false, 12), "the warm wait does not close the report");
        }
        for (int i = 0; i < 5; i++) {
            frame(timeline, report, Phase::RAMPING, 0.2f);
        }
        TEST_CHECK(!frame(timeline, report, Phase::TERRAIN, 0.1f, true, 0, 4), "a drape still baking holds the report open");
        bool closed = frame(timeline, report, Phase::TERRAIN, 0.1f, false, 0, 2);
        TEST_CHECK(closed, "the report closes the frame the bake queue empties");
        TEST_CHECK(report.rising, "and it knows which way the switch went");
        TEST_CHECK(std::abs(report.warmSeconds - 1.0f) < 1e-4f, "the warm wait is the tile re-decode, on its own");
        TEST_CHECK(report.warmFrames == 10, "counted in frames as well as seconds");
        TEST_CHECK(report.tilesOwed == 12, "with what it was still waiting for");
        TEST_CHECK(std::abs(report.rampSeconds - 1.0f) < 1e-4f, "the ramp is the animation, on its own");
        TEST_CHECK(std::abs(report.settleSeconds - 0.2f) < 1e-4f, "the settle is what the camera waited for after it landed");
        TEST_CHECK(report.bakes == 6, "and how many drape bakes that took");
        TEST_CHECK(std::abs(report.totalSeconds() - 2.2f) < 1e-4f, "the total is the three of them");
    }

    {
        FlattenSwitchTimeline timeline;
        frame(timeline, report, Phase::TERRAIN, 0.016f);
        for (int i = 0; i < 4; i++) {
            frame(timeline, report, Phase::RAMPING, 0.25f);
        }
        TEST_CHECK(frame(timeline, report, Phase::FLAT, 0.016f), "a sink closes as soon as it lands");
        TEST_CHECK(!report.rising, "and reports as 3D->2D");
        TEST_CHECK(report.warmSeconds == 0.0f, "sinking never waits for tiles");
        TEST_CHECK(std::abs(report.rampSeconds - 1.0f) < 1e-4f, "only the ramp cost anything");
    }

    {
        FlattenSwitchTimeline timeline;
        frame(timeline, report, Phase::FLAT, 0.016f);
        frame(timeline, report, Phase::WARMING, 1.0f, false, 3, 0, false);
        frame(timeline, report, Phase::WARMING, 1.0f, false, 3, 0, true);
        frame(timeline, report, Phase::RAMPING, 0.5f);
        TEST_CHECK(frame(timeline, report, Phase::TERRAIN, 0.016f), "the switch still completes");
        TEST_CHECK(report.timedOut, "and the report says the tiles never came");
    }

    {
        FlattenSwitchTimeline timeline;
        frame(timeline, report, Phase::FLAT, 0.016f);
        frame(timeline, report, Phase::MANUAL, 0.5f);
        frame(timeline, report, Phase::MANUAL, 0.5f);
        TEST_CHECK(frame(timeline, report, Phase::TERRAIN, 0.016f), "an app-driven rise reports too");
        TEST_CHECK(report.rising, "in the direction it came from");
        TEST_CHECK(std::abs(report.rampSeconds - 1.0f) < 1e-4f, "with the app's own animation as the ramp");
    }

    {
        FlattenSwitchTimeline timeline;
        frame(timeline, report, Phase::FLAT, 0.016f);
        frame(timeline, report, Phase::RAMPING, 0.5f);
        bool closed = false;
        for (int i = 0; i < 100 && !closed; i++) {
            closed = frame(timeline, report, Phase::TERRAIN, 1.0f, true);
        }
        TEST_CHECK(closed, "the settle gives up rather than never reporting");
        TEST_CHECK(report.settleSeconds >= FlattenSwitchTimeline::MAX_SETTLE_SECONDS, "at its own cap");
    }

    {
        FlattenSwitchTimeline timeline;
        frame(timeline, report, Phase::FLAT, 0.016f);
        frame(timeline, report, Phase::RAMPING, 0.5f);
        TEST_CHECK(frame(timeline, report, Phase::TERRAIN, 0.016f), "the first switch reports");
        frame(timeline, report, Phase::RAMPING, 0.25f);
        TEST_CHECK(frame(timeline, report, Phase::FLAT, 0.016f), "the second one does too");
        TEST_CHECK(std::abs(report.rampSeconds - 0.25f) < 1e-4f, "with its own ramp, not both of them");
    }
}
