/*
 * Tests for what one layer makes the 2D/3D switch wait for (all/native/layers/TerrainDecodeWait.h).
 *
 * The bug it exists for: the gate used to be "no cull running and no visible tile fetching", and a
 * moving camera always has a visible tile fetching. So a flight never satisfied it on its merits -
 * what released the switch was the 2.5 s warm timeout, which is about as long as a flight, and the
 * terrain ramp started as the flight was landing (measured at Zermatt, see the PR).
 *
 * NOT covered here: that TileLayer records the right tile ids, and that the composite's children
 * are polled at all. Both need the renderer - device checks, see docs/internals/rendering/04-terrain.md.
 */

#include "layers/TerrainDecodeWait.h"

using namespace massif;

#include "TestCheck.h"

namespace {

    auto nothingFetching() {
        return [](long long) { return false; };
    }

    auto fetching(std::unordered_set<long long> ids) {
        return [ids](long long tileId) { return ids.find(tileId) != ids.end(); };
    }

}

void testTerrainDecodeWait() {
    // A layer nothing has switched is settled: the switch must not wait on a map with no terrain.
    {
        TerrainDecodeWait wait;
        TEST_CHECK(wait.isSettled(), "an untouched layer is settled");
        TEST_CHECK(wait.settle(fetching({ 1, 2, 3 })), "and stays settled however much is in flight");
    }

    // Between the swap and the cull that lists what it invalidated, the answer is "not yet" - not
    // "yes, nothing is pending", which would let the terrain rise before a single tile was asked for.
    {
        TerrainDecodeWait wait;
        wait.markUnsettled();
        TEST_CHECK(!wait.settle(nothingFetching()), "an unnamed wait is not settled by an empty fetch list");
    }

    // The whole point: a tile fetched because the camera moved on is NOT one of the swap's, so it
    // cannot hold the switch open.
    {
        TerrainDecodeWait wait;
        wait.markUnsettled();
        wait.recordFetched({ 10, 11 });
        TEST_CHECK(!wait.settle(fetching({ 10, 11 })), "the swap's own tiles hold it");
        TEST_CHECK(!wait.settle(fetching({ 11 })), "one left is still one");
        TEST_CHECK(wait.settle(fetching({ 77, 78, 79 })), "tiles the camera moved onto do not hold it");
    }

    // Nothing to refetch: every visible tile was already at the new density. Settles on the spot.
    {
        TerrainDecodeWait wait;
        wait.markUnsettled();
        wait.recordFetched({});
        TEST_CHECK(wait.settle(fetching({ 1 })), "a swap that refetched nothing is settled at once");
    }

    // Only the FIRST cull after the swap names the set, or a cull mid-flight would keep renaming it
    // with the tiles of wherever the camera is now, and the wait would never end.
    {
        TerrainDecodeWait wait;
        wait.markUnsettled();
        wait.recordFetched({ 10 });
        wait.recordFetched({ 20, 21 });
        TEST_CHECK(wait.settle(fetching({ 20, 21 })), "a later cull does not extend the wait");
    }

    // Settling is sticky: once the swap's tiles are in, a later fetch of the same ids is ordinary
    // tile traffic and says nothing about the decode state.
    {
        TerrainDecodeWait wait;
        wait.markUnsettled();
        wait.recordFetched({ 10 });
        TEST_CHECK(wait.settle(nothingFetching()), "settles when its tiles land");
        TEST_CHECK(wait.settle(fetching({ 10 })), "and does not unsettle itself");
    }

    // A hidden or out-of-zoom-range layer draws nothing, so it owes the switch nothing.
    {
        TerrainDecodeWait wait;
        wait.markUnsettled();
        wait.settleNow();
        TEST_CHECK(wait.isSettled(), "a layer with nothing on screen settles immediately");
        TEST_CHECK(wait.settle(fetching({ 1, 2 })), "and is not re-armed by the fetch list");
    }

    // A second swap while the first is still waiting starts over rather than merging the two sets.
    {
        TerrainDecodeWait wait;
        wait.markUnsettled();
        wait.recordFetched({ 10 });
        wait.markUnsettled();
        wait.recordFetched({ 20 });
        TEST_CHECK(!wait.settle(fetching({ 20 })), "the second swap's tiles hold it");
        TEST_CHECK(wait.settle(fetching({ 10 })), "the first swap's tiles no longer do");
    }
}
