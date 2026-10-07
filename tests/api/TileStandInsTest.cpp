/*
 * Tests for the stand-in tiles (all/native/layers/TileStandIns.h): the coarse tiles a tile layer keeps
 * loaded under the view, maplibre-native's prefetchZoomDelta.
 *
 * Without them a tile turned into view has no parent in memory unless one happened to be there, and draws
 * blank for its whole load: web, terrain-3d at Innsbruck z12.05 tilt 25, a first 360 degree turn had a
 * blank tile in 86-107 of ~235 culls. What the rule must get right: few tiles (one per 4^delta of the view),
 * the same ancestor findParentTile walks to, and nothing finer than the tile or coarser than the source.
 *
 * NOT covered here: TileLayer fetching them at parent priority and refreshing on their arrival, which needs
 * the layer and the renderer.
 */

#include "layers/TileStandIns.h"

using namespace massif;

#include "TestCheck.h"

namespace {

    bool contains(const std::vector<MapTile>& tiles, int x, int y, int zoom) {
        for (const MapTile& tile : tiles) {
            if (tile.getX() == x && tile.getY() == y && tile.getZoom() == zoom) {
                return true;
            }
        }
        return false;
    }

}

void testTileStandIns() {
    std::vector<MapTile> one = calculateStandInTiles({ MapTile(8613, 5765, 14, 0) }, 4, 0);
    TEST_CHECK(one.size() == 1 && contains(one, 8613 >> 4, 5765 >> 4, 10), "a tile's stand-in is its ancestor four levels up");

    std::vector<MapTile> block;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            block.emplace_back(8608 + x, 5760 + y, 14, 0);
        }
    }
    TEST_CHECK(calculateStandInTiles(block, 4, 0).size() == 1u, "256 tiles under one ancestor share one stand-in");

    std::vector<MapTile> mixed = { MapTile(8613, 5765, 14, 0), MapTile(8614, 5765, 14, 0), MapTile(134, 90, 8, 0) };
    std::vector<MapTile> mixedStandIns = calculateStandInTiles(mixed, 4, 0);
    TEST_CHECK(mixedStandIns.size() == 2u && contains(mixedStandIns, 538, 360, 10) && contains(mixedStandIns, 8, 5, 4),
        "a mixed-zoom cover gets a stand-in per level, each four levels up from its own tile");

    std::vector<MapTile> clamped = calculateStandInTiles({ MapTile(20, 12, 5, 0) }, 4, 3);
    TEST_CHECK(clamped.size() == 1u && contains(clamped, 5, 3, 3), "never coarser than the source's minimum zoom");
    TEST_CHECK(calculateStandInTiles({ MapTile(5, 3, 3, 0) }, 4, 3).empty(), "a tile at the minimum zoom has no stand-in");

    std::vector<MapTile> wrapped = calculateStandInTiles({ MapTile(-1, 5, 4, 0) }, 4, 0);
    TEST_CHECK(wrapped.size() == 1u && contains(wrapped, 0, 0, 0), "a tile across the antimeridian maps into the world");
}
