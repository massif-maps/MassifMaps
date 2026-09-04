/*
 * Tests for the drape bake tuning rules (all/native/terrain/DrapeTuning.h): the power of two the
 * screen asks for, the cap the drape cache's byte budget puts on it, and the quantised zoom a tile
 * is baked for.
 *
 * The case that matters is the BOUNDARY. A working set of 24 against the 96 MB budget lets a
 * 1024 x 1024 x RGBA tile through with nothing to spare - 24 * 4 MB is 96 MB exactly - so the
 * comparison has to be a strict `>`. At the old working set of 64 the same arithmetic pinned every
 * device to 512, which is half the linear resolution mapbox bakes the same tile at, and is the
 * blurry drape at a grazing angle. A test that only checked "some power of two comes back" would
 * pass either way.
 *
 * NOT covered here: whether 1024 actually looks sharper, and whether the cache thrashes at that
 * size. Both are device checks - the 13.4 ms / 5.2 ms drape numbers in the rule's comment came off
 * a Crosscall north pan, not off this suite. Nor is TileRenderer::resolveDrapeResolution itself
 * reached: it needs ViewState, Options and the GL cache, i.e. the renderer.
 *
 * The zoom term has the same boundary: these check the quantisation only. That the term is adopted
 * when the camera SETTLES and held while it moves - so a pinch does not re-bake every tile on every
 * step - is a branch in MapRenderer::drawLayers on the bake path, and is a device check.
 */

#include "terrain/DrapeTuning.h"

using namespace massif;

#include "TestCheck.h"

namespace {

    const std::size_t BUDGET = 96 * 1024 * 1024;
    const int MIN_RES = 128;
    const int MAX_RES = 2048;

    // The shipped call, with only the working set and the screen left to vary.
    int resolve(double dpiScale, std::size_t workingSet, std::size_t budget = BUDGET) {
        return DrapeTuning::resolution(256, dpiScale, workingSet, budget, MIN_RES, MAX_RES);
    }

    void testBudgetBoundaryLets1024Through() {
        // 1024 x 1024 x 4 = 4 MB, and 24 of them is the budget to the byte. Exactly at the limit is
        // NOT over it - this is the whole reason the working set is 24 and not 25.
        TEST_CHECK(DrapeTuning::bytesPerTile(1024) * 24 == BUDGET, "24 tiles at 1024 is the budget exactly");
        TEST_CHECK(resolve(2.0, 24) == 1024, "a cover that exactly fills the budget is allowed to bake at 1024");
        TEST_CHECK(resolve(2.0, 25) == 512, "... and one tile more is over it, so the next power of two down");
    }

    void testOldWorkingSetPinnedEveryDeviceTo512() {
        // The regression this rule was changed for: at 64 the clamp fires whatever the screen asks.
        TEST_CHECK(resolve(2.0, 64) == 512, "at a working set of 64 a 1024 screen request is clamped to 512");
        TEST_CHECK(resolve(4.0, 64) == 512, "... and so is a 2048 one, on every device");
        TEST_CHECK(resolve(4.0, 24) == 1024, "at 24 the same screen gets 1024");
    }

    void testScreenLadderRoundsUp() {
        // Rounded UP: the cache holds one texture size, so landing under the screen's ask is a
        // magnified texel on every tile. 513 texels of demand must buy 1024, not 512.
        TEST_CHECK(resolve(1.0, 24, 0) == 512, "256 points at 1x is a 512 texel edge, exactly");
        TEST_CHECK(resolve(1.002, 24, 0) == 1024, "a hair over 512 rounds up, it does not truncate");
        TEST_CHECK(resolve(0.25, 24, 0) == 128, "a quarter-scale screen asks for 128");
    }

    void testClampsStayInsideTheRange() {
        TEST_CHECK(resolve(16.0, 24, 0) == MAX_RES, "the screen ladder never exceeds the maximum");
        // A working set nothing could satisfy must stop at the floor rather than divide to zero.
        TEST_CHECK(resolve(2.0, 100000) == MIN_RES, "an unsatisfiable budget clamps to the minimum, not below");
        TEST_CHECK(resolve(0.5, 100000) == MIN_RES, "... and a request already at the minimum stays there");
    }

    void testDisabledBudgetDoesNotClamp() {
        // debug.massif.drapebudget=0 on the device. The screen's answer stands, unreduced.
        TEST_CHECK(resolve(4.0, 64, 0) == 2048, "with no budget the working set stops mattering");
        TEST_CHECK(resolve(4.0, 64) == 512, "... which is not what the same call does with one");
    }

    // MapRenderer::DRAPE_REBAKE_ZOOM_THRESHOLD, the same quantum the label re-placement uses.
    const float STEP = 0.25f;

    void testZoomTermHoldsInsideOneStep() {
        // A tile must NOT re-bake for a drift the style's own functions cannot resolve. Both ends
        // of a step are the same term, so the fingerprint they feed is unchanged.
        TEST_CHECK(DrapeTuning::bakeZoomTerm(15.0f, STEP) == DrapeTuning::bakeZoomTerm(15.24f, STEP),
                   "a drift inside one quarter-level leaves the bake term alone");
        TEST_CHECK(DrapeTuning::bakeZoomTerm(15.0f, STEP) != DrapeTuning::bakeZoomTerm(15.26f, STEP),
                   "... and a drift past it does not, so the tile goes stale and re-bakes");
    }

    void testZoomTermStepsFourTimesPerLevel() {
        // Four bakes per zoom level, not one: the old behaviour re-baked only when a new tile level
        // brought new textures, so a road's width stepped once per integer level instead of growing.
        TEST_CHECK(DrapeTuning::bakeZoomTerm(16.0f, STEP) - DrapeTuning::bakeZoomTerm(15.0f, STEP) == 4,
                   "one zoom level is four bake terms");
        TEST_CHECK(DrapeTuning::bakeZoomTerm(15.0f, STEP) != DrapeTuning::bakeZoomTerm(15.5f, STEP),
                   "half a level apart is two terms apart, never a collision");
    }

    void testZoomTermIsMonotonicAndNeverWraps() {
        // The term is cast to an unsigned, so a negative zoom - free roam reaches one - would wrap
        // to a huge value that no later zoom repeats: every tile permanently stale.
        TEST_CHECK(DrapeTuning::bakeZoomTerm(-3.0f, STEP) == 0, "a negative zoom clamps to 0 rather than wrapping");
        TEST_CHECK(DrapeTuning::bakeZoomTerm(0.0f, STEP) == 0, "... and meets zoom 0 there");
        TEST_CHECK(DrapeTuning::bakeZoomTerm(-3.0f, STEP) == DrapeTuning::bakeZoomTerm(-9.0f, STEP),
                   "two negative zooms are the same term, so a camera below zero does not thrash");
        TEST_CHECK(DrapeTuning::bakeZoomTerm(24.0f, STEP) > DrapeTuning::bakeZoomTerm(23.0f, STEP),
                   "the term still rises at the top of the zoom range");
        // A zero threshold would be a division by zero; the guard makes it "never re-bake" instead.
        TEST_CHECK(DrapeTuning::bakeZoomTerm(15.0f, 0.0f) == DrapeTuning::bakeZoomTerm(3.0f, 0.0f),
                   "a zero threshold degrades to one constant term, not a division by zero");
    }

}

void testDrapeTuning() {
    testBudgetBoundaryLets1024Through();
    testOldWorkingSetPinnedEveryDeviceTo512();
    testScreenLadderRoundsUp();
    testClampsStayInsideTheRange();
    testDisabledBudgetDoesNotClamp();
    testZoomTermHoldsInsideOneStep();
    testZoomTermStepsFourTimesPerLevel();
    testZoomTermIsMonotonicAndNeverWraps();
}
