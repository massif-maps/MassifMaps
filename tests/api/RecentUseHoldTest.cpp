/*
 * Tests for the recent-use hold (all/native/terrain/RecentUseHold.h) that the DEM grid cache and the
 * elevation texture cache keep beside their size-bounded LRUs.
 *
 * A 3D view zoomed out over the Alps reads ~220 DEM grids a frame against a 192-grid LRU: every load
 * evicted a grid the view still drew, which was then reloaded and evicted the next, for minutes (web,
 * z9 tilt 35, 1920x1080: 2500-6400 reloads in 150 s; 0-53 with the hold). What the hold must get right:
 * a grid used lately is found after the LRU dropped it, and nothing is kept past the hold time, or the
 * cap is gone for good.
 *
 * NOT covered here: the two caches themselves. ElevationManager needs the data source, and
 * ElevationTextureCache the GL resource manager; neither links on the host (see ../README.md).
 */

#include "terrain/RecentUseHold.h"

#include <memory>

using namespace massif;

#include "TestCheck.h"

void testRecentUseHold() {
    using Clock = std::chrono::steady_clock;
    Clock::time_point t0 = Clock::now();
    Clock::time_point inside = t0 + RECENT_USE_HOLD_TIME / 2;
    Clock::time_point past = t0 + RECENT_USE_HOLD_TIME + std::chrono::milliseconds(1);

    TEST_CHECK(isRecentlyUsed(t0, t0), "a use this frame is recent");
    TEST_CHECK(isRecentlyUsed(t0, inside), "a use within the hold time is recent");
    TEST_CHECK(!isRecentlyUsed(t0, past), "a use older than the hold time is not");

    RecentUseHold<long long, std::shared_ptr<int> > hold;
    std::shared_ptr<int> grid = std::make_shared<int>(42);
    std::shared_ptr<int> found;
    TEST_CHECK(!hold.find(7, t0, found), "nothing is held before it is used");

    hold.use(7, grid, t0);
    TEST_CHECK(hold.find(7, inside, found) && found == grid, "a grid used lately is found, the same object");
    TEST_CHECK(!hold.find(8, inside, found), "another key is not");
    TEST_CHECK(!hold.find(7, past, found), "past the hold time it is no longer found");

    // A use refreshes the hold: a grid drawn every frame never expires.
    hold.use(7, grid, inside);
    TEST_CHECK(hold.find(7, past, found), "a later use extends the hold");

    hold.use(9, std::make_shared<int>(1), t0);
    hold.expire(past);
    TEST_CHECK(hold.size() == 1, "expire drops what was not used within the hold time and keeps the rest");
    TEST_CHECK(hold.find(7, past, found) && !hold.find(9, past, found), "the refreshed grid stays, the stale one goes");

    std::weak_ptr<int> released = grid;
    grid.reset();
    found.reset();
    hold.expire(inside + RECENT_USE_HOLD_TIME * 2);
    TEST_CHECK(hold.size() == 0 && released.expired(), "an expired grid is released, not kept alive by the hold");

    hold.use(7, std::make_shared<int>(3), t0);
    hold.clear();
    TEST_CHECK(!hold.find(7, t0, found), "clear drops everything, as when the DEM itself changes");
}
