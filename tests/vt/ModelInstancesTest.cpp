// A model rule's points become one instance list per model and style: in the vertex frame, with their
// feature id and MultiPoint index, and only the points the tile owns (half-open, as its neighbour
// places the rest).

#include "TileLayerBuilder.h"
#include "TileTransformer.h"

#include "TestCheck.h"

#include <cmath>
#include <vector>

using namespace massif::vt;

void testModelInstances() {
    auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
    TileLayerBuilder builder("trees", 0, TileId(14, 8501, 5845), transformer, 256.0f, 1.0f);
    auto oak = std::make_shared<Model>();
    auto pine = std::make_shared<Model>();
    ModelStyle oakStyle;
    oakStyle.model = oak;
    ModelStyle pineStyle = oakStyle;
    pineStyle.model = pine;

    auto oaks = builder.createModelProcessor(oakStyle);
    auto pines = builder.createModelProcessor(pineStyle);
    auto oaksAgain = builder.createModelProcessor(oakStyle);
    TEST_CHECK(oaks && pines && oaksAgain, "a style with a model builds a processor");
    TEST_CHECK(!builder.createModelProcessor(ModelStyle()), "a style without a model draws nothing");

    ModelInstance instance;
    instance.scale = 1.2f;
    oaks(5, cglib::vec2<float>(0.25f, 0.75f), instance, 0);
    oaksAgain(5, cglib::vec2<float>(0.5f, 0.5f), instance, 3);
    oaks(5, cglib::vec2<float>(1.0f, 0.5f), instance, 4);    // on the east edge: the neighbour's
    oaks(5, cglib::vec2<float>(-0.01f, 0.5f), instance, 5);  // in the buffer: the neighbour's
    pines(9, cglib::vec2<float>(0.0f, 0.0f), instance, 0);   // on the north-west corner: ours

    std::shared_ptr<TileLayer> layer = builder.buildTileLayer();
    TEST_CHECK(layer->getModels().size() == 2, "one instance list per model and style, however many processors fed it");
    if (layer->getModels().size() != 2) {
        return;
    }
    const TileModelInstances& oakInstances = *layer->getModels()[0];
    TEST_CHECK(oakInstances.getInstances().size() == 2, "points outside the half-open tile are left to the neighbour");
    TEST_CHECK(oakInstances.getGeoPosIndexes() == std::vector<int>({ 0, 3 }) && oakInstances.getIds() == std::vector<long long>({ 5, 5 }), "each instance keeps its feature id and MultiPoint index");
    const ModelInstance& first = oakInstances.getInstances()[0];
    TEST_CHECK(std::abs(first.position(0) - 0.25f) < 1e-6f && std::abs(first.position(1) - 0.25f) < 1e-6f, "the position is in the vertex frame, y up");
    TEST_CHECK(first.scale == 1.2f, "the instance's own values are kept");
    TEST_CHECK(layer->getFeatureCount() == 3, "instances count as features");
}
