/*
 * Tests for the tile layers' cache split (all/native/layers/TileCacheHold.h): which tiles stay in
 * the visible cache and which go to the bounded preloading cache.
 *
 * The label band, the shadow casters and the preloading ring are fetched as preloading tiles, and
 * every cull asks for all of them again. Left in the 10 MB preloading cache, a set larger than 10 MB
 * can never be complete: each arrival evicts another member, which the next cull refetches. Measured
 * on the web build, Grenoble z15.5 tilt 45 with terrain shadows: 8 label tiles and 5 caster tiles of
 * 0.4-1.6 MB decoded, refetched in a loop for as long as the page stayed open.
 *
 * The scenario below replays those sizes. NOT covered here: the layer side - that FetchTask puts
 * every arrival in the visible cache and refreshDrawData counts every tile it hands over as used -
 * which needs the layers and the renderer.
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

    enum class Policy {
        MASTER,       // preloading kinds land in the preloading cache and only the view is held
        HOLD_LATE,    // every kind is held, but only once a refresh runs after the arrivals
        HOLD_ON_PUT   // every arrival lands in the visible cache, every kind is held
    };

    // Culls the same view repeatedly. The preloading cache starts nearly full of tiles an earlier
    // camera left behind, and every tile missing from a cull arrives before the next refresh, as a
    // slow cull on a fast network does. Returns the fetches per cull.
    std::vector<int> cullRepeatedly(const std::vector<Tile>& tiles, Policy policy, int culls, Cache& visibleCache, Cache& preloadingCache) {
        for (long long id = 1000; id < 1009; id++) {
            preloadingCache.put(id, 0, 1024);
        }
        std::unordered_set<long long> used;
        for (const Tile& tile : tiles) {
            if (!tile.preloading || policy != Policy::MASTER) {
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
                bool visible = !tile.preloading || policy == Policy::HOLD_ON_PUT;
                (visible ? visibleCache : preloadingCache).put(tile.id, 0, tile.size);
            }
            holdTilesInUse(visibleCache, preloadingCache, used);
            fetches.push_back(fetched);
        }
        return fetches;
    }

    int laterFetches(const std::vector<int>& fetches) {
        int later = 0;
        for (std::size_t i = 1; i < fetches.size(); i++) {
            later += fetches[i];
        }
        return later;
    }

    void testTheOldSplitRefetchesForever() {
        // Guards the scenario itself: if these sizes fit the preloading cache, the checks below prove nothing.
        Cache visibleCache(VISIBLE_CAPACITY), preloadingCache(PRELOADING_CAPACITY);
        std::vector<int> fetches = cullRepeatedly(measuredScene(), Policy::MASTER, 20, visibleCache, preloadingCache);
        TEST_CHECK(fetches.back() > 0, "label band + casters in the preloading cache: still refetching after 20 culls");
    }

    void testHoldingAloneLosesArrivals() {
        // Holding at refresh is not enough: arrivals between two refreshes land in the bounded cache
        // and push each other out before any refresh sees them. Seen on the iOS simulator.
        Cache visibleCache(VISIBLE_CAPACITY), preloadingCache(PRELOADING_CAPACITY);
        std::vector<int> fetches = cullRepeatedly(measuredScene(), Policy::HOLD_LATE, 20, visibleCache, preloadingCache);
        TEST_CHECK(laterFetches(fetches) > 0, "arrivals in the preloading cache are refetched although every kind is held");
    }

    void testUsedTilesAreFetchedOnce() {
        Cache visibleCache(VISIBLE_CAPACITY), preloadingCache(PRELOADING_CAPACITY);
        std::vector<Tile> tiles = measuredScene();
        std::vector<int> fetches = cullRepeatedly(tiles, Policy::HOLD_ON_PUT, 20, visibleCache, preloadingCache);
        TEST_CHECK(fetches.front() == static_cast<int>(tiles.size()), "the first cull fetches every tile");
        TEST_CHECK(laterFetches(fetches) == 0, "arriving in the visible cache and held there: no tile is fetched twice");
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
        cullRepeatedly(measuredScene(), Policy::HOLD_ON_PUT, 1, visibleCache, preloadingCache);
        holdTilesInUse(visibleCache, preloadingCache, { 1 });
        TEST_CHECK(visibleCache.exists(1) && visibleCache.keys().size() == 1, "only the tile still used stays in the visible cache");
        TEST_CHECK(preloadingCache.size() <= preloadingCache.capacity(), "the released tiles respect the preloading bound");
    }

    // A look-around: the view turns through `directions` sets of `tilesPerView` tiles and back to the first. Returns
    // how many of the first view's tiles have to be fetched again, with the preloading cache sized `capacityKB`.
    int refetchedAfterALookAround(std::size_t capacityKB, int directions, int tilesPerView, std::size_t tileKB) {
        Cache visibleCache(VISIBLE_CAPACITY), preloadingCache(capacityKB);
        auto view = [&](int direction) {
            std::unordered_set<long long> used;
            int fetched = 0;
            for (int i = 0; i < tilesPerView; i++) {
                long long id = direction * 1000 + i;
                used.insert(id);
                if (!visibleCache.exists(id) && !preloadingCache.exists(id)) {
                    visibleCache.put(id, 0, tileKB);
                    fetched++;
                }
            }
            holdTilesInUse(visibleCache, preloadingCache, used);
            return fetched;
        };
        for (int direction = 0; direction < directions; direction++) {
            view(direction);
        }
        return view(0);
    }

    void testTheViewportRuleIsMaplibres() {
        // 1200x800 with 256 px tiles: 6 x 5 tiles in view, five viewports of them, 256 KB each.
        std::size_t capacity = viewportCacheCapacity(30 * 256 * 1024, 30, 1200, 800, 256, 10 * 1024 * 1024);
        TEST_CHECK(capacity == 150u * 256 * 1024, "the out-of-view cache holds five viewports of tiles at the size of those in view");
        TEST_CHECK(viewportCacheCapacity(30 * 256 * 1024, 30, 1200, 800, 512, 0) == 4u * 3 * 5 * 256 * 1024, "larger tiles on screen, fewer of them in a view");
        TEST_CHECK(viewportCacheCapacity(1024, 1, 200, 200, 256, 10 * 1024 * 1024) == 10u * 1024 * 1024, "a small view keeps the old 10 MB as its floor");
        TEST_CHECK(viewportCacheCapacity(0, 0, 1200, 800, 256, 7) == 7u, "with nothing in view the floor stands");
    }

    void testALookAroundComesBackToItsTiles() {
        // The bug: turning round the Innsbruck view and back re-fetched every tile, 10 MB holding ~40 satellite tiles.
        const int directions = 4, tilesPerView = 30;
        const std::size_t tileKB = 256;
        int withTheOldCap = refetchedAfterALookAround(PRELOADING_CAPACITY, directions, tilesPerView, tileKB);
        std::size_t viewportKB = viewportCacheCapacity(tilesPerView * tileKB, tilesPerView, 1200, 800, 256, PRELOADING_CAPACITY);
        int withTheViewportRule = refetchedAfterALookAround(viewportKB, directions, tilesPerView, tileKB);
        TEST_CHECK(withTheOldCap == tilesPerView, "with a 10 MB cache, back to the first view every tile is fetched again");
        TEST_CHECK(withTheViewportRule == 0, "with the viewport rule, back to the first view nothing is fetched");
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
    testHoldingAloneLosesArrivals();
    testUsedTilesAreFetchedOnce();
    testUnusedTilesReturnToTheBoundedCache();
    testAUsedPreloadedTileIsPromoted();
    testTheViewportRuleIsMaplibres();
    testALookAroundComesBackToItsTiles();
}
