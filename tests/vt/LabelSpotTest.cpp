// The label spot (vt/LabelSpot.h): how commitLabelMaps knows a label with a new id is a POI already on screen.
// Massif hashes the shield text into the id, and a POI drawn icon-only below its label minzoom gets its name a
// tile level up: same point, new id, and it blinked out until the next placement pass.
// Not covered: the carry-over itself (GLTileRenderer), checked on the emulator with a slow zoom out.

#include "LabelSpot.h"

#include <cmath>

using namespace massif::vt;

#include "TestCheck.h"

namespace {
    // A z14 source vertex as two overzoomed tiles place it: through each tile's own float transform.
    cglib::vec3<double> placeThroughTile(double worldX, double worldY, int zoom, double worldSize) {
        double tileSize = worldSize / (1 << zoom);
        float u = static_cast<float>(worldX / tileSize - std::floor(worldX / tileSize));
        float v = static_cast<float>(worldY / tileSize - std::floor(worldY / tileSize));
        return cglib::vec3<double>((std::floor(worldX / tileSize) + u) * tileSize, (std::floor(worldY / tileSize) + v) * tileSize, 0);
    }

    // The same point read from a vector tile: snapped to the tile's 4096 grid, a different grid at each zoom.
    cglib::vec3<double> snapToTileGrid(double worldX, double worldY, int zoom, double worldSize) {
        double unit = worldSize / (1 << zoom) / 4096;
        return cglib::vec3<double>(std::round(worldX / unit) * unit, std::round(worldY / unit) * unit, 0);
    }
}

void testLabelSpot() {
    const double worldSize = 1 << 20;
    const double cell = worldSize / (1 << 22);
    const int layer = 17563904;
    double x = 351234.567891, y = 241987.654321;

    LabelSpot fromZ17 = calculateLabelSpot(layer, placeThroughTile(x, y, 17, worldSize), cell);
    LabelSpot fromZ16 = calculateLabelSpot(layer, placeThroughTile(x, y, 16, worldSize), cell);
    TEST_CHECK(!(fromZ17 < fromZ16) && !(fromZ16 < fromZ17), "one POI placed through a z16 and a z17 tile stands on one spot");

    LabelSpot neighbour = calculateLabelSpot(layer, placeThroughTile(x + 3 * cell, y, 17, worldSize), cell);
    TEST_CHECK(fromZ17 < neighbour || neighbour < fromZ17, "a POI a few cells away is another spot");

    LabelSpot otherLayer = calculateLabelSpot(layer + 1, placeThroughTile(x, y, 17, worldSize), cell);
    TEST_CHECK(fromZ17 < otherLayer || otherLayer < fromZ17, "another layer's label on the same point is not the same POI");
}

void testLabelSpotNear() {
    const double worldSize = 1 << 20;
    const double cell = worldSize / (1 << 22);
    const int layer = 17563904;
    double x = 351234.567891, y = 241987.654321;

    for (int offset = 0; offset < 8; offset++) {
        // A place label read from a z11 and a z12 tile stands a few metres apart: no id nor fine cell agrees.
        double nudge = offset * cell / 8;
        std::multimap<LabelSpot, int> spots;
        auto z11 = snapToTileGrid(x + nudge, y, 11, worldSize);
        auto z12 = snapToTileGrid(x + nudge, y, 12, worldSize);
        spots.emplace(calculateLabelSpot(layer, z11, cell), 1);
        auto it = findNearSpot(spots, layer, z12, cell);
        TEST_CHECK(it != spots.end() && it->second == 1, "one POI read from a z11 and a z12 tile is one spot");
    }

    std::multimap<LabelSpot, int> spots;
    spots.emplace(calculateLabelSpot(layer, cglib::vec3<double>(x, y, 0), cell), 1);
    TEST_CHECK(findNearSpot(spots, layer, cglib::vec3<double>(x + 3 * cell, y, 0), cell) == spots.end(), "a POI three cells away is another spot");
    TEST_CHECK(findNearSpot(spots, layer + 1, cglib::vec3<double>(x, y, 0), cell) == spots.end(), "another layer's label on the same point is not the same POI");

    spots.emplace(calculateLabelSpot(layer, cglib::vec3<double>(x + 0.9 * cell, y, 0), cell), 2);
    auto first = findNearSpot(spots, layer, cglib::vec3<double>(x, y, 0), cell);
    TEST_CHECK(first != spots.end() && first->second == 1, "the label in the same cell wins over a neighbour");
    spots.erase(first);
    auto second = findNearSpot(spots, layer, cglib::vec3<double>(x, y, 0), cell);
    TEST_CHECK(second != spots.end() && second->second == 2, "a taken spot is not given twice");
}
