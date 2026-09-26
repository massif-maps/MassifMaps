// Span / contact-shadow presence is answered once at decode time (vt/TileLayer.h): a per-frame search
// for their absence walks every geometry of every layer to the end.

#include "TileLayer.h"
#include "TileGeometry.h"

#include "TestCheck.h"

#include <cstring>

using namespace massif::vt;

namespace {
    std::shared_ptr<TileGeometry> makeGeometry(TileGeometry::Type type, bool withSpan) {
        TileGeometry::VertexGeometryLayoutParameters params;
        params.coordOffset = 0;
        params.vertexSize = 8;
        params.coordScale = 1.0f;

        VertexArray<std::uint8_t> vertexGeometry;
        vertexGeometry.fill(0, 3 * params.vertexSize);
        VertexArray<std::uint16_t> indices;
        for (std::size_t i = 0; i < 3; i++) {
            indices.append(static_cast<std::uint16_t>(i));
        }
        auto geometry = std::make_shared<TileGeometry>(type, 1.0f, TileGeometry::StyleParameters(), params,
                                                       std::move(vertexGeometry), std::move(indices),
                                                       std::vector<std::pair<std::size_t, long long>>(),
                                                       std::vector<std::pair<std::size_t, std::uint16_t>>());
        if (withSpan) {
            TileGeometry::SpanRecord record;
            record.vertexCount = 3;
            geometry->setSpanRecords({ record });
        }
        return geometry;
    }

    std::shared_ptr<TileLayer> makeLayer(std::vector<std::shared_ptr<TileGeometry>> geometries) {
        return std::make_shared<TileLayer>("layer", 0, std::optional<CompOp>(), FloatFunction(1.0f),
                                           std::vector<std::shared_ptr<TileBackground>>(),
                                           std::vector<std::shared_ptr<TileBitmap>>(),
                                           std::move(geometries),
                                           std::vector<std::shared_ptr<TileLabel>>());
    }
}

void testLayerContentFlags() {
    {
        std::shared_ptr<TileLayer> layer = makeLayer({ makeGeometry(TileGeometry::Type::POLYGON, false),
                                                       makeGeometry(TileGeometry::Type::LINE, false) });
        TEST_CHECK(!layer->hasGroundAOGeometry(), "a layer of fills and lines has no contact shadow");
        TEST_CHECK(!layer->hasSpanGeometry(), "and no span");
    }

    // A contact shadow is its own geometry type, so the flag cannot be inferred from POLYGON3D.
    {
        std::shared_ptr<TileLayer> layer = makeLayer({ makeGeometry(TileGeometry::Type::POLYGON3D, false) });
        TEST_CHECK(!layer->hasGroundAOGeometry(), "an extrusion without its skirt has no contact shadow");
    }
    {
        std::shared_ptr<TileLayer> layer = makeLayer({ makeGeometry(TileGeometry::Type::POLYGON3D, false),
                                                       makeGeometry(TileGeometry::Type::POLYGON3DGROUND, false) });
        TEST_CHECK(layer->hasGroundAOGeometry(), "an extrusion with its skirt has one");
    }

    // The constructor's loop answers both flags and may only stop once both are settled.
    {
        std::shared_ptr<TileLayer> layer = makeLayer({ makeGeometry(TileGeometry::Type::POLYGON, false),
                                                       makeGeometry(TileGeometry::Type::POLYGON3DGROUND, false),
                                                       makeGeometry(TileGeometry::Type::LINE, false) });
        TEST_CHECK(layer->hasGroundAOGeometry(), "a skirt in the middle is still found");
    }

    {
        std::shared_ptr<TileLayer> layer = makeLayer({ makeGeometry(TileGeometry::Type::LINE, true),
                                                       makeGeometry(TileGeometry::Type::POLYGON3DGROUND, false) });
        TEST_CHECK(layer->hasSpanGeometry(), "a span found first does not hide the shadow");
        TEST_CHECK(layer->hasGroundAOGeometry(), "which is found after it");
    }
    {
        std::shared_ptr<TileLayer> layer = makeLayer({ makeGeometry(TileGeometry::Type::POLYGON3DGROUND, false),
                                                       makeGeometry(TileGeometry::Type::LINE, true) });
        TEST_CHECK(layer->hasGroundAOGeometry(), "a shadow found first does not hide the span");
        TEST_CHECK(layer->hasSpanGeometry(), "which is found after it");
    }

    {
        std::shared_ptr<TileLayer> layer = makeLayer({});
        TEST_CHECK(!layer->hasGroundAOGeometry(), "an empty layer has no contact shadow");
        TEST_CHECK(!layer->hasSpanGeometry(), "and no span");
    }
}
