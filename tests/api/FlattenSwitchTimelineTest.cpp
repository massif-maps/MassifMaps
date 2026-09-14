/*
 * What a 2D/3D switch cost, split by phase (terrain/FlattenSwitchTimeline.h).
 *
 * The case this exists for: "the switch is slow" on the Crosscall covers three separate waits with
 * three separate fixes - the tile re-decode the rise waits on, the ramp itself, and the drape
 * catching up after the camera has landed. One report has to name which one.
 *
 * NOT covered here: that the renderer feeds it the right phase every frame, or that the drape's
 * pending flag is set where the bakes actually run - that is the device run named in the PR. Nor the
 * lock order: the settle deliberately does NOT poll the layers, because a composite's
 * isUpdateInProgress takes _sourceMutex and the render thread holds MapRenderer::_mutex - polling it
 * there hung the app outright on the first switch. No host test can catch that.
 */

#include "terrain/FlattenSwitchTimeline.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif;

namespace {
    using Phase = FlattenSwitch::Phase;

    // One frame at 'phase'. Returns true when the timeline closed a report on this frame.
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

    // A map sitting flat is not a switch, however many frames it draws - the renderer seeds the
    // switch before the first step, and the timeline must not read that seed as a transition.
    {
        FlattenSwitchTimeline timeline;
        bool closed = false;
        for (int i = 0; i < 10; i++) {
            closed = frame(timeline, report, Phase::FLAT, 0.016f) || closed;
        }
        TEST_CHECK(!closed, "a map that never switches reports nothing");
    }

    // The rise, in full: warm, ramp, settle, one report.
    {
        FlattenSwitchTimeline timeline;
        frame(timeline, report, Phase::FLAT, 0.016f);
        for (int i = 0; i < 10; i++) {
            TEST_CHECK(!frame(timeline, report, Phase::WARMING, 0.1f, false, 12), "the warm wait does not close the report");
        }
        for (int i = 0; i < 5; i++) {
            frame(timeline, report, Phase::RAMPING, 0.2f);
        }
        // Landed, but the drape is still baking: the map is not finished and neither is the report.
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

    // The sink has nothing to wait for, so it is ramp only - and it is not called a rise.
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

    // A warm wait that gave up says so: late is better than never, but it is not the same number.
    {
        FlattenSwitchTimeline timeline;
        frame(timeline, report, Phase::FLAT, 0.016f);
        frame(timeline, report, Phase::WARMING, 1.0f, false, 3, 0, false);
        frame(timeline, report, Phase::WARMING, 1.0f, false, 3, 0, true);
        frame(timeline, report, Phase::RAMPING, 0.5f);
        TEST_CHECK(frame(timeline, report, Phase::TERRAIN, 0.016f), "the switch still completes");
        TEST_CHECK(report.timedOut, "and the report says the tiles never came");
    }

    // An app driving the ratio itself is still a switch: MANUAL counts as the ramp it is.
    {
        FlattenSwitchTimeline timeline;
        frame(timeline, report, Phase::FLAT, 0.016f);
        frame(timeline, report, Phase::MANUAL, 0.5f);
        frame(timeline, report, Phase::MANUAL, 0.5f);
        TEST_CHECK(frame(timeline, report, Phase::TERRAIN, 0.016f), "an app-driven rise reports too");
        TEST_CHECK(report.rising, "in the direction it came from");
        TEST_CHECK(std::abs(report.rampSeconds - 1.0f) < 1e-4f, "with the app's own animation as the ramp");
    }

    // A drape that never drains must not hold the report open for good.
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

    // Two switches in a row are two reports, not one running total.
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
