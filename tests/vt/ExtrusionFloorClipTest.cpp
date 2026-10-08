/*
 * A footprint's floor points come from its WHOLE ring (TileLayerBuilder::setBaseFootprints),
 * not from the vertices one tile kept of it.
 *
 * Packing keeps only the triangles inside the tile, and the floor used to be read from those: a
 * building cut by a tile line got a different "highest ground under it" on each side, so each half
 * stood at its own height. Measured on the web build at Collioure's castle (2026-10-08): one building
 * resolved to five bases ~1.8 m apart, one per z18/z19 tile, with the smoothed anchor identical.
 */

#include "TileLayerBuilder.h"

#include "TestCheck.h"

#include <cmath>
#include <vector>

using namespace massif::vt;

namespace {
    struct Packed {
        std::vector<TileGeometry::BaseAnchor> anchors;
        std::vector<TileGeometry::BaseRun> runs;
        std::size_t vertexCount = 0;
        float heightScale = 0;
    };

    // One footprint straddling x = 1, as the tile `shift` tiles to the right of the first packs it.
    Packed pack(float shift) {
        auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
        TileLayerBuilder builder("test", 0, TileId(18, 132775, 90182), transformer, 256.0f, 1.0f);
        Polygon3DStyle style(ColorFunction(Color(1, 1, 1, 1)), std::optional<Transform>());
        TileLayerBuilder::Polygon3DProcessor processor = builder.createPolygon3DProcessor(style);
        TileLayerBuilder::VerticesList verticesList;
        verticesList.push_back({ { 0.8f - shift, 0.4f }, { 1.3f - shift, 0.4f }, { 1.3f - shift, 0.45f }, { 1.05f - shift, 0.6f }, { 0.8f - shift, 0.45f } });
        processor(1, verticesList, 0.0f, 20.0f);

        Packed packed;
        std::shared_ptr<TileLayer> layer = builder.buildTileLayer();
        for (const std::shared_ptr<TileGeometry>& geometry : layer->getGeometries()) {
            if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
                packed.anchors = geometry->getBaseAnchors();
                packed.runs = geometry->getBaseRuns();
                packed.vertexCount = geometry->getVertexGeometry().size() / geometry->getVertexGeometryLayoutParameters().vertexSize;
                packed.heightScale = geometry->getVertexGeometryLayoutParameters().heightScale;
            }
        }
        return packed;
    }

    bool near(float a, float b) {
        return std::abs(a - b) < 1.0e-5f;
    }
}

void testExtrusionFloorClip() {
    Packed left = pack(0.0f);
    Packed right = pack(1.0f);
    TEST_CHECK(left.anchors.size() == 1 && right.anchors.size() == 1, "each tile packs the footprint with one anchor");
    TEST_CHECK(left.runs.size() == 1 && left.runs[0].begin == 0 && left.runs[0].end == left.vertexCount,
               "whose run covers every vertex the tile kept");
    if (left.anchors.size() != 1 || right.anchors.size() != 1) {
        return;
    }
    const std::vector<cglib::vec2<float>>& leftPoints = left.anchors[0].floorPoints;
    const std::vector<cglib::vec2<float>>& rightPoints = right.anchors[0].floorPoints;
    bool same = !leftPoints.empty() && leftPoints.size() == rightPoints.size();
    bool outside = false;
    for (std::size_t i = 0; same && i < leftPoints.size(); i++) {
        same = near(leftPoints[i](0), rightPoints[i](0) + 1.0f) && near(leftPoints[i](1), rightPoints[i](1));
        outside = outside || leftPoints[i](0) > 1.1f;
    }
    TEST_CHECK(same, "both tiles read the floor at the same points");
    TEST_CHECK(outside, "including ones outside the left tile");
    TEST_CHECK(near(left.anchors[0].maxHeightUnits / left.heightScale, right.anchors[0].maxHeightUnits / right.heightScale),
               "and measure it against the same roof height");
}
