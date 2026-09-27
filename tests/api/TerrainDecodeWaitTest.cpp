// What one layer makes the 2D/3D switch wait for (layers/TerrainDecodeWait.h): only the tiles the
// swap invalidated, since a moving camera always has some tile fetching.
// See docs/internals/rendering/04-terrain.md.

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
    {
        TerrainDecodeWait wait;
        TEST_CHECK(wait.isSettled(), "an untouched layer is settled");
        TEST_CHECK(wait.settle(fetching({ 1, 2, 3 })), "and stays settled however much is in flight");
    }

    // Before the cull names the set, "nothing pending" would let the terrain rise too early.
    {
        TerrainDecodeWait wait;
        wait.markUnsettled();
        TEST_CHECK(!wait.settle(nothingFetching()), "an unnamed wait is not settled by an empty fetch list");
    }

    {
        TerrainDecodeWait wait;
        wait.markUnsettled();
        wait.recordFetched({ 10, 11 });
        TEST_CHECK(!wait.settle(fetching({ 10, 11 })), "the swap's own tiles hold it");
        TEST_CHECK(!wait.settle(fetching({ 11 })), "one left is still one");
        TEST_CHECK(wait.settle(fetching({ 77, 78, 79 })), "tiles the camera moved onto do not hold it");
    }

    {
        TerrainDecodeWait wait;
        wait.markUnsettled();
        wait.recordFetched({});
        TEST_CHECK(wait.settle(fetching({ 1 })), "a swap that refetched nothing is settled at once");
    }

    // Only the first cull names the set, or a moving camera would keep extending the wait.
    {
        TerrainDecodeWait wait;
        wait.markUnsettled();
        wait.recordFetched({ 10 });
        wait.recordFetched({ 20, 21 });
        TEST_CHECK(wait.settle(fetching({ 20, 21 })), "a later cull does not extend the wait");
    }

    {
        TerrainDecodeWait wait;
        wait.markUnsettled();
        wait.recordFetched({ 10 });
        TEST_CHECK(wait.settle(nothingFetching()), "settles when its tiles land");
        TEST_CHECK(wait.settle(fetching({ 10 })), "and does not unsettle itself");
    }

    // A hidden or out-of-zoom-range layer.
    {
        TerrainDecodeWait wait;
        wait.markUnsettled();
        wait.settleNow();
        TEST_CHECK(wait.isSettled(), "a layer with nothing on screen settles immediately");
        TEST_CHECK(wait.settle(fetching({ 1, 2 })), "and is not re-armed by the fetch list");
    }

    {
        TerrainDecodeWait wait;
        wait.markUnsettled();
        wait.recordFetched({ 10 });
        wait.markUnsettled();
        wait.recordFetched({ 20 });
        TEST_CHECK(!wait.settle(fetching({ 20 })), "the second swap's tiles hold it");
        TEST_CHECK(wait.settle(fetching({ 10 })), "the first swap's tiles no longer do");
    }

    // -1 means "not named yet"; reporting 0 there would blame the ramp in the timing report.
    {
        TerrainDecodeWait wait;
        TEST_CHECK(wait.getPendingCount() == 0, "a settled layer owes nothing");
        wait.markUnsettled();
        TEST_CHECK(wait.getPendingCount() == -1, "an unnamed wait does not report a count");
        wait.recordFetched({ 10, 11, 12 });
        TEST_CHECK(wait.getPendingCount() == 3, "once named, it is the tiles it is waiting on");
        wait.settle(fetching({ 10 }));
        TEST_CHECK(wait.getPendingCount() == 1, "and it counts down as they land");
        wait.settle(nothingFetching());
        TEST_CHECK(wait.getPendingCount() == 0, "to nothing");
    }
}
