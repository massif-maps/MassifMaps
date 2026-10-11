// The label spot (vt/LabelSpot.h): how commitLabelMaps knows a label with a new id is a POI already on screen.
// Massif hashes the shield text into the id, and a POI drawn icon-only below its label minzoom gets its name a
// tile level up: same point, new id, and it blinked out until the next placement pass.
// Not covered: the carry-over itself (GLTileRenderer), checked on the emulator with a slow zoom out.

#include "LabelSpot.h"

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
}

void testLabelSpot() {
    const double worldSize = 1 << 20;
    const double cell = worldSize / (1 << 28);
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
