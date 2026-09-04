/*
 * Tests for the extent the terrain surface refinement covers (all/native/terrain/TesselationBounds.h),
 * the rule TerrainTileTransformer::TerrainVertexTransformer::tesselateTriangle gates on.
 *
 * A source tile keeps a buffer of geometry around its own data, and at OVERZOOM that buffer scales
 * with everything else: a z14 source drawn into a z19 target reaches past the border by whole tile
 * widths, while the split threshold is the z19 one. The polygon gate upstream is an INTERSECTS
 * test, so one triangle touching the tile was refined across its whole extent - measured over Paris
 * at z19, an edge of 145 m against a 2.4 m threshold, which is 4096 triangles out of one and about
 * a thousand such triangles in one frame. That is a 2.5 GB process, not a slow frame.
 *
 * The case that matters is the BOUNDARY, in both directions. Refining too little is worse than the
 * bug: the split rule is edge-local so that two triangles sharing an edge always decide alike and
 * the surface carries no T-vertex, and a triangle that covers the tile has to be refined however
 * far outside its own vertices sit. A test that only checked "something far away is skipped" would
 * pass a rule that dropped the tile's own ground.
 *
 * NOT covered here: that the coarse triangles left outside the border really are clipped per
 * fragment, and that no crack opens at a real tile seam. Both are device checks - the Matterhorn at
 * a tilt is the camera they show up at, and it was clean there.
 */

#include "terrain/TesselationBounds.h"

using namespace massif;

#include "TestCheck.h"

namespace {

    /** The bbox of a triangle, which is all the rule sees. */
    cglib::bbox2<float> triangle(float x0, float y0, float x1, float y1, float x2, float y2) {
        cglib::bbox2<float> bounds(cglib::vec2<float>(x0, y0));
        bounds.add(cglib::vec2<float>(x1, y1));
        bounds.add(cglib::vec2<float>(x2, y2));
        return bounds;
    }

    void testTheTilesOwnGroundIsRefined() {
        TEST_CHECK(TesselationBounds::refines(triangle(0.1f, 0.1f, 0.6f, 0.1f, 0.1f, 0.6f)),
                   "a triangle over the tile is refined");
        TEST_CHECK(TesselationBounds::refines(triangle(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f)),
                   "one filling the tile exactly is refined");
    }

    void testGeometryPastTheBorderIsNot() {
        // The kill: an overzoomed source's buffer, two tile widths out. Nothing there is drawn.
        TEST_CHECK(!TesselationBounds::refines(triangle(2.1f, 2.1f, 2.6f, 2.1f, 2.1f, 2.6f)),
                   "a triangle two tiles away is not refined");
        TEST_CHECK(!TesselationBounds::refines(triangle(-3.0f, 0.2f, -2.0f, 0.2f, -2.0f, 0.8f)),
                   "... nor one off the other side");
    }

    void testATriangleSPANNINGTheTileIsRefined() {
        // OVERLAP, not containment. Every vertex is outside and the body still covers the tile -
        // exactly what a coarse overzoomed source produces. Dropping it would leave the tile's own
        // ground uncut, which is the sagging surface the refinement exists to prevent.
        TEST_CHECK(TesselationBounds::refines(triangle(-1.5f, -1.5f, 2.5f, -0.5f, -0.5f, 2.5f)),
                   "a triangle spanning the tile from outside it is still refined");
        TEST_CHECK(TesselationBounds::refines(triangle(-2.0f, 0.4f, 3.0f, 0.4f, 0.5f, 0.6f)),
                   "... and so is a sliver crossing it end to end");
    }

    void testTheMarginIsSmallButNotZero() {
        // A margin of 0 would leave a drape bake's outermost texels reading a coarse surface; a
        // generous one costs the square of itself. Both ends of 1/32 are pinned here.
        TEST_CHECK(TesselationBounds::MARGIN > 0.0f, "the margin is not zero");
        TEST_CHECK(TesselationBounds::MARGIN <= 1.0f / 16.0f, "... and not generous either");
        float inside = 1.0f + TesselationBounds::MARGIN * 0.5f;
        float outside = 1.0f + TesselationBounds::MARGIN * 2.0f;
        TEST_CHECK(TesselationBounds::refines(triangle(inside, inside, inside, inside, inside, inside)),
                   "a point inside the margin is refined");
        TEST_CHECK(!TesselationBounds::refines(triangle(outside, outside, outside, outside, outside, outside)),
                   "one past it is not");
    }

    void testTheBoxIsSymmetric() {
        // The margin applies on all four sides: a tile's north border is no different from its
        // south one, and an asymmetric box would refine one seam and not its neighbour's.
        cglib::bbox2<float> box = TesselationBounds::box();
        TEST_CHECK(box.min(0) == -TesselationBounds::MARGIN && box.min(1) == -TesselationBounds::MARGIN,
                   "the box starts one margin before the tile on both axes");
        TEST_CHECK(box.max(0) == 1 + TesselationBounds::MARGIN && box.max(1) == 1 + TesselationBounds::MARGIN,
                   "... and ends one margin after it");
    }

}

void testTerrainTesselation() {
    testTheTilesOwnGroundIsRefined();
    testGeometryPastTheBorderIsNot();
    testATriangleSPANNINGTheTileIsRefined();
    testTheMarginIsSmallButNotZero();
    testTheBoxIsSymmetric();
}
