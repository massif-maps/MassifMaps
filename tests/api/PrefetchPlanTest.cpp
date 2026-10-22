/*
 * Tests for the animated layer's prefetch budget (all/native/utils/PrefetchPlan.h). A prefetched
 * step that the tile cache cannot hold is evicted and fetched again, which is a stall on every
 * loop, so the cache has to be sized from the same numbers the prefetch is.
 */

#include "utils/PrefetchPlan.h"

using namespace massif;

#include "TestCheck.h"

void testPrefetchPlan() {
    const std::size_t tile = 540672, budget = 192u * 1024 * 1024, floor = 10u * 1024 * 1024;

    PrefetchPlan loop = PlanPrefetch(15, 18, 19, tile, budget, floor);
    TEST_CHECK(loop.frames == 18, "a normal view prefetches the whole loop");
    TEST_CHECK(loop.cacheBytes >= 15 * (loop.frames + 1) * tile, "and the cache holds every step of it");

    PrefetchPlan big = PlanPrefetch(60, 18, 19, tile, budget, floor);
    TEST_CHECK(big.frames < 18 && big.frames > 0, "a large view prefetches fewer steps");
    TEST_CHECK(big.cacheBytes <= budget + 60 * 2 * tile, "and stays inside the budget");

    TEST_CHECK(PlanPrefetch(15, 2, 19, tile, budget, floor).frames == 2, "a short window is honoured");
    TEST_CHECK(PlanPrefetch(15, 50, 5, tile, budget, floor).frames == 4, "a window longer than the loop is the rest of the loop");

    PrefetchPlan none = PlanPrefetch(0, 18, 19, tile, budget, floor);
    TEST_CHECK(none.frames == 0 && none.cacheBytes == floor, "nothing visible, nothing to prefetch");
    TEST_CHECK(PlanPrefetch(15, 18, 1, tile, budget, floor).frames == 0, "one step has no steps ahead");
    TEST_CHECK(PlanPrefetch(400, 18, 19, tile, budget, floor).frames == 0, "a view that cannot afford a window prefetches nothing");
}
