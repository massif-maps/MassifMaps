/*
 * Tests for the drape stand-in cover (all/native/terrain/DrapeStandIn.h): which cached drape tiles
 * a leaf with no picture of its own shows until its bake lands.
 *
 * The case that matters is DEPTH. The search used to walk the tile tree two levels down, because
 * each level costs 4^depth lookups; a pinch out crossing three or more levels then found nothing,
 * the leaf fell through to the flat clear colour, and half the ground went blank for those frames -
 * measured on emulator-5556 as "9 of 17 surfaces drawn as a flat fill" at zoom 15.58. Searching the
 * cache instead has no depth limit at all, so the four-levels-down case below is the regression.
 *
 * The second rule is COARSEST WINS: a tile and its own children must never both be drawn. They are
 * separate surfaces at different tesselations over the same ground, so the finer one reads as
 * sitting slightly off the terrain.
 *
 * NOT covered here: that the entries are marked used (that is the cache's eviction, and it needs
 * GL), and that a descendant is rejected for missing elevation or an incomplete layer mask (both
 * are renderer state). Whether the ground actually stops flashing is a device check.
 */

#include "terrain/DrapeStandIn.h"

#include <algorithm>

using namespace massif;

#include "TestCheck.h"

namespace {

    using massif::vt::TileId;

    // The cache is keyed on (zoom, x, y), so its candidates always arrive coarsest first.
    std::vector<TileId> sorted(std::vector<TileId> tileIds) {
        std::sort(tileIds.begin(), tileIds.end(), [](const TileId& a, const TileId& b) { return a < b; });
        return tileIds;
    }

    std::vector<TileId> cover(const TileId& tileId, const std::vector<TileId>& candidates) {
        std::vector<TileId> result;
        for (std::size_t index : DrapeStandIn::coarsestCover(tileId, candidates)) {
            result.push_back(candidates[index]);
        }
        return result;
    }

    void testFourLevelsDownIsStillFound() {
        // THE regression: a pinch from z19 to z15 leaves the cache holding z19 tiles under a z15
        // leaf. The old two-level walk returned nothing here and the leaf was painted flat.
        TileId leaf(15, 16595, 11271);
        TileId deep(19, 16595 * 16 + 3, 11271 * 16 + 5);
        TEST_CHECK(leaf.covers(deep), "the z19 tile really is inside the z15 leaf");
        TEST_CHECK(cover(leaf, sorted({ deep })).size() == 1, "a descendant four levels down still stands in");
    }

    void testCoarsestWinsOverItsOwnChildren() {
        TileId leaf(15, 16595, 11271);
        TileId child(16, 33190, 22542);
        std::vector<TileId> candidates = sorted({ child, child.getChild(0, 0), child.getChild(1, 1) });
        std::vector<TileId> result = cover(leaf, candidates);
        TEST_CHECK(result.size() == 1, "a tile and its own children are not both drawn");
        TEST_CHECK(result.front() == child, "... and the one kept is the coarser");
    }

    void testSiblingsAreAllKept() {
        // Four children tile the leaf exactly: dropping any of them is a quarter of the ground
        // missing, which is the same hole by another route.
        TileId leaf(16, 33190, 22542);
        std::vector<TileId> candidates = sorted({ leaf.getChild(0, 0), leaf.getChild(1, 0), leaf.getChild(0, 1), leaf.getChild(1, 1) });
        TEST_CHECK(cover(leaf, candidates).size() == 4, "all four children of the leaf stand in together");
    }

    void testMixedDepthsCoverOnce() {
        // What the cache actually holds mid-pinch: one child baked, another only at its own
        // children's level. Both generations are needed, and neither overlaps the other.
        TileId leaf(16, 33190, 22542);
        TileId near = leaf.getChild(0, 0);
        TileId far = leaf.getChild(1, 1);
        std::vector<TileId> candidates = sorted({ near, far.getChild(0, 0), far.getChild(1, 0) });
        std::vector<TileId> result = cover(leaf, candidates);
        TEST_CHECK(result.size() == 3, "a coarse piece and two finer ones elsewhere all stand in");
    }

    void testOutsidersAndSelfAreRejected() {
        TileId leaf(16, 33190, 22542);
        TileId neighbour(17, (33191 << 1), 22542 << 1); // inside the leaf's NEIGHBOUR, not the leaf
        TEST_CHECK(cover(leaf, sorted({ neighbour })).empty(), "a tile outside the leaf never stands in for it");
        TEST_CHECK(cover(leaf, sorted({ leaf })).empty(), "the leaf is not its own stand-in");
        TEST_CHECK(cover(leaf, sorted({ leaf.getParent() })).empty(), "nor is an ancestor - that is the other path");
        TEST_CHECK(cover(leaf, {}).empty(), "an empty cache gives an empty cover, not a crash");
    }

    void testEmptyBakeIsNotAPicture() {
        TEST_CHECK(!DrapeStandIn::hasPicture(true, false, 0), "a bake with no layer in it does not show the ground");
        TEST_CHECK(DrapeStandIn::hasPicture(true, false, 1), "a bake with a layer in it does");
        TEST_CHECK(DrapeStandIn::hasPicture(false, true, 0), "a seed is the finer generation copied in: a picture");
        TEST_CHECK(!DrapeStandIn::hasPicture(false, false, 0), "a fresh texture holds another tile's picture");
    }

    void testEmptyBakeIsNeverComplete() {
        TEST_CHECK(!DrapeStandIn::isComplete(true, 0, 0), "nothing wanted, nothing baked: still stand in until content lands");
        TEST_CHECK(!DrapeStandIn::isComplete(true, 1, 0), "a wanted layer missing from an empty bake");
        TEST_CHECK(DrapeStandIn::isComplete(true, 1, 1), "every wanted layer baked");
        TEST_CHECK(DrapeStandIn::isComplete(true, 1, 3), "more baked than wanted is complete too");
        TEST_CHECK(!DrapeStandIn::isComplete(true, 3, 1), "one of two wanted layers missing");
        TEST_CHECK(!DrapeStandIn::isComplete(false, 0, 0), "an unbaked tile is not complete");
    }

}

void testDrapeStandIn() {
    testEmptyBakeIsNotAPicture();
    testEmptyBakeIsNeverComplete();
    testFourLevelsDownIsStillFound();
    testCoarsestWinsOverItsOwnChildren();
    testSiblingsAreAllKept();
    testMixedDepthsCoverOnce();
    testOutsidersAndSelfAreRejected();
}
