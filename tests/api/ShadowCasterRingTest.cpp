/*
 * Tests for the shadow caster ring's bound (all/native/terrain/ShadowCasterRing.h).
 *
 * The ring's zoom is set by the THROW - relief / tan(sun altitude) - so that a fixed number of
 * margin tiles spans however far a shadow reaches. Over FLAT ground the relief is 0, the throw is
 * 0, and that rule leaves the ring at the cover's finest zoom. A tilted cover reaches the horizon
 * and mixes zooms, so its footprint expressed at that zoom is thousands of tiles a side, and the
 * candidate grid is built before a single candidate is looked at: measured over Paris at z17-19
 * tilt 45 with shadows on, a std::vector<TileId> that grew to 1.5 GB and an out-of-memory kill at
 * 2.9 GB RSS. That is the case these pin.
 *
 * NOT covered here: the quadtree subdivision that brings the resolution back where the cover is
 * finer, and the caster set's own MAX_SHADOW_CASTER_TILES ceiling. Both live in
 * MapRenderer::applyTerrainShadows and need the renderer. Nor is the visual consequence of
 * coarsening - a distant shadow cast from a coarser DEM level - which is a device check.
 */

#include "terrain/ShadowCasterRing.h"

using namespace massif;

#include "TestCheck.h"

namespace {

    // MapRenderer::MAX_SHADOW_CASTER_TILES and LightOptions' default shadowCasterMargin.
    const std::size_t MAX_TILES = 2048;
    const int MARGIN = 3;

    ShadowCasterRing::Grid gridOf(int zoom, int minX, int minY, int maxX, int maxY) {
        ShadowCasterRing::Grid grid;
        grid.zoom = zoom;
        grid.minX = minX; grid.minY = minY;
        grid.maxX = maxX; grid.maxY = maxY;
        return grid;
    }

    void testASmallCoverIsLeftAlone() {
        // The ordinary case: a handful of tiles, already far inside the ceiling. Coarsening here
        // would throw away the ring's resolution for nothing.
        ShadowCasterRing::Grid grid = gridOf(17, 66000, 45000, 66007, 45006);
        ShadowCasterRing::Grid fitted = ShadowCasterRing::fit(grid, MARGIN, MAX_TILES);
        TEST_CHECK(fitted.zoom == 17, "a cover that already fits keeps its zoom");
        TEST_CHECK(fitted.minX == grid.minX && fitted.maxY == grid.maxY, "... and its footprint");
    }

    void testTheHorizonCoverIsBounded() {
        // The kill. A z19 cover whose far tiles reach the horizon: 4096 tiles a side is 16.7 M
        // candidates, 200 MB of TileId at the first allocation and gigabytes as the vector doubles.
        ShadowCasterRing::Grid grid = gridOf(19, 266000, 180000, 270095, 184095);
        TEST_CHECK(ShadowCasterRing::tileCount(grid, MARGIN) > 16000000u, "the unbounded grid really is that big");
        ShadowCasterRing::Grid fitted = ShadowCasterRing::fit(grid, MARGIN, MAX_TILES);
        TEST_CHECK(ShadowCasterRing::tileCount(fitted, MARGIN) <= MAX_TILES, "the fitted grid is inside the ceiling");
        TEST_CHECK(fitted.zoom < grid.zoom, "... which it reached by coarsening, not by cropping");
    }

    void testCoarseningHoldsTheSameGround() {
        // Coarsening drops the RESOLUTION, not the reach: every tile of the original footprint is
        // still under a tile of the fitted one, or a mountain off one edge stops casting.
        ShadowCasterRing::Grid grid = gridOf(19, 266000, 180000, 270095, 184095);
        ShadowCasterRing::Grid fitted = ShadowCasterRing::fit(grid, MARGIN, MAX_TILES);
        int shift = grid.zoom - fitted.zoom;
        TEST_CHECK(fitted.minX == (grid.minX >> shift) && fitted.minY == (grid.minY >> shift),
                   "the fitted footprint starts at the ancestor of the original's first tile");
        TEST_CHECK(fitted.maxX == (grid.maxX >> shift) && fitted.maxY == (grid.maxY >> shift),
                   "... and ends at the ancestor of its last, so nothing is cropped");
    }

    void testItStopsAtZoomZero() {
        // A margin large enough that no zoom satisfies the ceiling must stop at the top of the
        // pyramid rather than shift a negative zoom for ever.
        ShadowCasterRing::Grid grid = gridOf(19, 266000, 180000, 270095, 184095);
        ShadowCasterRing::Grid fitted = ShadowCasterRing::fit(grid, 4096, MAX_TILES);
        TEST_CHECK(fitted.zoom == 0, "an unsatisfiable ceiling clamps to zoom 0");
        TEST_CHECK(fitted.minX == 0 && fitted.maxX == 0, "... where the whole world is one tile");
    }

    void testTheBoundaryIsNotOverIt() {
        // Exactly at the ceiling is inside it: the comparison has to stay a strict `>`, or the ring
        // gives up a level it did not have to.
        ShadowCasterRing::Grid grid = gridOf(14, 8000, 5000, 8000 + 25, 5000 + 25); // 32 x 32 with the margin
        TEST_CHECK(ShadowCasterRing::tileCount(grid, MARGIN) == 1024u, "the case is the ceiling itself");
        TEST_CHECK(ShadowCasterRing::fit(grid, MARGIN, 1024).zoom == 14, "a grid exactly at the ceiling keeps its zoom");
        TEST_CHECK(ShadowCasterRing::fit(grid, MARGIN, 1023).zoom == 13, "... and one tile over it does not");
    }

    void testASingleTileCoverCountsItsMargin() {
        // The margin is the ring: one cover tile at margin 3 is a 7 x 7 ring, not one tile.
        ShadowCasterRing::Grid grid = gridOf(16, 33000, 22000, 33000, 22000);
        TEST_CHECK(ShadowCasterRing::tileCount(grid, MARGIN) == 49u, "one cover tile and a margin of 3 is 7 x 7");
        TEST_CHECK(ShadowCasterRing::tileCount(grid, 0) == 1u, "no margin is the cover tile alone");
    }

}

void testShadowCasterRing() {
    testASmallCoverIsLeftAlone();
    testTheHorizonCoverIsBounded();
    testCoarseningHoldsTheSameGround();
    testItStopsAtZoomZero();
    testTheBoundaryIsNotOverIt();
    testASingleTileCoverCountsItsMargin();
}
