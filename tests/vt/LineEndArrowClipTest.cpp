/*
 * Where `line-end-arrow` lands on a line the tile source clipped (TileLayerBuilder::tesselateLine).
 *
 * A source cuts a line at its buffer, just outside the tile, and each piece's last vertex used to get
 * a head of its own: the maneuver-arrows gallery example showed extra heads along the shaft wherever
 * the arrow crossed a z17/z18 tile edge (GeoJSONVectorTileDataSource, buffer 4 px of 256). A head is
 * now drawn only where the line ends inside the tile, half-open like maplibre's symbol anchors.
 *
 * NOT covered here: the head's shape and the GL draw - device checks.
 */

#include "TileLayerBuilder.h"

#include "TestCheck.h"

#include <cstdint>
#include <vector>

using namespace massif::vt;

namespace {
    // The default GeoJSONVectorTileDataSource buffer, as a fraction of a tile.
    constexpr float SOURCE_BUFFER = 4.0f / 256.0f;

    LineStyle arrowStyle(bool arrowOnly, bool arrow = true) {
        return LineStyle(CompOp::SRC_OVER, LineJoinMode::MITER, LineCapMode::NONE, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), FloatFunction(0.0f), -0.95f, -0.875f, std::shared_ptr<const BitmapPattern>(), std::optional<Transform>(), arrow ? 2.4f : 0.0f, arrow ? 1.9f : 0.0f, arrowOnly);
    }

    struct Built {
        std::size_t indices = 0;
        std::vector<std::uint8_t> vertices;
    };

    Built build(const std::vector<cglib::vec2<float>>& points, const LineStyle& style) {
        auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
        TileLayerBuilder builder("test", 0, TileId(18, 132775, 90182), transformer, 256.0f, 1.0f);
        TileLayerBuilder::LineProcessor processor = builder.createLineProcessor(style, std::shared_ptr<StrokeMap>());
        processor(1, points);

        Built built;
        std::shared_ptr<TileLayer> layer = builder.buildTileLayer();
        for (const std::shared_ptr<TileGeometry>& geometry : layer->getGeometries()) {
            built.indices += geometry->getIndices().size();
            built.vertices.insert(built.vertices.end(), geometry->getVertexGeometry().begin(), geometry->getVertexGeometry().end());
        }
        return built;
    }

    // A maneuver-like bend ending at (endX, 0.5).
    std::vector<cglib::vec2<float>> bendEndingAt(float endX) {
        return { { 0.3f, 0.5f }, { 0.5f, 0.3f }, { endX, 0.5f } };
    }
}

void testLineEndArrowClip() {
    TEST_CHECK(build(bendEndingAt(0.7f), arrowStyle(true)).indices > 0, "a line ending inside the tile gets its head");
    TEST_CHECK(build(bendEndingAt(1.0f + SOURCE_BUFFER), arrowStyle(true)).indices == 0, "a piece cut at the source buffer past the right edge gets no head");
    TEST_CHECK(build({ { 0.7f, 0.5f }, { 0.5f, 0.3f }, { -SOURCE_BUFFER, 0.5f } }, arrowStyle(true)).indices == 0, "nor one cut past the left edge");
    TEST_CHECK(build({ { 0.5f, 0.7f }, { 0.5f, 0.5f }, { 0.5f, 1.0f + SOURCE_BUFFER } }, arrowStyle(true)).indices == 0, "nor one cut past the bottom edge");

    // An end exactly on a shared edge belongs to one of the two tiles, so it is drawn once.
    TEST_CHECK(build({ { 0.7f, 0.5f }, { 0.5f, 0.3f }, { 0.0f, 0.5f } }, arrowStyle(true)).indices > 0, "an end on the left edge is this tile's");
    TEST_CHECK(build(bendEndingAt(1.0f), arrowStyle(true)).indices == 0, "an end on the right edge is the next tile's");

    // A full line with a head pulls its last vertex back to the head's base; at a cut end it must
    // stay a plain line, or the shaft gets a gap and a stray head at every tile edge.
    Built cutWithArrow = build(bendEndingAt(1.0f + SOURCE_BUFFER), arrowStyle(false));
    Built cutPlain = build(bendEndingAt(1.0f + SOURCE_BUFFER), arrowStyle(false, false));
    TEST_CHECK(cutWithArrow.indices == cutPlain.indices && cutWithArrow.vertices == cutPlain.vertices, "a cut end of a line with a head tesselates like a plain line");
    Built ownWithArrow = build(bendEndingAt(0.7f), arrowStyle(false));
    Built ownPlain = build(bendEndingAt(0.7f), arrowStyle(false, false));
    TEST_CHECK(ownWithArrow.indices > ownPlain.indices, "its own end still gets the head");
}
