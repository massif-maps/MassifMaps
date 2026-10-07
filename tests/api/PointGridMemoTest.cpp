/*
 * Tests for the point lookup memo (all/native/terrain/PointGridMemo.h) behind
 * ElevationManager::getGridForInternalPos, which every cached height read goes through: the terrain
 * focus, label anchors, the tile LOD.
 *
 * It used to keep whatever grid answered last. When a point with no fine data of its own was answered
 * by a coarse ancestor, the next point inside that ancestor - the focus, whose own fine grid WAS cached -
 * got the ancestor too, until the next DEM tile bumped the version. On web, rotating while tiles loaded
 * moved the camera up and down 41 times in one drag, the focus flipping between a z11 and a z8 grid.
 *
 * NOT covered here: ElevationManager itself (data source, grid cache) cannot be linked on the host
 * (see ../README.md); this covers the memo rule it delegates to.
 */

#include "terrain/PointGridMemo.h"
#include "core/MapBounds.h"

using namespace massif;

#include "TestCheck.h"

namespace {

    // Just what the memo reads off a grid: its tile and its bounds.
    struct FakeGrid {
        MapTile tile;
        MapBounds bounds;
        const MapTile& getTile() const { return tile; }
        const MapBounds& getInternalBounds() const { return bounds; }
    };

    enum class Mode { CACHED, LOAD };

    std::shared_ptr<FakeGrid> grid(int x, int y, int zoom, double minX, double minY, double maxX, double maxY) {
        return std::make_shared<FakeGrid>(FakeGrid { MapTile(x, y, zoom, 0), MapBounds(MapPos(minX, minY), MapPos(maxX, maxY)) });
    }

}

void testPointGridMemo() {
    // A z8 tile over [0, 8) x [0, 8) and one of its z11 descendants over [0, 1) x [0, 1).
    std::shared_ptr<FakeGrid> fine = grid(1024, 1024, 11, 0, 0, 1, 1);
    std::shared_ptr<FakeGrid> ancestor = grid(128, 128, 8, 0, 0, 8, 8);
    MapTile fineTile(1024, 1024, 11, 0);
    MapTile otherFineTile(1030, 1030, 11, 0);

    PointGridMemo<FakeGrid, Mode> memo;
    TEST_CHECK(!memo.find(1, 7, Mode::CACHED, 0.5, 0.5), "an empty memo answers nothing");

    memo.remember(1, 7, Mode::CACHED, fine, fineTile);
    TEST_CHECK(memo.find(1, 7, Mode::CACHED, 0.5, 0.5) == fine, "the looked-up tile's own grid answers the next point inside it");
    TEST_CHECK(!memo.find(1, 7, Mode::CACHED, 3.5, 3.5), "a point outside the remembered grid is looked up afresh");

    // The fault: a neighbouring point with no fine grid of its own, answered by the coarse ancestor.
    memo.remember(1, 7, Mode::CACHED, ancestor, otherFineTile);
    TEST_CHECK(memo.find(1, 7, Mode::CACHED, 0.5, 0.5) == fine, "an ancestor standing in for one point does not replace the fine grid of another");
    TEST_CHECK(!memo.find(1, 7, Mode::CACHED, 6.5, 6.5), "an ancestor standing in is never remembered, even for its own point");

    // An ancestor that IS the looked-up tile (the source has nothing finer) is an exact answer.
    memo.remember(1, 7, Mode::CACHED, ancestor, MapTile(128, 128, 8, 0));
    TEST_CHECK(memo.find(1, 7, Mode::CACHED, 6.5, 6.5) == ancestor, "a grid that is its own looked-up tile is remembered");

    TEST_CHECK(!memo.find(1, 8, Mode::CACHED, 6.5, 6.5), "a new grid cache version invalidates the memo");
    TEST_CHECK(!memo.find(2, 7, Mode::CACHED, 6.5, 6.5), "another manager's lookup never sees this one's grid");
    TEST_CHECK(!memo.find(1, 7, Mode::LOAD, 6.5, 6.5), "a lookup in another load mode does not reuse it");

    memo.remember(1, 7, Mode::CACHED, std::shared_ptr<FakeGrid>(), fineTile);
    TEST_CHECK(memo.find(1, 7, Mode::CACHED, 6.5, 6.5) == ancestor, "a miss leaves the remembered grid in place");
}
