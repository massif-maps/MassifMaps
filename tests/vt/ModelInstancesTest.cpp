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
    oaks(5, 500, cglib::vec2<float>(0.25f, 0.75f), instance, 0);
    oaksAgain(5, 500, cglib::vec2<float>(0.5f, 0.5f), instance, 3);
    oaks(5, 500, cglib::vec2<float>(1.0f, 0.5f), instance, 4);    // on the east edge: the neighbour's
    oaks(5, 500, cglib::vec2<float>(-0.01f, 0.5f), instance, 5);  // in the buffer: the neighbour's
    pines(9, 900, cglib::vec2<float>(0.0f, 0.0f), instance, 0);   // on the north-west corner: ours

    std::shared_ptr<TileLayer> layer = builder.buildTileLayer();
    TEST_CHECK(layer->getModels().size() == 2, "one instance list per model and style, however many processors fed it");
    if (layer->getModels().size() != 2) {
        return;
    }
    const TileModelInstances& oakInstances = *layer->getModels()[0];
    TEST_CHECK(oakInstances.getInstances().size() == 2, "points outside the half-open tile are left to the neighbour");
    TEST_CHECK(oakInstances.getGeoPosIndexes() == std::vector<int>({ 0, 3 }) && oakInstances.getIds() == std::vector<long long>({ 5, 5 }) && oakInstances.getFeatureIds() == std::vector<long long>({ 500, 500 }), "each instance keeps its ids and MultiPoint index");
    const ModelInstance& first = oakInstances.getInstances()[0];
    TEST_CHECK(std::abs(first.position(0) - 0.25f) < 1e-6f && std::abs(first.position(1) - 0.25f) < 1e-6f, "the position is in the vertex frame, y up");
    TEST_CHECK(first.scale == 1.2f, "the instance's own values are kept");
    TEST_CHECK(layer->getFeatureCount() == 3, "instances count as features");
}

void testModelDensityReduction() {
    auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
    auto model = std::make_shared<Model>();
    auto countPlaced = [&](int zoom, bool reduce) {
        TileLayerBuilder builder("trees", 0, TileId(zoom, 0, 0), transformer, 256.0f, 1.0f);
        ModelStyle style;
        style.model = model;
        style.densityReduction = reduce;
        auto processor = builder.createModelProcessor(style);
        ModelStyle other = style;
        other.model = std::make_shared<Model>();
        auto otherProcessor = builder.createModelProcessor(other);
        processor(1, 1, cglib::vec2<float>(0.5f, 0.5f), ModelInstance(), 0);
        processor(1, 1, cglib::vec2<float>(0.503f, 0.503f), ModelInstance(), 1);
        otherProcessor(1, 1, cglib::vec2<float>(0.5002f, 0.5f), ModelInstance(), 2);
        processor(1, 1, cglib::vec2<float>(0.9f, 0.1f), ModelInstance(), 3);
        std::size_t count = 0;
        std::shared_ptr<TileLayer> layer = builder.buildTileLayer();
        for (const std::shared_ptr<TileModelInstances>& models : layer->getModels()) {
            count += models->getInstances().size();
        }
        return count;
    };
    TEST_CHECK(countPlaced(14, true) == 2, "points sharing a grid cell keep the first, whichever model rule placed it");
    TEST_CHECK(countPlaced(14, false) == 4, "model-allow-density-reduction: false keeps every point");
    TEST_CHECK(countPlaced(15, true) == 3, "a finer grid at z15, the first zoom past a z14 source: only the shared cell drops");
    TEST_CHECK(countPlaced(16, true) == 4, "no thinning further in, as mapbox past canonical + 1");
}

void testModelOverlap() {
    auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
    TileId tileId(18, 134000, 93000);
    float unitsPerMetre = transformer->createTileVertexTransformer(tileId)->calculateHeight(cglib::vec2<float>(0.5f, 0.5f), 1.0f);
    auto crown = std::make_shared<Model>();
    crown->bounds = cglib::bbox3<float>(cglib::vec3<float>(-4, -3, 0), cglib::vec3<float>(4, 3, 10)); // a 3 m disc
    auto countPlaced = [&](bool allowOverlap, float scale) {
        TileLayerBuilder builder("trees", 0, tileId, transformer, 256.0f, 1.0f);
        ModelStyle style;
        style.model = crown;
        style.allowOverlap = allowOverlap;
        auto processor = builder.createModelProcessor(style);
        ModelInstance instance;
        instance.scale = scale;
        auto east = [&](float metres) { return cglib::vec2<float>(0.5f + metres * unitsPerMetre, 0.5f); };
        processor(1, 1, east(0), instance, 0);
        processor(1, 1, east(5), instance, 1);  // crowns cross
        processor(1, 1, east(8.5f), instance, 2); // touches neither
        processor(1, 1, east(15), instance, 3);
        processor(2, 2, east(15), instance, 0); // another feature at a point taken
        processor(1, 1, east(15), instance, 3); // the same point again: a tower and its rotor
        std::size_t count = 0;
        std::shared_ptr<TileLayer> layer = builder.buildTileLayer();
        for (const std::shared_ptr<TileModelInstances>& models : layer->getModels()) {
            count += models->getInstances().size();
        }
        return count;
    };
    TEST_CHECK(countPlaced(false, 1.0f) == 4, "a crown crossing a placed one is dropped, unless it belongs to the same point");
    TEST_CHECK(countPlaced(true, 1.0f) == 6, "model-allow-overlap: true keeps every point");
    TEST_CHECK(countPlaced(false, 0.5f) == 5, "the disc follows the instance's scale");
}

void testModelOverlapAcrossZooms() {
    // Every tile cut from one source sees all its points (TileReader feeds a model rule unclipped),
    // so a child tile keeps exactly the trees its parent kept inside it.
    auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
    TileId parent(17, 67000, 46500), child(18, 134001, 93000);
    float unitsPerMetre = transformer->createTileVertexTransformer(parent)->calculateHeight(cglib::vec2<float>(0.5f, 0.5f), 1.0f);
    auto crown = std::make_shared<Model>();
    crown->bounds = cglib::bbox3<float>(cglib::vec3<float>(-4, -4, 0), cglib::vec3<float>(4, 4, 10));
    std::vector<cglib::vec2<float>> points; // parent frame: a jittered grid 5 m apart, crowns 8 m
    for (int i = 0; i < 400; i++) {
        float jx = static_cast<float>((i * 7919) % 100) / 100.0f, jy = static_cast<float>((i * 104729) % 100) / 100.0f;
        points.emplace_back((0.3f + (i % 20) * 5 * unitsPerMetre) + jx * 2 * unitsPerMetre, (0.3f + (i / 20) * 5 * unitsPerMetre) + jy * 2 * unitsPerMetre);
    }
    auto placed = [&](const TileId& tileId, float scale, float offsetX, float offsetY) {
        TileLayerBuilder builder("trees", 0, tileId, transformer, 256.0f, 1.0f);
        ModelStyle style;
        style.model = crown;
        auto processor = builder.createModelProcessor(style);
        for (std::size_t i = 0; i < points.size(); i++) {
            processor(1, 1, cglib::vec2<float>(points[i](0) * scale - offsetX, points[i](1) * scale - offsetY), ModelInstance(), static_cast<int>(i));
        }
        std::vector<int> kept;
        std::shared_ptr<TileLayer> layer = builder.buildTileLayer();
        for (const std::shared_ptr<TileModelInstances>& models : layer->getModels()) {
            kept = models->getGeoPosIndexes();
        }
        return kept;
    };
    std::vector<int> fromParent;
    for (int index : placed(parent, 1.0f, 0.0f, 0.0f)) {
        if (points[index](0) >= 0.5f && points[index](1) < 0.5f) {
            fromParent.push_back(index);
        }
    }
    std::vector<int> fromChild = placed(child, 2.0f, 1.0f, 0.0f);
    TEST_CHECK(fromParent.size() > 10 && fromChild == fromParent, "zooming in keeps the trees the parent tile placed");
}

void testModelInstancePick() {
    // A box 2 m wide, 4 m long towards the model's north, 3 m tall; scaled 2x, turned to face east
    cglib::bbox3<double> bounds(cglib::vec3<double>(-1, 0, 0), cglib::vec3<double>(1, 4, 3));
    ModelInstance instance;
    instance.scale = 2.0f;
    instance.rotation = 1.5707963f;
    cglib::vec3<double> anchor(10, 20, 0);
    double metres = 0.5; // world units per metre
    double t = 0;
    cglib::ray3<double> down(cglib::vec3<double>(13, 20, 100), cglib::vec3<double>(0, 0, -1));
    TEST_CHECK(intersectModelInstance(bounds, instance, anchor, metres, down, t) && std::abs(t - 97.0) < 1e-3, "a ray down onto the turned model hits its top, 3 m x 2 x 0.5 up");
    cglib::ray3<double> north(cglib::vec3<double>(10, 23, 100), cglib::vec3<double>(0, 0, -1));
    TEST_CHECK(!intersectModelInstance(bounds, instance, anchor, metres, north, t), "where the model would be unturned is empty");
    instance.zOffset = 10.0f;
    TEST_CHECK(intersectModelInstance(bounds, instance, anchor, metres, down, t) && std::abs(t - 92.0) < 1e-3, "the z offset lifts the box by its metres");

    auto rotor = std::make_shared<Model>();
    rotor->bounds = cglib::bbox3<float>(cglib::vec3<float>(-40, 2, -40), cglib::vec3<float>(40, 6, 40));
    ModelStyle spinning;
    spinning.model = rotor;
    spinning.spinSpeed = 1.0f;
    TEST_CHECK(modelPickBounds(spinning).max(1) > 40, "a spinning model is picked by every box its spin sweeps");
}
