/*
 * Tests for the drape-stack cut rule (all/native/terrain/DrapeStackCuts.h): where a live no-drape
 * style layer sits in the stack, how many occlusion masks that costs, and which live layer takes
 * which mask (#175).
 *
 * The table in the issue is what these cases are: contours between hillshade and roads need one
 * mask, contours last need none, a maneuver layer after the contours must not take the contours'
 * mask, and a pathological D L D L D needs two nested suffixes.
 *
 * NOT covered here: everything that reads the rule. The masks themselves are an R8 bake per drape
 * tile through GLTileRenderer::bakeDrapeCoverage, and whether a contour really lands under a road
 * on a slope is a device check - see docs/internals/rendering/04-terrain.md.
 */

#include "terrain/DrapeStackCuts.h"

using namespace massif;

#include "TestCheck.h"

namespace {

    // One drape layer, style layers numbered from 0 in the order given.
    std::vector<DrapeStackCuts::Unit> oneLayer(const std::vector<bool>& draped) {
        std::vector<DrapeStackCuts::Unit> units;
        for (std::size_t i = 0; i < draped.size(); i++) {
            units.push_back(DrapeStackCuts::Unit { 0, static_cast<int>(i), draped[i] });
        }
        return units;
    }

    void testNothingDrapedAfter() {
        // landcover, hillshade, roads, contours: the contours are already last, which is what the
        // style asked for. Zero masks, zero cost - the case every "contours on top" style is in.
        std::vector<DrapeStackCuts::Cut> cuts;
        std::vector<std::map<int, int> > layerMasks(1);
        bool capped = DrapeStackCuts::compute(oneLayer({ true, true, true, false }), 2, cuts, layerMasks);
        TEST_CHECK(!capped, "four units and one live one cannot hit the cap");
        TEST_CHECK(cuts.empty(), "a live layer with nothing draped after it needs no mask");
        TEST_CHECK(layerMasks[0].empty(), "... and is not in the mask map at all, so it draws unmasked");
    }

    void testOneCut() {
        // landcover, hillshade, contours, roads: one live->draped transition.
        std::vector<DrapeStackCuts::Cut> cuts;
        std::vector<std::map<int, int> > layerMasks(1);
        DrapeStackCuts::compute(oneLayer({ true, true, false, true }), 2, cuts, layerMasks);
        TEST_CHECK(cuts.size() == 1, "one live->draped transition is one mask");
        TEST_CHECK(cuts[0].layerIndex == 0 && cuts[0].styleLayerIdx == 3,
                   "the mask starts at the first draped unit AFTER the live one, not at the live one");
        TEST_CHECK(layerMasks[0].size() == 1 && layerMasks[0][2] == 0, "the contour layer takes mask 0");
    }

    void testLiveLayerAfterTheCutTakesNoMask() {
        // landcover, contours, roads, maneuver: the maneuver layer is last and must stay on top,
        // even though the contours below it are masked. One mask, and it is not the maneuver's.
        std::vector<DrapeStackCuts::Cut> cuts;
        std::vector<std::map<int, int> > layerMasks(1);
        DrapeStackCuts::compute(oneLayer({ true, false, true, false }), 2, cuts, layerMasks);
        TEST_CHECK(cuts.size() == 1, "only the contours have anything draped after them");
        TEST_CHECK(layerMasks[0].count(1) == 1 && layerMasks[0][1] == 0, "the contours are masked");
        TEST_CHECK(layerMasks[0].count(3) == 0, "the last layer is not masked - nothing draped follows it");
    }

    void testAdjacentLiveLayersShareOneMask() {
        // Two live layers with no draped unit between them see the same suffix, so they share a
        // mask rather than each costing one.
        std::vector<DrapeStackCuts::Cut> cuts;
        std::vector<std::map<int, int> > layerMasks(1);
        DrapeStackCuts::compute(oneLayer({ false, false, true }), 2, cuts, layerMasks);
        TEST_CHECK(cuts.size() == 1, "live units sharing a nearest draped unit share a mask");
        TEST_CHECK(layerMasks[0][0] == 0 && layerMasks[0][1] == 0, "... and both point at it");
    }

    void testNestedSuffixes() {
        // D L D L D - the pathological shape: two transitions, two nested suffix masks, and the
        // deeper live layer must take the LOWER cut (more content above it), not the higher one.
        std::vector<DrapeStackCuts::Cut> cuts;
        std::vector<std::map<int, int> > layerMasks(1);
        bool capped = DrapeStackCuts::compute(oneLayer({ true, false, true, false, true }), 2, cuts, layerMasks);
        TEST_CHECK(!capped, "two cuts fit a cap of two");
        TEST_CHECK(cuts.size() == 2, "two live->draped transitions are two masks");
        TEST_CHECK(layerMasks[0][3] == 0 && cuts[0].styleLayerIdx == 4,
                   "the upper live layer is occluded by the last draped unit only");
        TEST_CHECK(layerMasks[0][1] == 1 && cuts[1].styleLayerIdx == 2,
                   "the lower one is occluded from the draped unit right above it, so its mask nests the other");
    }

    void testCap() {
        // D L D L D L D needs three; with a cap of two the deepest cut is dropped and that layer
        // draws on top, as it did before the fix.
        std::vector<DrapeStackCuts::Cut> cuts;
        std::vector<std::map<int, int> > layerMasks(1);
        bool capped = DrapeStackCuts::compute(oneLayer({ true, false, true, false, true, false, true }), 2, cuts, layerMasks);
        TEST_CHECK(capped, "three transitions against a cap of two is reported");
        TEST_CHECK(cuts.size() == 2, "and never allocates more masks than the cap");
        TEST_CHECK(layerMasks[0].count(1) == 0, "the dropped cut's layer is unmasked, not mis-masked");
    }

    void testAcrossDrapeLayers() {
        // A standalone contour LAYER between a hillshade layer and the base map: the cut is at the
        // start of a later drape layer, which is the case MapRenderer alone can see.
        std::vector<DrapeStackCuts::Unit> units;
        units.push_back(DrapeStackCuts::Unit { 0, 0, true });  // hillshade layer
        units.push_back(DrapeStackCuts::Unit { 1, 0, false }); // contour layer, live
        units.push_back(DrapeStackCuts::Unit { 2, 5, true });  // base map, roads
        std::vector<DrapeStackCuts::Cut> cuts;
        std::vector<std::map<int, int> > layerMasks(3);
        DrapeStackCuts::compute(units, 2, cuts, layerMasks);
        TEST_CHECK(cuts.size() == 1 && cuts[0].layerIndex == 2 && cuts[0].styleLayerIdx == 5,
                   "the cut names the drape layer AND the style layer inside it");
        TEST_CHECK(layerMasks[1].size() == 1 && layerMasks[1][0] == 0, "the contour layer takes the mask");
        TEST_CHECK(layerMasks[0].empty() && layerMasks[2].empty(), "draped layers take none");
    }

    void testSignature() {
        std::vector<DrapeStackCuts::Cut> cuts1 { DrapeStackCuts::Cut { 0, 3 } };
        std::vector<DrapeStackCuts::Cut> cuts2 { DrapeStackCuts::Cut { 0, 4 } };
        TEST_CHECK(DrapeStackCuts::signature(cuts1) != DrapeStackCuts::signature(cuts2),
                   "moving a cut changes the signature, so the cached masks are re-baked");
        TEST_CHECK(DrapeStackCuts::signature(std::vector<DrapeStackCuts::Cut>()) == 0,
                   "no cuts is a zero signature, so a style with no live layer never disturbs the fingerprints");
    }

}

void testDrapeStackCuts() {
    testNothingDrapedAfter();
    testOneCut();
    testLiveLayerAfterTheCutTakesNoMask();
    testAdjacentLiveLayersShareOneMask();
    testNestedSuffixes();
    testCap();
    testAcrossDrapeLayers();
    testSignature();
}
