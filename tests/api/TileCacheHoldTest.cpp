/*
 * Tests for the vector tile layer's cache split (all/native/layers/TileCacheHold.h): which tiles
 * stay in the visible cache and which go to the bounded preloading cache.
 *
 * The label band and the shadow casters are fetched as preloading tiles, and every cull asks for
 * all of them again. Left in the 10 MB preloading cache, a set larger than 10 MB can never be
 * complete: each arrival evicts another member, which the next cull refetches. Measured on the web
 * build, Grenoble z15.5 tilt 45 with terrain shadows: 8 label tiles and 5 caster tiles of 0.4-1.6 MB
 * decoded, refetched in a loop for as long as the page stayed open.
 *
 * The scenario below replays those sizes. NOT covered here: the layer side - that calculateDrawData
 * marks the label band and the casters as used - which needs VectorTileLayer and the renderer.
 */

#include "layers/TileCacheHold.h"

#include <stdext/timed_lru_cache.h>

#include <unordered_set>
#include <vector>

using namespace massif;

#include "TestCheck.h"

namespace {

    typedef cache::timed_lru_cache<long long, int> Cache;

    // VectorTileLayer::DEFAULT_PRELOADING_CACHE_SIZE, in KB.
    const std::size_t PRELOADING_CAPACITY = 10 * 1024;
    const std::size_t VISIBLE_CAPACITY = 512 * 1024;

    struct Tile {
        long long id;
        std::size_t size; // KB
        bool preloading;
    };

    // The measured scene: view tiles, then the label band (ids 100+) and the casters (ids 200+).
    std::vector<Tile> measuredScene() {
        std::vector<Tile> tiles = { { 1, 1087, false }, { 2, 1545, false }, { 3, 1642, false }, { 4, 1802, false } };
        for (std::size_t size : { 1174, 954, 1533, 1161, 731, 1362, 424, 1592 }) {
            tiles.push_back({ 100 + static_cast<long long>(tiles.size()), size, true });
        }
        for (std::size_t size : { 1299, 1444, 1176, 1636, 1463 }) {
            tiles.push_back({ 200 + static_cast<long long>(tiles.size()), size, true });
        }
        return tiles;
    }

    // Culls the same view repeatedly: a missing tile is fetched into the cache its kind lands in
    // (VectorTileLayer::FetchTask), and every arrival is followed by a refresh. Returns the fetches per cull.
    std::vector<int> cullRepeatedly(const std::vector<Tile>& tiles, bool holdPreloadingTiles, int culls, Cache& visibleCache, Cache& preloadingCache) {
        std::unordered_set<long long> used;
        for (const Tile& tile : tiles) {
            if (!tile.preloading || holdPreloadingTiles) {
                used.insert(tile.id);
            }
        }
        std::vector<int> fetches;
        for (int cull = 0; cull < culls; cull++) {
            int fetched = 0;
            for (const Tile& tile : tiles) {
                if (visibleCache.exists(tile.id) || preloadingCache.exists(tile.id)) {
                    continue;
                }
                fetched++;
                (tile.preloading ? preloadingCache : visibleCache).put(tile.id, 0, tile.size);
                holdTilesInUse(visibleCache, preloadingCache, used);
            }
            fetches.push_back(fetched);
        }
        return fetches;
    }

    void testTheOldSplitRefetchesForever() {
        // Guards the scenario itself: if these sizes fit the preloading cache, the next check proves nothing.
        Cache visibleCache(VISIBLE_CAPACITY), preloadingCache(PRELOADING_CAPACITY);
        std::vector<int> fetches = cullRepeatedly(measuredScene(), false, 20, visibleCache, preloadingCache);
        TEST_CHECK(fetches.back() > 0, "label band + casters in the preloading cache: still refetching after 20 culls");
    }

    void testUsedTilesAreFetchedOnce() {
        Cache visibleCache(VISIBLE_CAPACITY), preloadingCache(PRELOADING_CAPACITY);
        std::vector<Tile> tiles = measuredScene();
        std::vector<int> fetches = cullRepeatedly(tiles, true, 20, visibleCache, preloadingCache);
        TEST_CHECK(fetches.front() == static_cast<int>(tiles.size()), "the first cull fetches every tile");
        int later = 0;
        for (std::size_t i = 1; i < fetches.size(); i++) {
            later += fetches[i];
        }
        TEST_CHECK(later == 0, "held in the visible cache: no tile of the scene is fetched twice");
        bool allVisible = true;
        for (const Tile& tile : tiles) {
            allVisible = allVisible && visibleCache.exists(tile.id) && !preloadingCache.exists(tile.id);
        }
        TEST_CHECK(allVisible, "every used tile sits in the visible cache, none is left in the preloading one");
    }

    void testUnusedTilesReturnToTheBoundedCache() {
        // The view moves on: what the frame no longer uses must go back under the preloading bound,
        // or the visible cache grows with every tile ever seen.
        Cache visibleCache(VISIBLE_CAPACITY), preloadingCache(PRELOADING_CAPACITY);
        cullRepeatedly(measuredScene(), true, 1, visibleCache, preloadingCache);
        holdTilesInUse(visibleCache, preloadingCache, { 1 });
        TEST_CHECK(visibleCache.exists(1) && visibleCache.keys().size() == 1, "only the tile still used stays in the visible cache");
        TEST_CHECK(preloadingCache.size() <= preloadingCache.capacity(), "the released tiles respect the preloading bound");
    }

    void testAUsedPreloadedTileIsPromoted() {
        // A tile preloaded earlier comes into use: it moves, it is not duplicated or refetched.
        Cache visibleCache(VISIBLE_CAPACITY), preloadingCache(PRELOADING_CAPACITY);
        preloadingCache.put(7, 0, 500);
        holdTilesInUse(visibleCache, preloadingCache, { 7 });
        TEST_CHECK(visibleCache.exists(7) && !preloadingCache.exists(7), "a used tile found in the preloading cache moves to the visible one");
        holdTilesInUse(visibleCache, preloadingCache, { 8 });
        TEST_CHECK(!visibleCache.exists(7) && preloadingCache.exists(7), "a tile no longer used goes back to the preloading cache");
        TEST_CHECK(!visibleCache.exists(8) && !preloadingCache.exists(8), "a used tile not fetched yet is left for the fetch, not invented");
    }

}

void testTileCacheHold() {
    testTheOldSplitRefetchesForever();
    testUsedTilesAreFetchedOnce();
    testUnusedTilesReturnToTheBoundedCache();
    testAUsedPreloadedTileIsPromoted();
}
