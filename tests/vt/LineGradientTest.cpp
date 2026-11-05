/*
 * What a gradient line carries to the GPU (TileLayerBuilder::createLineProgressProcessor): per
 * vertex, how far along the source line it is. A tile holds a piece of the line, so the piece's
 * own range comes in and every vertex, joins and caps included, lands inside it in proportion to
 * how far along the piece it is.
 *
 * NOT covered here: the ramp lookup and the shader (device checks).
 */

#include "TileLayerBuilder.h"

#include "TestCheck.h"

#include <cmath>
#include <vector>

using namespace massif::vt;

namespace {
    LineStyle gradientStyle(std::shared_ptr<const ColorRamp> ramp, LineCapMode capMode = LineCapMode::NONE) {
        LineStyle style(CompOp::SRC_OVER, LineJoinMode::MITER, capMode, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), FloatFunction(0.0f), -0.95f, 0.1f, std::shared_ptr<const BitmapPattern>(), std::optional<Transform>());
        style.gradient = std::move(ramp);
        return style;
    }

    std::shared_ptr<TileLayerBuilder> makeBuilder() {
        return std::make_shared<TileLayerBuilder>("test", 0, TileId(0, 0, 0), std::make_shared<DefaultTileTransformer>(1.0f), 256.0f, 1.0f);
    }

    struct Vertex {
        cglib::vec2<float> position; // tile units, y up
        float progress;
    };

    std::vector<Vertex> vertices(const TileGeometry& geometry) {
        const TileGeometry::VertexGeometryLayoutParameters& layout = geometry.getVertexGeometryLayoutParameters();
        std::vector<Vertex> result;
        for (std::size_t i = 0; i < geometry.getVertexGeometry().size() / layout.vertexSize; i++) {
            const std::uint8_t* vertex = geometry.getVertexGeometry().data() + i * layout.vertexSize;
            const std::int16_t* coord = reinterpret_cast<const std::int16_t*>(vertex + layout.coordOffset);
            float progress = 0;
            std::memcpy(&progress, vertex + layout.progressOffset, sizeof(float));
            result.push_back({ cglib::vec2<float>(coord[0] / layout.coordScale, 1.0f - coord[1] / layout.coordScale), progress });
        }
        return result;
    }

    // The vertices nearest a point on the line, which is where the join sits.
    std::vector<float> progressAt(const std::vector<Vertex>& all, const cglib::vec2<float>& point) {
        std::vector<float> result;
        for (const Vertex& vertex : all) {
            if (cglib::length(vertex.position - point) < 0.01f) {
                result.push_back(vertex.progress);
            }
        }
        return result;
    }

    bool allNear(const std::vector<float>& values, float expected) {
        for (float value : values) {
            if (std::abs(value - expected) > 0.002f) {
                return false;
            }
        }
        return !values.empty();
    }

    const std::vector<cglib::vec2<float>> BEND = { { 0.1f, 0.5f }, { 0.5f, 0.5f }, { 0.5f, 0.9f } };
}

void testLineGradient() {
    auto ramp = std::make_shared<ColorRamp>();

    {
        auto builder = makeBuilder();
        builder->createLineProgressProcessor(gradientStyle(ramp), std::shared_ptr<StrokeMap>())(1, BEND, LineProgress { 0.0, 1.0 });
        auto layer = builder->buildTileLayer();
        TEST_CHECK(layer->getGeometries().size() == 1 && layer->getGeometries()[0]->getVertexGeometryLayoutParameters().progressOffset >= 0, "a gradient line's vertices carry a progress field");
        TEST_CHECK(layer->getGeometries()[0]->getStyleParameters().gradients[0] == ramp, "and its slot names the ramp");

        std::vector<Vertex> all = vertices(*layer->getGeometries()[0]);
        TEST_CHECK(allNear(progressAt(all, BEND[0]), 0.0f) && allNear(progressAt(all, BEND[2]), 1.0f), "the ends are at the ends of the range");
        TEST_CHECK(allNear(progressAt(all, BEND[1]), 0.5f), "the bend, halfway along, is halfway through it");
    }

    {
        auto builder = makeBuilder();
        builder->createLineProgressProcessor(gradientStyle(ramp), std::shared_ptr<StrokeMap>())(1, BEND, LineProgress { 0.25, 0.75 });
        std::vector<Vertex> all = vertices(*builder->buildTileLayer()->getGeometries()[0]);
        TEST_CHECK(allNear(progressAt(all, BEND[0]), 0.25f) && allNear(progressAt(all, BEND[1]), 0.5f) && allNear(progressAt(all, BEND[2]), 0.75f), "a piece of a line covers only its own range");
    }

    {
        auto builder = makeBuilder();
        builder->createLineProgressProcessor(gradientStyle(ramp, LineCapMode::ROUND), std::shared_ptr<StrokeMap>())(1, BEND, LineProgress { 0.25, 0.75 });
        std::vector<Vertex> all = vertices(*builder->buildTileLayer()->getGeometries()[0]);
        bool inside = true;
        for (const Vertex& vertex : all) {
            inside = inside && vertex.progress >= 0.25f - 0.002f && vertex.progress <= 0.75f + 0.002f;
        }
        TEST_CHECK(!all.empty() && inside, "round caps do not run the gradient past the ends of the range");
    }

    {
        auto builder = makeBuilder();
        builder->createLineProgressProcessor(gradientStyle(ramp), std::shared_ptr<StrokeMap>())(1, BEND, std::nullopt);
        std::vector<Vertex> all = vertices(*builder->buildTileLayer()->getGeometries()[0]);
        bool first = !all.empty();
        for (const Vertex& vertex : all) {
            first = first && vertex.progress == 0.0f;
        }
        TEST_CHECK(first, "a line with no range is drawn at the start of its gradient");
    }

    {
        auto builder = makeBuilder();
        auto plain = gradientStyle(nullptr);
        builder->createLineProcessor(plain, std::shared_ptr<StrokeMap>())(1, BEND);
        auto layer = builder->buildTileLayer();
        TEST_CHECK(layer->getGeometries().size() == 1 && layer->getGeometries()[0]->getVertexGeometryLayoutParameters().progressOffset < 0 && layer->getGeometries()[0]->getStyleParameters().gradients[0] == nullptr, "a plain line pays nothing for the field");
    }

    {
        auto builder = makeBuilder();
        auto other = std::make_shared<ColorRamp>();
        auto first = builder->createLineProgressProcessor(gradientStyle(ramp), std::shared_ptr<StrokeMap>());
        first(1, BEND, LineProgress { 0.0, 1.0 });
        builder->createLineProgressProcessor(gradientStyle(ramp), std::shared_ptr<StrokeMap>())(2, BEND, LineProgress { 0.0, 1.0 });
        builder->createLineProgressProcessor(gradientStyle(other), std::shared_ptr<StrokeMap>())(3, BEND, LineProgress { 0.0, 1.0 });
        builder->createLineProcessor(gradientStyle(nullptr), std::shared_ptr<StrokeMap>())(4, BEND);
        auto layer = builder->buildTileLayer();
        TEST_CHECK(layer->getGeometries().size() == 2, "plain lines and gradient lines are drawn from different geometries");
        const auto& gradients = layer->getGeometries()[0]->getStyleParameters();
        TEST_CHECK(gradients.parameterCount == 2 && gradients.gradients[0] == ramp && gradients.gradients[1] == other, "one ramp is one slot however many lines use it, a second ramp is a second slot");
    }
}
