/*
 * What a decoded layer knows about its own content without walking it (vt/TileLayer.h).
 *
 * Both flags exist for the same reason, and it is ABSENCE that costs. A search for "is there a span
 * here" or "is there a contact shadow here" stops at the first hit, so a style that HAS them is
 * cheap - and a style that has none walks every geometry of every layer to the end and answers no.
 *
 * Measured on the Crosscall in AlpiMaps, which draws no 3D buildings at all:
 *   PROF PRELUDE: 219.4 ms | ... paintTiles 8.1 tail 211.0
 * The 211 ms was isGroundAOBakeable, asked once per drape layer per frame to fingerprint the drape
 * stack. It took the renderer mutex - which a tile-set change holds for a whole label map rebuild -
 * and then scanned every visible tile's every layer's every geometry for a POLYGON3DGROUND that a
 * style without extrusions never contains. The default AO intensity is 0.2, not 0, so the one cheap
 * short-circuit ahead of the scan never fired for anyone.
 *
 * Answered at decode time instead, on a worker, once per tile for the life of that tile.
 *
 * NOT covered here: that the renderer caches the per-frame answer (GLTileRenderer is not in this
 * link) - see refreshGroundAOBakeable, called where the visible tiles are published.
 */

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
    // The case that was costing 211 ms a frame: no extrusions anywhere. The answer is no, and it
    // costs one bool rather than a scan that cannot stop early.
    {
        std::shared_ptr<TileLayer> layer = makeLayer({ makeGeometry(TileGeometry::Type::POLYGON, false),
                                                       makeGeometry(TileGeometry::Type::LINE, false) });
        TEST_CHECK(!layer->hasGroundAOGeometry(), "a layer of fills and lines has no contact shadow");
        TEST_CHECK(!layer->hasSpanGeometry(), "and no span");
    }

    // A contact shadow IS its own geometry type - an extrusion alone does not carry one, which is
    // why the flag cannot be inferred from POLYGON3D.
    {
        std::shared_ptr<TileLayer> layer = makeLayer({ makeGeometry(TileGeometry::Type::POLYGON3D, false) });
        TEST_CHECK(!layer->hasGroundAOGeometry(), "an extrusion without its skirt has no contact shadow");
    }
    {
        std::shared_ptr<TileLayer> layer = makeLayer({ makeGeometry(TileGeometry::Type::POLYGON3D, false),
                                                       makeGeometry(TileGeometry::Type::POLYGON3DGROUND, false) });
        TEST_CHECK(layer->hasGroundAOGeometry(), "an extrusion with its skirt has one");
    }

    // The skirt need not be first, or last: the constructor's loop now answers two questions at
    // once and may only stop when BOTH are settled.
    {
        std::shared_ptr<TileLayer> layer = makeLayer({ makeGeometry(TileGeometry::Type::POLYGON, false),
                                                       makeGeometry(TileGeometry::Type::POLYGON3DGROUND, false),
                                                       makeGeometry(TileGeometry::Type::LINE, false) });
        TEST_CHECK(layer->hasGroundAOGeometry(), "a skirt in the middle is still found");
    }

    // Both flags at once, in either order - the bug the shared loop could introduce is stopping at
    // the first answer and reporting the other one false.
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

    // An empty layer answers no to both, without touching anything.
    {
        std::shared_ptr<TileLayer> layer = makeLayer({});
        TEST_CHECK(!layer->hasGroundAOGeometry(), "an empty layer has no contact shadow");
        TEST_CHECK(!layer->hasSpanGeometry(), "and no span");
    }
}
