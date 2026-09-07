/*
 * Tests for the DEM prefetch ordering metric (all/native/terrain/PrefetchOrder.h), the key
 * ElevationManager::runPrefetchWorker picks the next elevation tile to load by.
 *
 * With 3D terrain on, the queue used to drain newest-first with no distance term at all, so the
 * ground at the horizon was routinely fetched, decoded and meshed before the ground under the
 * camera. The metric is what fixes that, and it has two properties that are easy to lose:
 *
 * - It is measured in TILE WIDTHS at the tile's own zoom, not in mercator units, because the queue
 *   mixes levels. A coarse ancestor covering the focus and a fine tile sitting on it are both "the
 *   ground under the camera"; in raw mercator the ancestor's centre can be half a world of tiles
 *   away and would lose to a fine tile several tiles off to the side. The mixed-level check below
 *   is the one that fails if someone drops the `* extent`.
 * - u wraps and v does not. A view on the antimeridian queues tiles on both sides of it, and
 *   without the wrap they rank a whole world apart; mercator y has no such seam, so wrapping it too
 *   would make the arctic look near the antarctic.
 *
 * NOT covered here: the drain itself - that priority still beats distance, that the queue cap sheds
 * the lowest priority, and that a pan re-orders what is already queued. All three live in
 * ElevationManager, which needs the data source and the grid cache and cannot be linked on the host
 * (see ../README.md). They are a visual check - see the camera named in the PR.
 */

#include "terrain/PrefetchOrder.h"

using namespace massif;

#include "TestCheck.h"

namespace {

    // The centre of a tile, in the same normalised mercator the focus is given in.
    void tileCentre(int x, int y, int zoom, double& u, double& v) {
        double extent = static_cast<double>(1 << zoom);
        u = (x + 0.5) / extent;
        v = (y + 0.5) / extent;
    }

}

void testPrefetchOrder() {
    // The tile under the focus, its neighbour, and one four tiles away: the ordering the whole
    // change exists for.
    double u = 0, v = 0;
    tileCentre(8, 8, 4, u, v);
    double atFocus = prefetchTileDistance(MapTile(8, 8, 4, 0), u, v);
    double neighbour = prefetchTileDistance(MapTile(9, 8, 4, 0), u, v);
    double fourAway = prefetchTileDistance(MapTile(12, 8, 4, 0), u, v);
    TEST_CHECK(atFocus == 0.0, "the tile the focus sits in is at distance zero");
    TEST_CHECK(atFocus < neighbour && neighbour < fourAway, "queued tiles order by distance from the focus");

    // Tile widths, not mercator units: the neighbour is one tile away and the far one is four,
    // whatever the zoom. A metric in mercator units would give 1/16 and 4/16 here and something
    // else again at another level, which is what breaks the mixed-level case below.
    TEST_CHECK(std::abs(neighbour - 1.0) < 1.0e-9, "an edge neighbour is one tile width away");
    TEST_CHECK(std::abs(fourAway - 4.0) < 1.0e-9, "distance is in tile widths at the tile's own zoom");

    // Same distance at a different zoom, so the metric can compare two levels at all.
    double deepU = 0, deepV = 0;
    tileCentre(8192, 8192, 14, deepU, deepV);
    double deepNeighbour = prefetchTileDistance(MapTile(8193, 8192, 14, 0), deepU, deepV);
    TEST_CHECK(std::abs(deepNeighbour - neighbour) < 1.0e-9, "an edge neighbour is one tile width at z14 as at z4");

    // Mixed levels, the case the tile-width scaling exists for: a z10 ancestor CONTAINING the focus
    // against a z14 tile four tiles to the side. The ancestor is the ground under the camera and
    // must win. In raw mercator it does not - it sits 4.9e-4 from the focus against the fine tile's
    // 2.4e-4 - which is exactly the inversion this check pins down.
    double focusU = 0, focusV = 0;
    tileCentre(8192, 5461, 14, focusU, focusV);
    double ancestor = prefetchTileDistance(MapTile(8192 >> 4, 5461 >> 4, 10, 0), focusU, focusV);
    double sideways = prefetchTileDistance(MapTile(8196, 5461, 14, 0), focusU, focusV);
    TEST_CHECK(ancestor < sideways, "a coarse ancestor covering the focus beats a fine tile four tiles away");
    TEST_CHECK(ancestor < 0.7072, "a tile containing the focus is within half a tile diagonal of it");

    // The antimeridian. The focus is in column 0; column 15 is its WEST neighbour, one tile away,
    // not fifteen. Without the wrap the tiles behind the camera would be loaded first.
    double edgeU = 0, edgeV = 0;
    tileCentre(0, 8, 4, edgeU, edgeV);
    double acrossSeam = prefetchTileDistance(MapTile(15, 8, 4, 0), edgeU, edgeV);
    TEST_CHECK(std::abs(acrossSeam - 1.0) < 1.0e-9, "u wraps: the tile across the antimeridian is an edge neighbour");

    // ...but v must not, or the two poles would rank as neighbours.
    double northU = 0, northV = 0;
    tileCentre(8, 0, 4, northU, northV);
    double farSouth = prefetchTileDistance(MapTile(8, 15, 4, 0), northU, northV);
    TEST_CHECK(std::abs(farSouth - 15.0) < 1.0e-9, "v does not wrap: the bottom row stays far from the top row");

    // Symmetric in every direction, so nothing is skewed by the wrap arithmetic.
    tileCentre(8, 8, 4, u, v);
    double west = prefetchTileDistance(MapTile(7, 8, 4, 0), u, v);
    double north = prefetchTileDistance(MapTile(8, 7, 4, 0), u, v);
    double south = prefetchTileDistance(MapTile(8, 9, 4, 0), u, v);
    TEST_CHECK(std::abs(west - 1.0) < 1.0e-9 && std::abs(north - 1.0) < 1.0e-9 && std::abs(south - 1.0) < 1.0e-9,
        "all four edge neighbours are one tile width away");

    // A diagonal is further than an edge neighbour, which is why the drain still ranks them apart
    // by priority rather than leaving it to the distance.
    double diagonal = prefetchTileDistance(MapTile(9, 9, 4, 0), u, v);
    TEST_CHECK(diagonal > neighbour, "a diagonal neighbour is further than an edge neighbour");
}
