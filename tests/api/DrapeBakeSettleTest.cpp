/*
 * Tests for the drape re-bake hold (all/native/terrain/DrapeBakeSettle.h): a tile whose content is
 * still arriving waits for its fingerprint to stop moving before it re-bakes.
 *
 * The case that matters is the composite's late layers: three vector groups, the hillshade and the
 * contours land a few frames apart, and each landing used to re-bake the tile. Measured on the
 * Crosscall, 74 of 241 bakes in a pan replaced a picture that was at most 5 frames old.
 *
 * NOT covered here: that a held tile is skipped by the bake queue, or what it shows meanwhile (its
 * own older picture, as before). Both are MapRenderer state: a device check.
 */

#include "terrain/DrapeBakeSettle.h"

using namespace massif;

#include "TestCheck.h"

namespace {

    using massif::vt::TileId;

    const TileId TILE(16, 33890, 23266);

    void testAFirstSightingIsNeverHeld() {
        DrapeBakeSettle settle;
        settle.observe(TILE, 7, 100);
        TEST_CHECK(!settle.shouldHold(TILE, 100), "a tile with no baseline has nothing to coalesce");
        TEST_CHECK(!settle.shouldHold(TileId(15, 1, 1), 100), "an unknown tile is not held");
    }

    void testAnUnchangedTileIsNeverHeld() {
        DrapeBakeSettle settle;
        for (unsigned frame = 100; frame < 130; frame++) {
            settle.observe(TILE, 7, frame);
            TEST_CHECK(!settle.shouldHold(TILE, frame), "a fingerprint that never moved owes nothing");
        }
    }

    void testAChangeWaitsForItToSettle() {
        DrapeBakeSettle settle;
        settle.observe(TILE, 7, 100);
        settle.observe(TILE, 8, 101);
        TEST_CHECK(settle.shouldHold(TILE, 101), "the frame it moved: held");
        settle.observe(TILE, 8, 102);
        TEST_CHECK(settle.shouldHold(TILE, 102), "still inside the settle window");
        settle.observe(TILE, 8, 103);
        TEST_CHECK(settle.shouldHold(TILE, 103), "one frame short of settled");
        settle.observe(TILE, 8, 104);
        TEST_CHECK(!settle.shouldHold(TILE, 104), "stable for SETTLE_FRAMES: bake");
    }

    void testEachLandingRestartsTheWait() {
        DrapeBakeSettle settle;
        settle.observe(TILE, 7, 100);
        settle.observe(TILE, 8, 101);
        settle.observe(TILE, 9, 103);
        TEST_CHECK(settle.shouldHold(TILE, 104), "a second layer landed: the clock restarted");
        settle.observe(TILE, 9, 106);
        TEST_CHECK(!settle.shouldHold(TILE, 106), "and runs out from the last landing");
    }

    void testAChurningTileStillBakesByTheCap() {
        DrapeBakeSettle settle;
        settle.observe(TILE, 0, 100);
        unsigned bakedAt = 0;
        for (unsigned frame = 101; frame < 140 && bakedAt == 0; frame++) {
            settle.observe(TILE, frame, frame); // a new fingerprint every frame
            if (!settle.shouldHold(TILE, frame)) {
                bakedAt = frame;
            }
        }
        TEST_CHECK(bakedAt == 101 + DrapeBakeSettle::MAX_HOLD_FRAMES, "content that never stops arriving is held MAX_HOLD_FRAMES, not forever");
    }

    void testABakeClearsThePendingChange() {
        DrapeBakeSettle settle;
        settle.observe(TILE, 7, 100);
        settle.observe(TILE, 8, 101);
        settle.baked(TILE);
        TEST_CHECK(!settle.shouldHold(TILE, 101), "baked: nothing pending");
        settle.observe(TILE, 8, 102);
        TEST_CHECK(!settle.shouldHold(TILE, 102), "and the same fingerprint stays unheld");
        settle.observe(TILE, 9, 103);
        TEST_CHECK(settle.shouldHold(TILE, 103), "a new change after the bake waits again, from its own start");
    }

    void testTilesAreIndependent() {
        DrapeBakeSettle settle;
        TileId other(16, 33891, 23266);
        settle.observe(TILE, 7, 100);
        settle.observe(other, 7, 100);
        settle.observe(TILE, 8, 101);
        settle.observe(other, 7, 101);
        TEST_CHECK(settle.shouldHold(TILE, 101), "the tile that changed waits");
        TEST_CHECK(!settle.shouldHold(other, 101), "its neighbour does not");
    }

    void testPruneForgetsTilesLeftTheCover() {
        DrapeBakeSettle settle;
        settle.observe(TILE, 7, 100);
        settle.observe(TileId(16, 1, 1), 7, 300);
        settle.prune(300, 120);
        TEST_CHECK(settle.size() == 1, "the tile unseen for 200 frames is dropped");
        settle.observe(TILE, 8, 301);
        TEST_CHECK(!settle.shouldHold(TILE, 301), "and comes back as a first sighting, unheld");
    }

}

void testDrapeBakeSettle() {
    testAFirstSightingIsNeverHeld();
    testAnUnchangedTileIsNeverHeld();
    testAChangeWaitsForItToSettle();
    testEachLandingRestartsTheWait();
    testAChurningTileStillBakesByTheCap();
    testABakeClearsThePendingChange();
    testTilesAreIndependent();
    testPruneForgetsTilesLeftTheCover();
}
