// A draw-once group (hybrid's translucent roads) reaches the renderer as the tile layer's group name,
// and the renderer draws each run of one group twice, top layer first (vt/DrawOnceOrder.h).
// Not covered: the stencil passes themselves (GLTileRenderer, GL) - that is a render check.

#include "DrawOnceOrder.h"
#include "TileLayerBuilder.h"
#include "TileTransformer.h"

#include "TestCheck.h"

#include <string>
#include <vector>

using namespace massif::vt;

namespace {
    struct Layer {
        int index;
        std::string group;
    };

    // index + n(one) / c(ore) / r(im), e.g. "1n 3c 2c 3r 2r"
    std::string order(const std::vector<Layer>& layers) {
        std::string out;
        for (const auto& [layer, pass] : drawOnceSchedule(layers, [](const Layer& layer) { return layer.group; })) {
            out += (out.empty() ? "" : " ") + std::to_string(layer.index) + (pass == DrawOncePass::NONE ? "n" : pass == DrawOncePass::CORE ? "c" : "r");
        }
        return out;
    }
}

void testDrawOnce() {
    TEST_CHECK(order({ { 1, "" }, { 2, "" }, { 3, "" } }) == "1n 2n 3n", "layers outside any group draw once, in order");
    TEST_CHECK(order({ { 1, "" }, { 2, "road" }, { 3, "road" }, { 4, "road" }, { 5, "" } }) == "1n 4c 3c 2c 4r 3r 2r 5n",
               "a group draws all its cores then all its rims, top layer first, between untouched neighbours");
    TEST_CHECK(order({ { 1, "casing" }, { 2, "casing" }, { 3, "road" }, { 4, "road" } }) == "2c 1c 2r 1r 4c 3c 4r 3r",
               "two adjacent groups each run their own passes, the casing still under the fill");
    TEST_CHECK(order({ { 1, "road" }, { 2, "" }, { 3, "road" } }) == "1c 1r 2n 3c 3r",
               "one name split by another layer is two runs, not one reordered across it");
    TEST_CHECK(order({}).empty(), "no layers, nothing to do");

    auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
    TileLayerBuilder plain("road", 0, TileId(14, 8501, 5845), transformer, 256.0f, 1.0f);
    TEST_CHECK(plain.buildTileLayer()->getDrawOnceGroup().empty(), "a layer with no group stays plain");
    TileLayerBuilder grouped("road", 0, TileId(14, 8501, 5845), transformer, 256.0f, 1.0f);
    grouped.setDrawOnceGroup("road");
    TEST_CHECK(grouped.buildTileLayer()->getDrawOnceGroup() == "road", "the builder's group reaches the tile layer");
}
