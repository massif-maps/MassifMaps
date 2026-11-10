// When a layer retained for the cross-fade gives up its ground (vt/RenderTileBlend.h). A timer fade
// leaves a gap while the new tile is still fetching; holding until covered strands the layer forever
// when the new tile arrived without it.

#include "RenderTileBlend.h"

using namespace massif::vt;

#include "TestCheck.h"

void testRenderTileBlend() {
    // Fading both at once leaves coverage at blend + (1-blend)^2, a blink at every zoom step.
    TEST_CHECK(!retainedLayerFades(true, true, true, true), "a layer under a still-fading replacement holds");
    TEST_CHECK(!retainedLayerFades(true, false, false, false), "and holds off screen too");

    // Held ground: on screen, nothing active in the tile, and the tile not of the current set yet.
    TEST_CHECK(!retainedLayerFades(false, true, false, false), "the only thing painting held ground holds");

    TEST_CHECK(retainedLayerFades(false, true, true, false), "a tile that has its own content lets the old go");

    // Off screen the caller's blend delta is 1, so it goes in one step.
    TEST_CHECK(retainedLayerFades(false, false, false, false), "a tile that left the view fades");

    TEST_CHECK(retainedLayerFades(false, true, false, false, true), "held ground gives up a layer too coarse to stand in");
    TEST_CHECK(!retainedLayerFades(true, true, false, false, true), "but not while its replacement is still fading in");

    // A current-set tile that decoded without this layer is the answer, not a gap.
    TEST_CHECK(retainedLayerFades(false, true, false, true), "a current tile without the layer is an answer, not a gap");
}

void testRetainedLayerTooCoarse() {
    // A cold start renders once at the default camera, so a z1 tile is loaded when the camera lands at z15.8.
    TEST_CHECK(retainedLayerTooCoarse(1, 15.8f, 100.0, 120.0), "a world tile under a city view is no stand-in");
    TEST_CHECK(!retainedLayerTooCoarse(9, 15.8f, 100.0, 120.0), "the parent search's own depth still is");
    TEST_CHECK(retainedLayerTooCoarse(7, 15.8f, 100.0, 120.0), "one level past it is not");
    // The zoom asked of a tile falls with its distance: a horizon tile is coarse by design.
    TEST_CHECK(!retainedLayerTooCoarse(6, 15.8f, 120.0 * 16.0, 120.0), "a tile sixteen focus distances away wants about z11.8");
    TEST_CHECK(retainedLayerTooCoarse(1, 15.8f, 120.0 * 16.0, 120.0), "and a world tile is still too coarse there");
    // No focus distance known: never drop on a guess.
    TEST_CHECK(!retainedLayerTooCoarse(1, 15.8f, 100.0, 0.0), "an unset focus distance drops nothing");
}

// The cull worker rebuilds the render tiles every frame while the camera moves. A retained tile
// split into four child copies handed each layer to a tile overlapping them four times, and the
// next rebuild sixteen: 855 render layers became 1 049 538 in six rebuilds and the heap ran out.
#include <map>
#include <memory>
#include <vector>

#include "TileId.h"

namespace {
    struct FakeRenderLayer {
        std::shared_ptr<int> layer;
        TileId targetTileId = TileId(0, 0, 0);
        float blend = 0.0f;
    };
}

void testRetainedLayerDedupe() {
    std::shared_ptr<int> layer = std::make_shared<int>(7);
    TileId parent(17, 67620, 47050);

    // Each rebuild: the tile is split into its four children (mergeExistingRenderTile), and the parent
    // inherits from each copy it intersects (initializeRenderTile), keeping each layer's own target.
    std::multimap<int, FakeRenderLayer> layers;
    layers.insert({ 3, FakeRenderLayer { layer, parent, 0.5f } });
    for (int rebuild = 0; rebuild < 6; rebuild++) {
        std::multimap<int, FakeRenderLayer> inherited;
        for (int child = 0; child < 4; child++) {
            for (const auto& entry : layers) {
                FakeRenderLayer copy = entry.second;
                insertRetainedLayer(inherited, entry.first, std::move(copy));
            }
        }
        layers = std::move(inherited);
    }
    TEST_CHECK(layers.size() == 1, "a layer inherited from four copies of one tile is kept once, rebuild after rebuild");
    TEST_CHECK(layers.begin()->second.blend == 0.5f, "and keeps its blend");

    std::multimap<int, FakeRenderLayer> quarters;
    for (int child = 0; child < 4; child++) {
        TEST_CHECK(insertRetainedLayer(quarters, 3, FakeRenderLayer { layer, parent.getChild(child / 2, child % 2), 0.2f * child }),
                   "the same layer on a different target is a different quarter of the ground, kept");
    }
    TEST_CHECK(!insertRetainedLayer(quarters, 3, FakeRenderLayer { layer, parent.getChild(1, 1), 0.9f }), "a repeat is not added");
    float blend = 0.0f;
    for (const auto& entry : quarters) {
        if (entry.second.targetTileId == parent.getChild(1, 1)) {
            blend = entry.second.blend;
        }
    }
    TEST_CHECK(quarters.size() == 4 && blend == 0.9f, "but lifts the kept entry to the higher blend");
    TEST_CHECK(insertRetainedLayer(quarters, 3, FakeRenderLayer { std::make_shared<int>(7), parent, 0.0f }),
               "another layer object at the same index is its own entry");
}
