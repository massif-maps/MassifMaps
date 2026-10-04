// A point drawn as tile geometry (marker-clip, point-*) keeps its index in the source MultiPoint, so a
// click on one tree of a merged MultiPoint reports that tree (VectorTileClickInfo::getFeaturePosIndex).

#include "TileLayerBuilder.h"
#include "TileTransformer.h"

#include "TestCheck.h"

#include <vector>

using namespace massif::vt;

void testPointGeoPosIndex() {
    auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
    TileLayerBuilder builder("trees", 0, TileId(14, 8501, 5845), transformer, 256.0f, 1.0f);
    auto bitmap = std::make_shared<Bitmap>(2, 2, std::vector<std::uint32_t>(4, 0xffffffff));
    PointStyle style(CompOp::SRC_OVER, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), std::make_shared<BitmapImage>(1.0f, bitmap), std::optional<Transform>());
    auto processor = builder.createPointProcessor(style, std::make_shared<GlyphMap>(256, 256));
    TEST_CHECK(static_cast<bool>(processor), "a point style with an image builds geometry");
    if (!processor) {
        return;
    }
    processor(1, cglib::vec2<float>(0.25f, 0.25f), 0);
    processor(1, cglib::vec2<float>(0.5f, 0.5f), 7);
    processor(1, cglib::vec2<float>(0.75f, 0.75f), 300);

    std::vector<int> indices;
    std::size_t vertexCount = 0;
    std::shared_ptr<TileLayer> layer = builder.buildTileLayer();
    for (const std::shared_ptr<TileGeometry>& geometry : layer->getGeometries()) {
        for (const auto& [count, index] : geometry->getGeoPosIndexes()) {
            indices.push_back(index);
            vertexCount += count;
        }
    }
    TEST_CHECK(indices == std::vector<int>({ 0, 7, 300 }), "each point's geometry carries the index it was given, not 0");
    TEST_CHECK(vertexCount > 3, "the index covers every vertex of the point's quad");
}
