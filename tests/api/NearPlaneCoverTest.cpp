/*
 * mapbox's near-plane tile cover (all/native/layers/NearPlaneCover.h): the tiles under the frustum's
 * bottom edges that a building may rise from into the frame. What TileLayer does with them (the
 * frustum corners, the tile grid, the dedup against the cover) needs the renderer; the picture is a
 * device check.
 */

#include "layers/NearPlaneCover.h"

#include "TestCheck.h"

#include <algorithm>

using namespace massif;

namespace {
    bool has(const std::vector<std::pair<int, int> >& tiles, int x, int y) {
        return std::find(tiles.begin(), tiles.end(), std::make_pair(x, y)) != tiles.end();
    }
}

void testNearPlaneCover() {
    std::printf("  NearPlaneCover\n");

    cglib::vec2<double> ground;
    TEST_CHECK(NearPlaneCover::projectToGround(cglib::vec3<double>(10.5, 10.5, 1), cglib::vec3<double>(10.5, 14.5, -1), 0, ground), "a bottom edge coming down meets the ground");
    TEST_CHECK(std::abs(ground(0) - 10.5) < 1e-9 && std::abs(ground(1) - 12.5) < 1e-9, "halfway down, halfway along");
    TEST_CHECK(!NearPlaneCover::projectToGround(cglib::vec3<double>(0, 0, 1), cglib::vec3<double>(0, 4, 2), 0, ground), "an edge looking up never does");
    TEST_CHECK(!NearPlaneCover::projectToGround(cglib::vec3<double>(0, 0, -1), cglib::vec3<double>(0, 4, -2), 0, ground), "nor one already under the ground");

    std::vector<std::pair<int, int> > tiles;
    NearPlaneCover::edgeTiles(cglib::vec2<double>(10.5, 10.5), cglib::vec2<double>(10.5, 12.5), 64, tiles);
    TEST_CHECK(has(tiles, 10, 10) && has(tiles, 10, 11), "the tiles the edge runs over");
    TEST_CHECK(!has(tiles, 9, 11) && !has(tiles, 11, 11), "and not those beside it");
    TEST_CHECK(tiles.size() == 2, "each once, and only within the 3x3 around the near corner");

    tiles.clear();
    NearPlaneCover::edgeTiles(cglib::vec2<double>(10.5, 10.5), cglib::vec2<double>(10.5, 40.5), 64, tiles);
    TEST_CHECK(!has(tiles, 10, 12) && has(tiles, 10, 11), "however long the edge: the rest is the cover's own");

    tiles.clear();
    NearPlaneCover::edgeTiles(cglib::vec2<double>(0.5, 0.5), cglib::vec2<double>(-3.5, 0.5), 64, tiles);
    TEST_CHECK(tiles.size() == 1 && has(tiles, 0, 0), "nothing off the grid");
}
