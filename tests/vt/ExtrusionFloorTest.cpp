/*
 * Which few of a footprint's vertices the drawn ground under a building is read at.
 *
 * The base of a building is one plane, and the floor that keeps it out of its own hill is the MAX
 * ground over its footprint. Asking every rising vertex for that was 3 M elevation queries a second
 * while panning (performance-log 26); eight support points bound it instead.
 *
 * The property that matters is that a support point is ALWAYS A FOOTPRINT VERTEX. A bounding box is
 * the cheap alternative and it is the wrong one: 04-terrain.md records mapbox's corner-sampled lift
 * being reverted because a corner beside the Seine landed on the Tuileries terrace and lifted a
 * wing 5 m above its neighbour. Corners of a box are not on the building; support points are.
 */

#include "ExtrusionFloor.h"

#include "TestCheck.h"

#include <vector>

using namespace massif::vt;

namespace {

    /** The support point of a polygon along one direction, the way the vertex walk finds it. */
    std::pair<float, float> support(const std::vector<std::pair<float, float>>& ring, int direction) {
        std::pair<float, float> best = ring.front();
        float bestScore = ExtrusionFloor::supportScore(direction, best.first, best.second);
        for (const std::pair<float, float>& p : ring) {
            float score = ExtrusionFloor::supportScore(direction, p.first, p.second);
            if (score > bestScore) {
                bestScore = score;
                best = p;
            }
        }
        return best;
    }

    bool onRing(const std::vector<std::pair<float, float>>& ring, const std::pair<float, float>& p) {
        for (const std::pair<float, float>& q : ring) {
            if (q == p) {
                return true;
            }
        }
        return false;
    }

}

void testExtrusionFloor() {
    // An L-shaped plan: the reflex corner means its bounding box has a corner (10, 10) that is NOT
    // on the building at all - the case that sank the box model.
    std::vector<std::pair<float, float>> ell {
        { 0, 0 }, { 10, 0 }, { 10, 4 }, { 4, 4 }, { 4, 10 }, { 0, 10 }
    };
    for (int d = 0; d < ExtrusionFloor::SUPPORT_DIRECTIONS; d++) {
        TEST_CHECK(onRing(ell, support(ell, d)),
                   "every support point of an L-shaped plan is one of its own vertices");
    }
    TEST_CHECK(!onRing(ell, std::make_pair(10.0f, 10.0f)),
               "while its bounding box has a corner the building does not reach - the Tuileries case");

    // A building at 45 degrees is what the four axis directions alone would miss: its extremes are
    // the diagonal ones, and an axis-aligned box puts all four corners off the plan.
    std::vector<std::pair<float, float>> diamond { { 5, 0 }, { 10, 5 }, { 5, 10 }, { 0, 5 } };
    TEST_CHECK(support(diamond, 0) == std::make_pair(10.0f, 5.0f), "+x reaches the east point");
    TEST_CHECK(support(diamond, 2) == std::make_pair(5.0f, 10.0f), "+y reaches the north point");
    TEST_CHECK(support(diamond, 4) == std::make_pair(10.0f, 5.0f) ||
               support(diamond, 4) == std::make_pair(5.0f, 10.0f),
               "and the x+y diagonal reaches one of the two it lies between");
    for (int d = 0; d < ExtrusionFloor::SUPPORT_DIRECTIONS; d++) {
        TEST_CHECK(onRing(diamond, support(diamond, d)), "no direction leaves the plan");
    }

    // Opposed directions must not collapse onto the same vertex, or the floor would be read at one
    // end of the building and the far end could still be buried.
    TEST_CHECK(support(diamond, 0) != support(diamond, 1), "+x and -x find opposite ends");
    TEST_CHECK(support(diamond, 4) != support(diamond, 7), "and so do the opposed diagonals");

    // A footprint small enough to be one point extremises every direction there; the renderer
    // spots the repeat and asks the elevation source once rather than eight times.
    std::vector<std::pair<float, float>> point { { 3, 7 } };
    for (int d = 1; d < ExtrusionFloor::SUPPORT_DIRECTIONS; d++) {
        TEST_CHECK(support(point, d) == support(point, 0),
                   "a degenerate footprint gives every direction the same support point");
    }
}
