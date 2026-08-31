/*
 * The corner cutback that rounds an extrusion's VERTICAL edges (see vt/ExtrusionCorner.h). Header-
 * only maths, so it links without the tile builder. This is mapbox's fill_extrusion_bucket rule:
 * each wall stops short of a corner by radius * tan(halfAngle), and the wedge that opens is filled
 * from the same vertex columns.
 *
 * NOT covered here: the fill itself - which triangles close the wedge, and that their winding faces
 * out - is TileLayerBuilder::appendPolygon3DRing, which drags the whole tile builder in and is
 * device-verified instead.
 */

#include "ExtrusionCorner.h"

#include "TestCheck.h"

using namespace massif::vt;

namespace {
    float cutback(float px, float py, float qx, float qy, float rx, float ry, float radius) {
        return extrusionCornerCutback(cglib::vec2<float>(px, py), cglib::vec2<float>(qx, qy), cglib::vec2<float>(rx, ry), radius);
    }

    std::vector<cglib::vec2<float>> square(float x0, float y0, float side, bool ccw) {
        std::vector<cglib::vec2<float>> points = { { x0, y0 }, { x0 + side, y0 }, { x0 + side, y0 + side }, { x0, y0 + side } };
        if (!ccw) {
            std::reverse(points.begin(), points.end());
        }
        return points;
    }
}

void testExtrusionRingOrientation() {
    // The whole point of orienting rings: an edge normal must point out of the MATERIAL, which on
    // a hole means into the void. That only holds if the hole winds against its outer ring.
    TEST_CHECK(extrusionRingArea2(square(0, 0, 10, true)) > 0.0f, "a counter-clockwise ring has positive area");
    TEST_CHECK(extrusionRingArea2(square(0, 0, 10, false)) < 0.0f, "a clockwise ring has negative area");

    TEST_CHECK(!extrusionRingNeedsReverse(square(0, 0, 10, true), false), "a counter-clockwise outer ring is already oriented");
    TEST_CHECK(extrusionRingNeedsReverse(square(0, 0, 10, false), false), "a clockwise outer ring is reversed");
    TEST_CHECK(!extrusionRingNeedsReverse(square(2, 2, 6, false), true), "a clockwise hole is already oriented");
    // The case that made a courtyard's walls face inward, so they were culled and it was
    // see-through, and inset its roof ring the wrong way.
    TEST_CHECK(extrusionRingNeedsReverse(square(2, 2, 6, true), true), "a hole wound like its outer ring is reversed");

    // A degenerate ring has no orientation to fix, and reversing it would only churn.
    std::vector<cglib::vec2<float>> line = { { 0, 0 }, { 1, 0 } };
    TEST_CHECK(!extrusionRingNeedsReverse(line, false), "a ring with fewer than three points is left alone");

    // After orienting, an outer edge's normal points away from the ring's interior and a hole
    // edge's normal points into the hole - the same direction relative to the material.
    cglib::vec2<float> outer = extrusionEdgeNormal(cglib::vec2<float>(0, 0), cglib::vec2<float>(10, 0));
    TEST_CHECK(outer(1) < 0.0f, "an outer ring's edge normal points away from its interior");
    cglib::vec2<float> hole = extrusionEdgeNormal(cglib::vec2<float>(2, 2), cglib::vec2<float>(2, 8));
    TEST_CHECK(hole(0) > 0.0f, "an oriented hole's edge normal points into the hole");
}

void testExtrusionCorner() {
    // A right angle is tan(45) = 1, so the wall gives up exactly the radius. This is the case every
    // building footprint is mostly made of.
    TEST_CHECK(std::abs(cutback(0, 0, 10, 0, 10, 10, 1.0f) - 1.0f) < 1.0e-4f, "a right-angle corner gives up exactly the radius");

    // A COLLINEAR vertex is not a corner: tan(0) = 0, so nothing is cut and no wedge opens. OSM
    // footprints are full of these, and cutting at them would notch a straight wall for nothing.
    TEST_CHECK(cutback(0, 0, 10, 0, 20, 0, 1.0f) == 0.0f, "a collinear vertex is not cut back");

    // Two edges doubling back on each other have no bisector to round about.
    TEST_CHECK(cutback(0, 0, 10, 0, 0, 0, 1.0f) == 0.0f, "an edge that doubles back is not cut back");

    // The sharper the corner, the more of the wall the same radius eats - which is what the
    // one-third cap is there to stop.
    TEST_CHECK(cutback(0, 0, 100, 0, 0, 100, 1.0f) > cutback(0, 0, 100, 0, 100, 100, 1.0f), "a sharper corner eats more wall than a right angle");

    // Capped at a third of the SHORTER of the two edges, so the two ends of a short wall cannot
    // meet in the middle and invert it.
    TEST_CHECK(std::abs(cutback(0, 0, 3, 0, 3, 30, 100.0f) - 1.0f) < 1.0e-4f, "a huge radius is capped at a third of the shorter edge");
    TEST_CHECK(cutback(0, 0, 3, 0, 3, 3, 100.0f) * 2.0f <= 3.0f, "the two ends of a short wall cannot cross");

    // No radius, no chamfer - the property's 0 means off all the way down.
    TEST_CHECK(cutback(0, 0, 10, 0, 10, 10, 0.0f) == 0.0f, "no edge radius leaves the corner sharp");

    // A CONCAVE corner rounds too: a courtyard's inner corner is an edge like any other, and the
    // turn is the same size whichever way it turns.
    TEST_CHECK(std::abs(cutback(0, 0, 10, 0, 10, 10, 1.0f) - cutback(0, 0, 10, 0, 10, -10, 1.0f)) < 1.0e-4f, "a concave corner is cut back like its convex mirror");
}
