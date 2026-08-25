/*
 * Tests for the 2D/3D switch (all/native/terrain/FlattenSwitch.h): the phases, which direction
 * waits for its tiles, when the decode density is allowed to move, and the timeout that keeps a
 * tile that never loads from pinning the map in 2D.
 *
 * NOT covered here: everything that feeds it. Whether TileLayer really reports its tiles settled,
 * whether the two densities really draw the same picture flat, and whether the switch is really
 * invisible are device checks - see docs/internals/rendering/04-terrain.md.
 */

#include "terrain/FlattenSwitch.h"

using namespace massif;

#include "TestCheck.h"

namespace {

    using Phase = FlattenSwitch::Phase;

    // Binary-exact numbers throughout: a ramp of 0.25 s a frame over 0.5 s lands on 1.0 in two
    // frames with no rounding left over, so a frame COUNT can be asserted at all.
    FlattenSwitch::Input input(bool flatten, bool fullSwitch, bool tilesReady, float delta = 0.25f) {
        FlattenSwitch::Input in;
        in.flatten = flatten;
        in.fullSwitch = fullSwitch;
        in.tilesReady = tilesReady;
        in.deltaSeconds = delta;
        in.flattenDuration = 0.5f;
        in.riseDuration = 0.5f;
        in.warmTimeout = 2.0f;
        return in;
    }

    FlattenSwitch::State flatState(bool decode3D) {
        FlattenSwitch::State state;
        state.phase = Phase::FLAT;
        state.ratio = 1.0f;
        state.decode3D = decode3D;
        return state;
    }

    void testFlatteningNeverWaits() {
        // 3D tiles draw correctly flat, so this direction has nothing to wait for: it ramps at once
        // even with no tile loaded.
        FlattenSwitch::State state;
        TEST_CHECK(state.phase == Phase::TERRAIN, "the switch starts in 3D, like TerrainOptions");

        state = FlattenSwitch::step(state, input(true, true, false));
        TEST_CHECK(state.phase == Phase::RAMPING, "asking for flat starts the ramp immediately");
        TEST_CHECK(state.decode3D, "and the tiles stay decoded for 3D until the map IS flat");

        for (int i = 0; i < 2; i++) {
            state = FlattenSwitch::step(state, input(true, true, false));
        }
        TEST_CHECK(state.phase == Phase::FLAT, "0.5 s of ramp at 0.25 s a frame lands flat");
        TEST_CHECK(state.ratio == 1.0f, "flat is ratio 1");
        TEST_CHECK(state.decode3D, "the frame it lands on still holds the 3D tiles");

        // Only the frame AFTER it has settled flat does the decode drop - the swap is made where
        // both densities draw the same picture.
        state = FlattenSwitch::step(state, input(true, true, false));
        TEST_CHECK(!state.decode3D, "settled flat, FULL mode drops the terrain subdivision");
    }

    void testRenderModeNeverDropsTheDecode() {
        FlattenSwitch::State state = flatState(true);
        for (int i = 0; i < 5; i++) {
            state = FlattenSwitch::step(state, input(true, false, false));
        }
        TEST_CHECK(state.phase == Phase::FLAT && state.decode3D,
                   "RENDER mode keeps the 3D tiles however long the map stays flat - that is the whole difference");
    }

    void testRisingWaitsForItsTiles() {
        // Flat-decoded tiles chord straight over a valley, so this direction may not rise before the
        // tiles it needs exist.
        FlattenSwitch::State state = flatState(false);

        state = FlattenSwitch::step(state, input(false, true, false));
        TEST_CHECK(state.phase == Phase::WARMING, "asking for 3D starts the wait, not the ramp");
        TEST_CHECK(state.decode3D, "and asks for the 3D tiles right away");
        TEST_CHECK(state.ratio == 1.0f, "the map stays flat while it waits");

        for (int i = 0; i < 5; i++) {
            state = FlattenSwitch::step(state, input(false, true, false));
            TEST_CHECK(state.phase == Phase::WARMING && state.ratio == 1.0f,
                       "and keeps waiting, rendering 2D, for as long as the tiles are not there");
        }

        state = FlattenSwitch::step(state, input(false, true, true));
        TEST_CHECK(state.phase == Phase::RAMPING, "the tiles arriving is what starts the ramp");

        for (int i = 0; i < 2; i++) {
            state = FlattenSwitch::step(state, input(false, true, true));
        }
        TEST_CHECK(state.phase == Phase::TERRAIN && state.ratio == 0.0f, "and it ends in 3D");
    }

    void testTheWaitGivesUp() {
        FlattenSwitch::State state = flatState(false);
        state = FlattenSwitch::step(state, input(false, true, false));

        // 2 s at 0.25 s a frame. Late 3D beats a map pinned flat by one tile that never loads.
        for (int i = 0; i < 7; i++) {
            state = FlattenSwitch::step(state, input(false, true, false));
            TEST_CHECK(state.phase == Phase::WARMING, "the timeout has not run out yet");
        }
        state = FlattenSwitch::step(state, input(false, true, false));
        TEST_CHECK(state.phase == Phase::RAMPING, "past the timeout it rises anyway");

        // A timeout of 0 waits forever, which is what an app asking for no timeout means.
        FlattenSwitch::State forever = flatState(false);
        FlattenSwitch::Input in = input(false, true, false);
        in.warmTimeout = 0.0f;
        forever = FlattenSwitch::step(forever, in);
        for (int i = 0; i < 100; i++) {
            forever = FlattenSwitch::step(forever, in);
        }
        TEST_CHECK(forever.phase == Phase::WARMING, "with no timeout it waits as long as it takes");
    }

    void testAskingBackMidSwitch() {
        // A tilt gesture that crosses the threshold and comes straight back must not leave the
        // switch half way.
        FlattenSwitch::State state = flatState(false);
        state = FlattenSwitch::step(state, input(false, true, false));
        TEST_CHECK(state.phase == Phase::WARMING, "on its way up");
        state = FlattenSwitch::step(state, input(true, true, false));
        TEST_CHECK(state.phase == Phase::FLAT && state.ratio == 1.0f,
                   "asked back before it ever left, so it is simply flat again");

        // Mid-ramp the other way: the ramp reverses rather than finishing first.
        FlattenSwitch::State ramp;
        ramp = FlattenSwitch::step(ramp, input(true, true, false));
        ramp = FlattenSwitch::step(ramp, input(true, true, false));
        TEST_CHECK(ramp.phase == Phase::RAMPING && ramp.ratio > 0.0f && ramp.ratio < 1.0f, "mid-ramp");
        for (int i = 0; i < 4 && ramp.phase == Phase::RAMPING; i++) {
            ramp = FlattenSwitch::step(ramp, input(false, true, true));
        }
        TEST_CHECK(ramp.phase == Phase::TERRAIN, "reversing the ask reverses the ramp");
    }

    void testInstantSwitch() {
        // AutoFlattenDuration 0: one frame each way, and the tile wait still applies going up.
        FlattenSwitch::Input in = input(true, true, false, 0.016f);
        in.flattenDuration = 0.0f;
        in.riseDuration = 0.0f;

        FlattenSwitch::State state;
        state = FlattenSwitch::step(state, in);
        state = FlattenSwitch::step(state, in);
        TEST_CHECK(state.phase == Phase::FLAT && state.ratio == 1.0f, "no duration lands flat in one ramp frame");

        in.flatten = false;
        state = FlattenSwitch::step(state, in);
        TEST_CHECK(state.phase == Phase::WARMING, "the wait is not the ramp, so a duration of 0 does not skip it");
    }

    void testTerrainActiveFollowsTheRatio() {
        // Everything the renderer gates on isActive() follows this, so WARMING must read INACTIVE:
        // its whole point is that the wait costs 2D and shows no half-built terrain.
        FlattenSwitch::State state = flatState(false);
        TEST_CHECK(!FlattenSwitch::isTerrainActive(state), "flat is not active");

        state = FlattenSwitch::step(state, input(false, true, false));
        TEST_CHECK(state.phase == Phase::WARMING && !FlattenSwitch::isTerrainActive(state),
                   "and neither is warming up - it renders as plain 2D");

        state = FlattenSwitch::step(state, input(false, true, true));
        state = FlattenSwitch::step(state, input(false, true, true));
        TEST_CHECK(FlattenSwitch::isTerrainActive(state), "the moment the ramp leaves flat, it is");
    }

    void testPerDirectionDurations() {
        // The two ways are timed apart: the rise is the one an app matches to a camera flight.
        FlattenSwitch::Input in = input(true, true, false);
        in.flattenDuration = 0.5f;  // 2 frames at 0.25 s
        in.riseDuration = 1.0f;     // 4 frames

        FlattenSwitch::State state;
        state = FlattenSwitch::step(state, in);  // TERRAIN -> RAMPING
        int down = 0;
        while (state.phase == Phase::RAMPING && down < 10) {
            state = FlattenSwitch::step(state, in);
            down++;
        }
        TEST_CHECK(state.phase == Phase::FLAT && down == 2, "sinking takes the flatten duration");

        in.flatten = false;
        state = FlattenSwitch::step(state, in);  // FLAT -> WARMING
        in.tilesReady = true;
        state = FlattenSwitch::step(state, in);  // WARMING -> RAMPING
        int up = 0;
        while (state.phase == Phase::RAMPING && up < 10) {
            state = FlattenSwitch::step(state, in);
            up++;
        }
        TEST_CHECK(state.phase == Phase::TERRAIN && up == 4, "and rising takes its own, longer one");
    }

    void testManualRatio() {
        // The app driving the ratio off its own clock - a camera flight's progress - is the only way
        // two animations can be made to match exactly.
        FlattenSwitch::State state;  // 3D
        FlattenSwitch::Input in = input(true, true, true);
        in.manual = true;

        in.manualRatio = 0.25f;
        state = FlattenSwitch::step(state, in);
        TEST_CHECK(state.phase == Phase::MANUAL && state.ratio == 0.25f,
                   "the ratio is taken verbatim, no ramp of the switch's own");
        in.manualRatio = 0.75f;
        state = FlattenSwitch::step(state, in);
        TEST_CHECK(state.ratio == 0.75f, "and follows the app frame by frame");
        TEST_CHECK(state.decode3D, "still decoded for 3D on the way down");

        in.manualRatio = 1.0f;
        state = FlattenSwitch::step(state, in);
        TEST_CHECK(state.ratio == 1.0f && !state.decode3D,
                   "landing flat in FULL mode drops the terrain subdivision");

        // Handing it back resumes the automatic switch from wherever the app left it.
        in.manual = false;
        in.flatten = true;
        state = FlattenSwitch::step(state, in);
        TEST_CHECK(state.phase == Phase::FLAT, "released at 1, so it resumes flat");
    }

    void testManualRiseIsStillGatedOnTiles() {
        // Manual does NOT mean unsafe: unsubdivided geometry displaced over relief is a road in the
        // sky, so the ground is held flat until the tiles for 3D exist.
        FlattenSwitch::State state = flatState(false);
        FlattenSwitch::Input in = input(false, true, false);
        in.manual = true;
        in.manualRatio = 0.5f;

        state = FlattenSwitch::step(state, in);
        TEST_CHECK(state.phase == Phase::MANUAL && state.ratio == 1.0f,
                   "the app asked for 3D but the ground is held flat");
        TEST_CHECK(state.decode3D, "while the tiles it needs are asked for");
        TEST_CHECK(FlattenSwitch::isWaitingForTiles(state, in), "and the hold is visible to the app");

        in.tilesReady = true;
        state = FlattenSwitch::step(state, in);
        TEST_CHECK(state.ratio == 0.5f, "the tiles arriving releases it");
        TEST_CHECK(!FlattenSwitch::isWaitingForTiles(state, in), "and the hold is over");
    }

}

void testFlattenSwitch() {
    testFlatteningNeverWaits();
    testRenderModeNeverDropsTheDecode();
    testRisingWaitsForItsTiles();
    testTheWaitGivesUp();
    testAskingBackMidSwitch();
    testInstantSwitch();
    testTerrainActiveFollowsTheRatio();
    testPerDirectionDurations();
    testManualRatio();
    testManualRiseIsStillGatedOnTiles();
}
