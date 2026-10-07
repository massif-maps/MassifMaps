/*
 * Tests for the shadow caster ring (all/native/terrain/ShadowCasterRing.h).
 *
 * The ring's zoom is set by the THROW - relief / tan(sun altitude) - so that a fixed number of
 * margin tiles spans however far a shadow reaches. It used to be the cover's bounding box at that
 * zoom: a tilted cover reaches the horizon and mixes zooms, so the box was thousands of tiles a side
 * (Paris z17-19 tilt 45: a 1.5 GB candidate vector and an out-of-memory kill), and once bounded by
 * coarsening still ~560 z10 tiles out to the horizon for a z9 view over the Alps, every one loading its
 * own DEM grid - most of the 450 DEM tiles that view fetched. The ring is now built per cover tile at
 * that tile's own zoom, or the ring zoom when coarser: its size follows the cover, not its box.
 *
 * NOT covered here: the quadtree subdivision that brings the resolution back where the cover is
 * finer, and the caster set's own MAX_SHADOW_CASTER_TILES ceiling. Both live in
 * MapRenderer::applyTerrainShadows and need the renderer. Nor the visual consequence - a distant
 * shadow cast from a coarser DEM level - which is a device check.
 */

#include "terrain/ShadowCasterRing.h"

using namespace massif;

#include "TestCheck.h"

namespace {

    // LightOptions' default shadowCasterMargin.
    const int MARGIN = 3;
    // A throw of a whole world: every ring reaches its full margin.
    const double FAR = 1.0;

    bool contains(const std::vector<ShadowCasterRing::Tile>& tiles, int zoom, int x, int y) {
        for (const ShadowCasterRing::Tile& tile : tiles) {
            if (tile.zoom == zoom && tile.x == x && tile.y == y) {
                return true;
            }
        }
        return false;
    }

    bool allAtZoom(const std::vector<ShadowCasterRing::Tile>& tiles, int zoom) {
        for (const ShadowCasterRing::Tile& tile : tiles) {
            if (tile.zoom != zoom) {
                return false;
            }
        }
        return !tiles.empty();
    }

    void testASingleTileRingIsItsMargin() {
        std::vector<ShadowCasterRing::Tile> ring = ShadowCasterRing::ringCandidates({ { 16, 33000, 22000 } }, 16, MARGIN, FAR);
        TEST_CHECK(ring.size() == 49u, "one cover tile and a margin of 3 is 7 x 7 candidates");
        TEST_CHECK(contains(ring, 16, 33000 - 3, 22000 - 3) && contains(ring, 16, 33000 + 3, 22000 + 3), "... reaching margin tiles each way");
        TEST_CHECK(ShadowCasterRing::ringCandidates({ { 16, 33000, 22000 } }, 16, 0, FAR).size() == 1u, "no margin is the cover tile alone");
    }

    void testFarCoarseGroundKeepsItsZoom() {
        // The fault: a far z7 tile got a ring at the near ground's zoom, hundreds of fine tiles.
        std::vector<ShadowCasterRing::Tile> ring = ShadowCasterRing::ringCandidates({ { 7, 66, 45 } }, 11, MARGIN, FAR);
        TEST_CHECK(ring.size() == 49u && allAtZoom(ring, 7), "a cover tile coarser than the ring zoom casts at its own zoom");
    }

    void testFineGroundCoarsensToTheThrow() {
        // The throw spans the margin at the ring zoom: a finer cover tile casts from there.
        std::vector<ShadowCasterRing::Tile> ring = ShadowCasterRing::ringCandidates({ { 16, 33000, 22000 } }, 12, MARGIN, FAR);
        TEST_CHECK(ring.size() == 49u && allAtZoom(ring, 12), "a cover tile finer than the ring zoom casts at the ring zoom");
        TEST_CHECK(contains(ring, 12, (33000 >> 4) - 3, (22000 >> 4) + 3), "... around its ancestor, margin tiles each way");
    }

    void testTheHorizonCoverIsBounded() {
        // A tilted cover: an 8 x 8 block of z19 tiles near, one z12 tile at the horizon. Their box at z19
        // is thousands of tiles a side; the ring is bounded by the cover tiles themselves.
        std::vector<ShadowCasterRing::Tile> cover;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                cover.push_back({ 19, 266000 + x, 180000 + y });
            }
        }
        cover.push_back({ 12, 2200, 1500 });
        std::vector<ShadowCasterRing::Tile> ring = ShadowCasterRing::ringCandidates(cover, 19, MARGIN, FAR);
        TEST_CHECK(ring.size() <= cover.size() * 49u, "the ring is at most the margin around each cover tile, not the box");
        TEST_CHECK(ring.size() == 14u * 14u + 49u, "the near block's ring is its block widened by the margin, without duplicates");
    }

    void testTheMarginFollowsTheThrow() {
        // 3 km of relief under a 42 degree sun throws ~3.3 km: one z11 tile (~14 km at 46N) already spans it,
        // and three were 40 km of casters, each loading its DEM.
        double throwFraction = 3300.0 / 27800000.0;
        std::vector<ShadowCasterRing::Tile> ring = ShadowCasterRing::ringCandidates({ { 11, 1066, 728 } }, 11, MARGIN, throwFraction);
        TEST_CHECK(ring.size() == 9u, "a throw under one tile is a ring of one tile");
        std::vector<ShadowCasterRing::Tile> fine = ShadowCasterRing::ringCandidates({ { 14, 8528, 5824 } }, 14, MARGIN, throwFraction);
        TEST_CHECK(fine.size() == 25u, "the same throw over smaller tiles takes more of them: 2 at z14");
        std::vector<ShadowCasterRing::Tile> flat = ShadowCasterRing::ringCandidates({ { 11, 1066, 728 } }, 11, MARGIN, 0.0);
        TEST_CHECK(flat.size() == 9u, "no throw still keeps one tile around the cover");
        std::vector<ShadowCasterRing::Tile> capped = ShadowCasterRing::ringCandidates({ { 18, 136448, 93184 } }, 18, MARGIN, throwFraction);
        TEST_CHECK(capped.size() == 49u, "the margin never exceeds the app's shadowCasterMargin");
    }

    void testTheRingStopsAtThePoles() {
        std::vector<ShadowCasterRing::Tile> ring = ShadowCasterRing::ringCandidates({ { 4, 5, 0 } }, 4, MARGIN, FAR);
        TEST_CHECK(ring.size() == 7u * 4u, "nothing is added past the pole row");
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
    testASingleTileRingIsItsMargin();
    testFarCoarseGroundKeepsItsZoom();
    testFineGroundCoarsensToTheThrow();
    testTheHorizonCoverIsBounded();
    testTheMarginFollowsTheThrow();
    testTheRingStopsAtThePoles();
    testSunwardTilesLieOnTheSunsSide();
    testSunwardTilesNeverOverlapTheView();
    testSunwardTilesStartAtTheMinZoom();
}
