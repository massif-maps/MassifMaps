/*
 * Tests for the terrain skirt drops (all/native/terrain/TerrainSkirts.h): how far below each edge of a drawn
 * cover tile a skirt hangs to close the gap to a neighbour drawn from other height data.
 *
 * The fields below are synthetic node grids, so the gap at every shared edge is known exactly. A hole only opens
 * where the two sides come from different data; the skirt hangs from the HIGHER side.
 *
 * NOT covered here: where the fields come from (ElevationTextureCache::getDrawnNodeField reads the uploaded node
 * bitmaps), the skirt mesh and its shader drop (vt GLTileRenderer), and the frame where the hole was. Those are
 * the on-screen check named in the PR.
 */

#include "terrain/TerrainSkirts.h"

#include <cmath>

using namespace massif;

#include "TestCheck.h"

namespace {

    const double WORLD = 1024.0;

    NodeFieldView flatField(const vt::TileId& tileId, double metres, long long source) {
        double extent = static_cast<double>(1 << tileId.zoom);
        NodeFieldView view;
        view.minX = (tileId.x / extent - 0.5) * WORLD;
        view.maxX = ((tileId.x + 1) / extent - 0.5) * WORLD;
        view.maxY = (0.5 - tileId.y / extent) * WORLD;
        view.minY = (0.5 - (tileId.y + 1) / extent) * WORLD;
        view.nodes = 8;
        view.source = source;
        view.height = [metres](int, int) { return metres; };
        return view;
    }

}

void testTerrainSkirts() {
    // Two same-level neighbours, west at 500 m and east at 300 m, from different data.
    const vt::TileId west(4, 7, 7), east(4, 8, 7);
    std::vector<vt::TileId> cover = { west, east };
    auto differentData = [&](const vt::TileId& tileId, NodeFieldView& view) {
        view = flatField(tileId, tileId == west ? 500.0 : 300.0, tileId.x);
        return true;
    };
    std::map<vt::TileId, cglib::vec4<float>> drops = TerrainSkirts::drops(cover, WORLD, differentData);
    TEST_CHECK(drops.count(west) == 1 && drops[west](1) > 200.0f, "the higher tile hangs a skirt from its shared edge, past the 200 m gap");
    TEST_CHECK(drops.count(west) == 1 && drops[west](0) == 0.0f && drops[west](2) == 0.0f && drops[west](3) == 0.0f,
               "and from no edge without a neighbour");
    TEST_CHECK(drops.count(east) == 0, "the lower tile hangs none: its skirt would sit under its own ground");

    // Same heights from one shared source: the shader samples one texture, so the edge is exact.
    auto sameData = [&](const vt::TileId& tileId, NodeFieldView& view) {
        view = flatField(tileId, tileId == west ? 500.0 : 300.0, 1);
        return true;
    };
    TEST_CHECK(TerrainSkirts::drops(cover, WORLD, sameData).empty(), "two tiles drawn from the same data get no skirt");

    // A coarse neighbour two levels up, south of a fine tile, lower by 50 m: found across levels.
    const vt::TileId fine(6, 32, 31), coarse(4, 8, 8);
    std::vector<vt::TileId> mixed = { fine, coarse };
    auto mixedData = [&](const vt::TileId& tileId, NodeFieldView& view) {
        view = flatField(tileId, tileId == fine ? 250.0 : 200.0, tileId.zoom);
        return true;
    };
    std::map<vt::TileId, cglib::vec4<float>> mixedDrops = TerrainSkirts::drops(mixed, WORLD, mixedData);
    TEST_CHECK(mixedDrops.count(fine) == 1 && mixedDrops[fine](2) > 50.0f, "a coarser neighbour across levels is found and bridged");

    // A gap under the threshold is no hole worth a wall.
    auto tinyGap = [&](const vt::TileId& tileId, NodeFieldView& view) {
        view = flatField(tileId, tileId == west ? 300.2 : 300.0, tileId.x);
        return true;
    };
    TEST_CHECK(TerrainSkirts::drops(cover, WORLD, tinyGap).empty(), "a sub-metre difference hangs no skirt");

    // A tile drawn with no elevation has no field: nothing to compare, no skirt either side.
    auto missing = [&](const vt::TileId& tileId, NodeFieldView& view) {
        if (tileId == east) {
            return false;
        }
        view = flatField(tileId, 500.0, 1);
        return true;
    };
    TEST_CHECK(TerrainSkirts::drops(cover, WORLD, missing).empty(), "a neighbour without a drawn field gets no skirt against it");

    // The sampler is bilinear between nodes, as the vertex shader reads them.
    NodeFieldView ramp = flatField(west, 0, 1);
    ramp.height = [](int i, int) { return i * 10.0; };
    double mid = ramp.sample(ramp.minX + (ramp.maxX - ramp.minX) * 0.5 / 8.0, ramp.minY);
    TEST_CHECK(std::abs(mid - 5.0) < 1.0e-9, "half a cell between two nodes reads half way");
}
