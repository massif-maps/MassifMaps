/*
 * Where the drawn ground under a building is read for its floor (ExtrusionFloor::floorPoints).
 *
 * The floor keeps a footprint's roof 2 m above the highest ground under it, so the points have to
 * cover the footprint's AREA, not only its outline. At Collioure the DEM holds the castle's walls as
 * terrain: their crest runs down the middle of each wall, the outline sits on its flanks, and a floor
 * read on the outline left the walls buried under their own hill. Every point must also lie ON the
 * footprint - 04-terrain.md records a bbox corner beside the Seine lifting a wing 5 m.
 */

#include "ExtrusionFloor.h"
#include "PolygonTesselator.h"

#include "TestCheck.h"

#include <cmath>
#include <vector>

using namespace massif::vt;

namespace {
    std::vector<cglib::vec2<float>> floorPoints(const std::vector<cglib::vec2<float>>& ring) {
        PolygonTesselator tesselator;
        std::vector<std::vector<cglib::vec2<float>>> rings { ring };
        tesselator.tesselate(rings);
        return ExtrusionFloor::floorPoints(rings, tesselator.getVertices(), tesselator.getElements());
    }
}

void testExtrusionFloor() {
    // A wall 40 long and 2 wide along x: its crest is the line y = 1, which no vertex is on.
    std::vector<cglib::vec2<float>> wall { { 0, 0 }, { 40, 0 }, { 40, 2 }, { 0, 2 } };
    bool onCrest = false;
    for (const cglib::vec2<float>& p : floorPoints(wall)) {
        onCrest = onCrest || (p(1) > 0.5f && p(1) < 1.5f);
    }
    TEST_CHECK(onCrest, "a thin wall's floor is read along its middle, not only on its flanks");

    // An L-shaped plan: its bounding box has a corner (10, 10) the building never reaches.
    std::vector<cglib::vec2<float>> ell { { 0, 0 }, { 10, 0 }, { 10, 4 }, { 4, 4 }, { 4, 10 }, { 0, 10 } };
    bool onPlan = true;
    for (const cglib::vec2<float>& p : floorPoints(ell)) {
        onPlan = onPlan && ((p(0) >= 0 && p(0) <= 10 && p(1) >= 0 && p(1) <= 4) || (p(0) >= 0 && p(0) <= 4 && p(1) >= 0 && p(1) <= 10));
    }
    TEST_CHECK(onPlan, "every point of an L-shaped plan lies on the plan - the Tuileries case");

    // The cost is per point per DEM arrival, so a detailed outline is thinned, evenly and the same way
    // in every tile.
    std::vector<cglib::vec2<float>> circle;
    for (int i = 0; i < 200; i++) {
        circle.emplace_back(10.0f * std::cos(i * 0.0314159f), 10.0f * std::sin(i * 0.0314159f));
    }
    std::vector<cglib::vec2<float>> thinned = floorPoints(circle);
    TEST_CHECK(thinned.size() == ExtrusionFloor::MAX_POINTS, "a 200-vertex footprint is read at MAX_POINTS points");
    TEST_CHECK(thinned == floorPoints(circle), "and at the same ones every time");
}
