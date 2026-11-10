/*
 * Tests for which camera seeds join the drape cover (all/native/terrain/DrapeCoverSeeds.h).
 *
 * The case that matters is the cold start. The first layer reports a z1 tile that, split by the seeds, covers
 * the view; when it becomes a single z14 tile, or vector layers bring z16 tiles, the deepest collected zoom
 * jumps and every seed at or above it used to be dropped. The cover collapsed to 1-5 leaves and the terrain
 * outside them was not drawn (Crosscall cold start, 04-terrain.md).
 *
 * NOT covered here: collectTerrainCover itself and the leaf split it feeds (DrapeCoverLeavesTest); both need the
 * renderer. That the blink is gone is a device check (cold-start capture).
 */

#include "terrain/DrapeCoverSeeds.h"

#include <algorithm>

using namespace massif;

#include "TestCheck.h"

namespace {

    using massif::vt::TileId;

    std::map<TileId, std::size_t> collectedOf(const std::vector<TileId>& tiles) {
        std::map<TileId, std::size_t> collected;
        for (const TileId& tile : tiles) {
            collected[tile] = 0;
        }
        return collected;
    }

    bool contains(const std::vector<TileId>& tiles, const TileId& tile) {
        return std::find(tiles.begin(), tiles.end(), tile) != tiles.end();
    }

}

void testDrapeCoverSeeds() {
    const TileId collectedTile(14, 8452, 5880);
    const TileId sibling(14, 8453, 5880);
    const TileId far(13, 100, 200);
    const TileId parentOfCollected = collectedTile.getParent();
    const TileId childOfCollected = collectedTile.getChild(0, 0);
    const TileId deeper(15, 20000, 20000);

    std::map<TileId, std::size_t> collected = collectedOf({ collectedTile });
    {
        std::vector<TileId> seeds = DrapeCoverSeeds::select({ sibling, far }, collected, 14);
        TEST_CHECK(contains(seeds, sibling) && contains(seeds, far), "a seed at or above the data zoom that overlaps no collected tile is kept: it is ground nothing else covers");
    }
    {
        std::vector<TileId> seeds = DrapeCoverSeeds::select({ collectedTile, parentOfCollected, childOfCollected }, collected, 14);
        TEST_CHECK(contains(seeds, collectedTile) == false && contains(seeds, parentOfCollected) == false, "the collected tile and its ancestor are dropped as before: they would split the settled cover");
        TEST_CHECK(contains(seeds, childOfCollected), "a seed deeper than the data zoom is always kept, as before");
    }
    {
        std::vector<TileId> seeds = DrapeCoverSeeds::select({ deeper, TileId(15, 20001, 20000) }, collected, 14);
        TEST_CHECK(seeds.size() == 2, "deeper seeds all join, overlapping or not");
    }
    {
        std::map<TileId, std::size_t> root = collectedOf({ TileId(1, 0, 0) });
        std::vector<TileId> seeds = DrapeCoverSeeds::select({ TileId(13, 3, 3), TileId(13, 5000, 5000) }, root, 1);
        TEST_CHECK(seeds.size() == 2, "deeper than a z1 root, every seed joins");
        std::vector<TileId> flat = DrapeCoverSeeds::select({ TileId(1, 0, 0), TileId(1, 1, 0) }, root, 1);
        TEST_CHECK(flat.size() == 1 && flat[0] == TileId(1, 1, 0), "at the data zoom only the root's own seed is dropped, a neighbour is kept");
    }
    {
        std::vector<TileId> seeds = DrapeCoverSeeds::select({ sibling }, std::map<TileId, std::size_t>(), 14);
        TEST_CHECK(seeds.size() == 1, "nothing collected: the seed stays");
    }
}
