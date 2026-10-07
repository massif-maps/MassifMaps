/*
 * Tests for the drape cover partition (all/native/terrain/DrapeCoverLeaves.h): the leaves the terrain is
 * baked and drawn on, each fetching the DEM of its own level.
 *
 * The case that matters is a coarse stand-in split down to the view. A layer's first tiles are z1 parents
 * standing in while z12 loads; splitting a z1 down to z12 left three sibling leaves per level, almost all
 * off-screen. Measured on the Crosscall, terrain-3d at Innsbruck (z12.05, tilt 25): 50-100 leaves for the
 * first 5 s against 25 once settled, and a 2 x 2 DEM pyramid from z1 to z5. Maplibre's terrain tiles stop at
 * the frustum, so do these.
 *
 * NOT covered here: the frustum test itself (the tile bbox with its cached DEM height range) and the DEM
 * requests a leaf makes; both need the renderer. The fetch counts are a device check.
 */

#include "terrain/DrapeCoverLeaves.h"

#include <algorithm>

using namespace massif;

#include "TestCheck.h"

namespace {

    using massif::vt::TileId;

    std::set<TileId> ancestorsOf(const TileId& tile) {
        std::set<TileId> ancestors;
        for (TileId t = tile; t.zoom > 0; ) {
            t = t.getParent();
            ancestors.insert(t);
        }
        return ancestors;
    }

    // A unit world per root tile; the view is the open rectangle (minX, minY)-(maxX, maxY).
    auto viewRect(double minX, double minY, double maxX, double maxY) {
        return [=](const TileId& tile) {
            double size = 1.0 / (1 << tile.zoom);
            return tile.x * size < maxX && (tile.x + 1) * size > minX && tile.y * size < maxY && (tile.y + 1) * size > minY;
        };
    }

    bool contains(const std::vector<TileId>& tiles, const TileId& tile) {
        return std::find(tiles.begin(), tiles.end(), tile) != tiles.end();
    }

}

void testDrapeCoverLeaves() {
    const TileId stand(1, 0, 0);
    const TileId fine(4, 3, 5);
    std::set<TileId> ancestors = ancestorsOf(fine);

    {
        std::vector<TileId> leaves = buildDrapeCoverLeaves({ stand }, ancestors, 4, 256, viewRect(0, 0, 1, 1));
        TEST_CHECK(leaves.size() == 10, "with everything in view a z1 split down to z4 leaves three siblings per level");
        TEST_CHECK(contains(leaves, fine), "and the fine tile itself");
    }
    {
        // The view is inside the fine tile: z4 is 1/16 wide, the fine tile spans x 3/16..4/16, y 5/16..6/16.
        std::vector<TileId> leaves = buildDrapeCoverLeaves({ stand }, ancestors, 4, 256, viewRect(3.2 / 16, 5.2 / 16, 3.8 / 16, 5.8 / 16));
        TEST_CHECK(leaves.size() == 1 && leaves[0] == fine, "a sibling the camera cannot see is not a leaf");
    }
    {
        std::vector<TileId> leaves = buildDrapeCoverLeaves({ stand }, ancestors, 4, 256, [](const TileId&) { return false; });
        TEST_CHECK(leaves.empty(), "a top split with nothing in view leaves nothing");
    }
    {
        std::vector<TileId> leaves = buildDrapeCoverLeaves({ stand }, ancestors, 2, 256, viewRect(0, 0, 1, 1));
        TEST_CHECK(leaves.size() == 4 && !contains(leaves, fine), "the zoom limit stops the split");
    }
    {
        std::vector<TileId> leaves = buildDrapeCoverLeaves({ stand, TileId(1, 1, 0) }, ancestors, 4, 2, viewRect(0, 0, 1, 1));
        int maxZoom = 0;
        for (const TileId& leaf : leaves) {
            maxZoom = std::max(maxZoom, leaf.zoom);
        }
        TEST_CHECK(leaves.size() == 5 && maxZoom == 2, "over the tile cap the split stops where it is, what is left stays coarse");
    }
}
