/*
 * Tests for the 2D/3D switch's tile stash (all/native/layers/TileCacheStash.h): with
 * TerrainFlattenMode FULL a flat map decodes plain 2D tiles, and every rise used to decode the 3D
 * set again. Measured on the web build, terrain-2d-3d's default view: repeat rises held the ground
 * flat 149-214 ms while 3D tiles the previous rise had decoded were rebuilt.
 *
 * The cull below is the layer's rule reduced to one cache: a visible tile is fetched unless a valid
 * one is cached, and TerrainDecodeWait waits on what that cull fetched. NOT covered here: the layer
 * side - that TileLayer::loadData restores only into the same decode state and TerrainOptions, and
 * that a fetch in flight across the swap is dropped (VectorTileLayer's transformer check).
 */

#include "layers/TileCacheStash.h"
#include "layers/TerrainDecodeWait.h"

#include <stdext/timed_lru_cache.h>

#include <chrono>
#include <string>
#include <unordered_set>
#include <vector>

using namespace massif;

#include "TestCheck.h"

namespace {

    typedef cache::timed_lru_cache<long long, std::string> Cache;

    const std::size_t PRELOADING_CAPACITY = 10 * 1024;
    const std::size_t VISIBLE_CAPACITY = 512 * 1024;

    std::size_t sizeOf(const std::string&) { return 100; }

    void swap(Cache& visibleCache, Cache& preloadingCache, Cache& stashedVisibleCache) {
        swapStashedTiles(visibleCache, preloadingCache, stashedVisibleCache, sizeOf);
    }

    bool holds(const Cache& cache, long long tileId, const std::string& decode, bool valid) {
        std::string tile;
        return cache.peek(tileId, tile) && tile == decode && cache.valid(tileId) == valid;
    }

    // One cull: the visible tiles with no valid copy are fetched, the switch waits on them, and the
    // fetches land decoded for the current state.
    std::unordered_set<long long> cull(Cache& visibleCache, const std::vector<long long>& visible, TerrainDecodeWait& wait) {
        std::unordered_set<long long> fetched;
        for (long long tileId : visible) {
            if (!visibleCache.exists(tileId) || !visibleCache.valid(tileId)) {
                fetched.insert(tileId);
            }
        }
        wait.recordFetched(fetched);
        return fetched;
    }

    void land(Cache& visibleCache, const std::unordered_set<long long>& fetched, const std::string& decode) {
        for (long long tileId : fetched) {
            visibleCache.put(tileId, decode, sizeOf(decode));
        }
    }

    bool settledNow(TerrainDecodeWait& wait) {
        return wait.settle([](long long) { return true; });
    }

    void testFirstSwapKeepsStandIns() {
        Cache visibleCache(VISIBLE_CAPACITY), preloadingCache(PRELOADING_CAPACITY), stashedVisibleCache(VISIBLE_CAPACITY);
        visibleCache.put(1, "2d", 100);
        visibleCache.put(2, "2d", 100);
        preloadingCache.put(9, "2d", 100);
        swap(visibleCache, preloadingCache, stashedVisibleCache);
        TEST_CHECK(holds(visibleCache, 1, "2d", false) && holds(visibleCache, 2, "2d", false), "nothing stashed yet: the outgoing tiles stay drawn, invalid, until 3D ones land");
        TEST_CHECK(holds(stashedVisibleCache, 1, "2d", true) && holds(stashedVisibleCache, 2, "2d", true), "the outgoing tiles are put aside still valid");
        TEST_CHECK(preloadingCache.empty() && !stashedVisibleCache.exists(9), "preloading tiles are dropped, not stashed");
    }

    void testRepeatRiseReusesTheStash() {
        Cache visibleCache(VISIBLE_CAPACITY), preloadingCache(PRELOADING_CAPACITY), stashedVisibleCache(VISIBLE_CAPACITY);
        std::vector<long long> view = { 1, 2, 3 };
        TerrainDecodeWait wait;
        land(visibleCache, { 1, 2, 3 }, "2d");

        // First rise: everything is decoded again, as before the stash.
        swap(visibleCache, preloadingCache, stashedVisibleCache);
        wait.markUnsettled();
        std::unordered_set<long long> fetched = cull(visibleCache, view, wait);
        TEST_CHECK(fetched.size() == 3 && !settledNow(wait), "the first rise waits for every visible tile");
        land(visibleCache, fetched, "3d");

        // Fall, then rise again at the same view.
        swap(visibleCache, preloadingCache, stashedVisibleCache);
        TEST_CHECK(holds(visibleCache, 2, "2d", true), "the fall gets the flat tiles back, valid");
        swap(visibleCache, preloadingCache, stashedVisibleCache);
        wait.markUnsettled();
        fetched = cull(visibleCache, view, wait);
        TEST_CHECK(fetched.empty(), "a repeat rise at the same view fetches nothing");
        TEST_CHECK(settledNow(wait), "so the switch has nothing to wait for");
        TEST_CHECK(holds(visibleCache, 1, "3d", true) && holds(stashedVisibleCache, 1, "2d", true), "each mode keeps its own decode");
    }

    void testMovedViewWaitsOnlyForNewTiles() {
        // Panned while flat: a tile the stash lacks is still fetched, and its flat copy stands in.
        Cache visibleCache(VISIBLE_CAPACITY), preloadingCache(PRELOADING_CAPACITY), stashedVisibleCache(VISIBLE_CAPACITY);
        stashedVisibleCache.put(1, "3d", 100);
        visibleCache.put(1, "2d", 100);
        visibleCache.put(4, "2d", 100);
        swap(visibleCache, preloadingCache, stashedVisibleCache);
        TEST_CHECK(holds(visibleCache, 1, "3d", true), "a stashed tile wins over the outgoing stand-in");
        TEST_CHECK(holds(visibleCache, 4, "2d", false), "a tile the stash lacks keeps an invalid stand-in");
        TerrainDecodeWait wait;
        wait.markUnsettled();
        std::unordered_set<long long> fetched = cull(visibleCache, { 1, 4 }, wait);
        TEST_CHECK(fetched.size() == 1 && fetched.count(4) == 1, "only the tile the stash lacks is fetched");
    }

    void testExpiredTileIsNotRevived() {
        // A tile whose data expired while stashed comes back expired, and is fetched like any other.
        Cache visibleCache(VISIBLE_CAPACITY), preloadingCache(PRELOADING_CAPACITY), stashedVisibleCache(VISIBLE_CAPACITY);
        stashedVisibleCache.put(1, "3d", 100);
        stashedVisibleCache.invalidate(1, std::chrono::steady_clock::now() - std::chrono::seconds(1));
        swap(visibleCache, preloadingCache, stashedVisibleCache);
        TEST_CHECK(holds(visibleCache, 1, "3d", false), "the stash keeps a tile's expiry");
    }

}

void testTileCacheStash() {
    testFirstSwapKeepsStandIns();
    testRepeatRiseReusesTheStash();
    testMovedViewWaitsOnlyForNewTiles();
    testExpiredTileIsNotRevived();
}
