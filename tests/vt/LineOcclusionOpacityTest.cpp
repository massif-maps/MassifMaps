/*
 * line-occlusion-opacity (LineStyle::occlusionOpacity): a line that is drawn a second time, ghosted,
 * where 3D content hides it. Everything the renderer decides from is what the builder stamps on the
 * geometry, so that is what is checked here.
 *
 * What must hold:
 *  - UNSET stays unset all the way to the geometry and the layer flag stays false. A style that never
 *    asks pays nothing, and its lines keep draping, because isDrapeableGeometry reads this field.
 *  - A value is one value per geometry: two different ones cannot share a batch, two equal ones must.
 *  - A fill never joins a ghosted batch. Polygons otherwise ride in a line batch (line shader), and
 *    would be ghosted and kept out of the drape with it.
 *  - TileLayer::hasOcclusionGeometry is found wherever the geometry sits in the layer.
 *
 * NOT covered: the draw itself - GL_GREATER against the extrusions' depth, the drape bake skipping the
 * geometry (GLTileRenderer::isDrapeableGeometry), the matching terrain state. Those are a device check.
 */

#include "TileLayerBuilder.h"

#include "TestCheck.h"

#include <optional>
#include <vector>

using namespace massif::vt;

namespace {
    const TileId PARIS_Z16(16, 33193, 22545);

    LineStyle lineStyle(std::optional<float> occlusionOpacity) {
        LineStyle style(CompOp::SRC_OVER, LineJoinMode::MITER, LineCapMode::NONE, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), FloatFunction(0.0f), -0.95f, 0.0f, std::shared_ptr<const BitmapPattern>(), std::optional<Transform>());
        style.occlusionOpacity = occlusionOpacity;
        return style;
    }

    PolygonStyle fillStyle() {
        return PolygonStyle(CompOp::SRC_OVER, ColorFunction(Color(1, 1, 1, 1)), std::shared_ptr<const BitmapPattern>(), std::optional<Transform>());
    }

    // One entry per build step: a line with that opacity, or (nullopt) a fill.
    struct Step {
        bool fill = false;
        std::optional<float> occlusionOpacity;
    };

    std::shared_ptr<TileLayer> build(const std::vector<Step>& steps) {
        auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
        TileLayerBuilder builder("test", 0, PARIS_Z16, transformer, 256.0f, 1.0f);
        long long id = 1;
        for (const Step& step : steps) {
            if (step.fill) {
                TileLayerBuilder::PolygonProcessor processor = builder.createPolygonProcessor(fillStyle());
                TileLayerBuilder::VerticesList verticesList = { { { 0.1f, 0.1f }, { 0.4f, 0.1f }, { 0.4f, 0.4f }, { 0.1f, 0.4f } } };
                processor(id++, verticesList);
            } else {
                TileLayerBuilder::LineProcessor processor = builder.createLineProcessor(lineStyle(step.occlusionOpacity), std::shared_ptr<StrokeMap>());
                processor(id++, { { 0.1f, 0.5f }, { 0.6f, 0.5f }, { 0.6f, 0.8f } });
            }
        }
        return builder.buildTileLayer();
    }

    std::vector<std::optional<float>> opacitiesOf(const TileLayer& layer) {
        std::vector<std::optional<float>> opacities;
        for (const std::shared_ptr<TileGeometry>& geometry : layer.getGeometries()) {
            opacities.push_back(geometry->getStyleParameters().occlusionOpacity);
        }
        return opacities;
    }

    void testUnsetStaysUnset() {
        std::shared_ptr<TileLayer> layer = build({ { false, std::nullopt } });
        std::vector<std::optional<float>> opacities = opacitiesOf(*layer);
        TEST_CHECK(opacities.size() == 1, "one line, one geometry");
        TEST_CHECK(opacities.size() == 1 && !opacities[0].has_value(), "a line that states no occlusion opacity carries none");
        TEST_CHECK(!layer->hasOcclusionGeometry(), "and its layer is not flagged, so it drapes as it always did");
    }

    void testAValueReachesTheGeometry() {
        std::shared_ptr<TileLayer> layer = build({ { false, 0.35f } });
        std::vector<std::optional<float>> opacities = opacitiesOf(*layer);
        TEST_CHECK(opacities.size() == 1 && opacities[0].has_value(), "a line that states one carries it");
        TEST_CHECK(opacities.size() == 1 && opacities[0] == 0.35f, "at the value it stated");
        TEST_CHECK(layer->hasOcclusionGeometry(), "and flags its layer");
    }

    void testDifferentValuesDoNotShareABatch() {
        std::vector<std::optional<float>> opacities = opacitiesOf(*build({ { false, std::nullopt }, { false, 0.35f }, { false, 0.6f } }));
        TEST_CHECK(opacities.size() == 3, "unset, 0.35 and 0.6 are three geometries");
        TEST_CHECK(opacities.size() == 3 && !opacities[0] && opacities[1] == 0.35f && opacities[2] == 0.6f, "each keeps its own, in build order");
    }

    void testEqualValuesStillBatch() {
        TEST_CHECK(opacitiesOf(*build({ { false, 0.35f }, { false, 0.35f } })).size() == 1, "two rules at the same value share one geometry");
        TEST_CHECK(opacitiesOf(*build({ { false, std::nullopt }, { false, std::nullopt } })).size() == 1, "and so do two that both say nothing");
    }

    void testAFillNeverJoinsAGhostedBatch() {
        // The fill comes after the ghosted line: left to the line batch it would take its place.
        std::shared_ptr<TileLayer> layer = build({ { false, 0.35f }, { true, std::nullopt } });
        std::vector<std::optional<float>> opacities = opacitiesOf(*layer);
        TEST_CHECK(opacities.size() == 2, "a fill after a ghosted line is a geometry of its own");
        if (opacities.size() == 2) {
            TEST_CHECK(opacities[0] == 0.35f && !opacities[1].has_value(), "and it is not ghosted");
            TEST_CHECK(layer->getGeometries()[1]->getType() == TileGeometry::Type::POLYGON, "it stays a polygon");
        }
    }

    void testAGhostedLineDoesNotJoinAFillBatch() {
        std::vector<std::optional<float>> opacities = opacitiesOf(*build({ { true, std::nullopt }, { false, 0.35f } }));
        TEST_CHECK(opacities.size() == 2, "a ghosted line after a fill is a geometry of its own");
        if (opacities.size() == 2) {
            TEST_CHECK(!opacities[0].has_value() && opacities[1] == 0.35f, "the fill keeps draping, the line does not");
        }
    }

    void testTheFlagIsFoundAnywhereInTheLayer() {
        std::shared_ptr<TileLayer> layer = build({ { false, std::nullopt }, { true, std::nullopt }, { false, 0.35f }, { true, std::nullopt } });
        TEST_CHECK(layer->hasOcclusionGeometry(), "a ghosted line in the middle of the layer is found");
    }
}

void testLineOcclusionOpacity() {
    testUnsetStaysUnset();
    testAValueReachesTheGeometry();
    testDifferentValuesDoNotShareABatch();
    testEqualValuesStillBatch();
    testAFillNeverJoinsAGhostedBatch();
    testAGhostedLineDoesNotJoinAFillBatch();
    testTheFlagIsFoundAnywhereInTheLayer();
}
