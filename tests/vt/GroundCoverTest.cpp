// When the tiles paint all the ground in view, so the map can skip its background plane (vt/GroundCover.h).

#include "GroundCover.h"

#include <cglib/bbox.h>

using namespace massif::vt;

#include "TestCheck.h"

namespace {
    // A unit world per root tile, x to the east, y to the south; the view is an axis-aligned rectangle.
    cglib::bbox3<double> unitTileBBox(const TileId& tile) {
        double size = 1.0 / (1 << tile.zoom);
        return cglib::bbox3<double>(cglib::vec3<double>(tile.x * size, tile.y * size, 0), cglib::vec3<double>((tile.x + 1) * size, (tile.y + 1) * size, 0));
    }

    bool coversView(const std::unordered_set<TileId>& tiles, double minX, double minY, double maxX, double maxY) {
        return coversVisibleGround(tiles, unitTileBBox, [=](const cglib::bbox3<double>& bbox) {
            return bbox.min(0) < maxX && bbox.max(0) > minX && bbox.min(1) < maxY && bbox.max(1) > minY;
        });
    }

    std::unordered_set<TileId> zoom2Block(int x0, int y0, int x1, int y1) {
        std::unordered_set<TileId> tiles;
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                tiles.insert(TileId(2, x, y));
            }
        }
        return tiles;
    }
}

void testGroundCover() {
    // The view [0.3, 0.7]^2 needs zoom-2 tiles 1..2 in both axes.
    std::unordered_set<TileId> block = zoom2Block(1, 1, 2, 2);
    TEST_CHECK(coversView(block, 0.3, 0.3, 0.7, 0.7), "the tiles under the view cover it");
    TEST_CHECK(!coversView(block, 0.2, 0.3, 0.7, 0.7), "a view reaching past them is not covered");

    std::unordered_set<TileId> holed = block;
    holed.erase(TileId(2, 2, 1));
    TEST_CHECK(!coversView(holed, 0.3, 0.3, 0.7, 0.7), "a missing tile is a hole");

    std::unordered_set<TileId> parent = holed;
    parent.insert(TileId(1, 1, 0));
    TEST_CHECK(coversView(parent, 0.3, 0.3, 0.7, 0.7), "a coarser stand-in fills the hole");

    std::unordered_set<TileId> children = holed;
    for (int i = 0; i < 4; i++) {
        children.insert(TileId(2, 2, 1).getChild(i % 2, i / 2));
    }
    TEST_CHECK(coversView(children, 0.3, 0.3, 0.7, 0.7), "four finer tiles fill the hole");
    children.erase(TileId(3, 5, 3));
    TEST_CHECK(!coversView(children, 0.3, 0.3, 0.7, 0.7), "three of them do not");

    std::unordered_set<TileId> root = { TileId(0, 0, 0) };
    TEST_CHECK(coversView(root, 0.1, 0.1, 0.9, 0.9), "the root tile covers its world");
    TEST_CHECK(!coversView(root, 0.1, -0.2, 0.9, 0.9), "past the world's edge there is no ground");
    TEST_CHECK(!coversView(root, -0.2, 0.1, 0.9, 0.9), "nor past an unwrapped edge");

    std::unordered_set<TileId> wrapped = { TileId(0, 0, 0), TileId(1, -1, 0), TileId(1, -1, 1) };
    TEST_CHECK(coversView(wrapped, -0.4, 0.1, 0.9, 0.9), "a wrapped copy covers the world beside it");

    TEST_CHECK(!coversView(std::unordered_set<TileId>(), 0.3, 0.3, 0.7, 0.7), "no tiles cover nothing");

    // frustum3::inside is conservative: under tilt it reports a box far larger than the view as seen.
    bool loose = coversVisibleGround(block, unitTileBBox, [](const cglib::bbox3<double>& bbox) {
        bool large = bbox.max(0) - bbox.min(0) > 1.5 || bbox.max(1) - bbox.min(1) > 1.5;
        return large || (bbox.min(0) < 0.7 && bbox.max(0) > 0.3 && bbox.min(1) < 0.7 && bbox.max(1) > 0.3);
    });
    TEST_CHECK(loose, "the world's edge is tested at tile size, not as one box past it");
}
