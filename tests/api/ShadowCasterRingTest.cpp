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

    bool contains(const std::vector<ShadowCasterRing::Tile>& tiles, int zoom, int x, int y) {
        for (const ShadowCasterRing::Tile& tile : tiles) {
            if (tile.zoom == zoom && tile.x == x && tile.y == y) {
                return true;
            }
        }
        return false;
    }

    void testSunwardTilesLieOnTheSunsSide() {
        // A building just past the edge on the sun's side throws its shadow into the view, one on the
        // other side throws it away: only the first kind is worth a fetch.
        std::vector<ShadowCasterRing::Tile> view = { { 17, 100, 100 } };
        std::vector<ShadowCasterRing::Tile> east = ShadowCasterRing::sunwardTiles(view, 1, 0, 16);
        TEST_CHECK(east.size() == 1 && contains(east, 17, 101, 100), "a sun along +x adds the +x neighbour alone");
        std::vector<ShadowCasterRing::Tile> diagonal = ShadowCasterRing::sunwardTiles(view, -1, 1, 16);
        TEST_CHECK(diagonal.size() == 3, "a diagonal sun adds the two sides and the corner");
        TEST_CHECK(contains(diagonal, 17, 99, 100) && contains(diagonal, 17, 100, 101) && contains(diagonal, 17, 99, 101), "... all on the sun's side");
        TEST_CHECK(ShadowCasterRing::sunwardTiles(view, 0, 0, 16).empty(), "a sun overhead adds nothing");
    }

    void testSunwardTilesNeverOverlapTheView() {
        // A neighbour already drawn, or under or over a drawn tile of another zoom, would cast its
        // buildings twice and cost a fetch for nothing.
        std::vector<ShadowCasterRing::Tile> view = { { 17, 100, 100 }, { 17, 101, 100 }, { 16, 51, 49 }, { 18, 204, 202 } };
        std::vector<ShadowCasterRing::Tile> casters = ShadowCasterRing::sunwardTiles(view, 1, 1, 16);
        TEST_CHECK(!contains(casters, 17, 101, 100), "a visible neighbour is not added again");
        TEST_CHECK(!contains(casters, 17, 102, 99), "a tile under a visible coarser tile is not added");
        TEST_CHECK(!contains(casters, 17, 102, 101), "a tile over a visible finer tile is not added");
        TEST_CHECK(contains(casters, 17, 100, 101), "a free neighbour still is");
    }

    void testSunwardTilesStartAtTheMinZoom() {
        // mapbox's SHADOWS_MIN_ZOOM_EXTRA_TILES: under it the extra requests outweigh the shadows.
        std::vector<ShadowCasterRing::Tile> view = { { 15, 50, 50 }, { 16, 102, 100 } };
        std::vector<ShadowCasterRing::Tile> casters = ShadowCasterRing::sunwardTiles(view, 1, 0, 16);
        TEST_CHECK(casters.size() == 1 && contains(casters, 16, 103, 100), "only tiles from the min zoom extend the cover");
        std::vector<ShadowCasterRing::Tile> edge = ShadowCasterRing::sunwardTiles({ { 16, 5, 0 } }, 0, -1, 16);
        TEST_CHECK(edge.empty(), "nothing is added past the pole row");
    }

}

void testShadowCasterRing() {
    testASmallCoverIsLeftAlone();
    testTheHorizonCoverIsBounded();
    testCoarseningHoldsTheSameGround();
    testItStopsAtZoomZero();
    testTheBoundaryIsNotOverIt();
    testASingleTileCoverCountsItsMargin();
    testSunwardTilesLieOnTheSunsSide();
    testSunwardTilesNeverOverlapTheView();
    testSunwardTilesStartAtTheMinZoom();
}
