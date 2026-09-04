/*
 * The ANCHOR an extrusion's base is resolved at: the footprint's centroid, packed into the
 * texcoord slot at the coord scale (TileLayerBuilder::packGeometry). Every vertex of a footprint
 * has to carry the same one, and it has to survive the int16 pack even when it lies tiles away -
 * a palace's centroid seen through a z20 piece of it wrapped the int16 at the coords' scale, and
 * each tile then read its base at a garbage position: the same wing at two heights, walls under
 * the ground, only on 3D terrain and only zoomed in close.
 *
 * NOT covered here: the ground sample itself - GLTileRenderer::resolveExtrusionBases asks the
 * SDK's elevation provider for the SMOOTHED field, which is a device check.
 */

#include "TileLayerBuilder.h"

#include "TestCheck.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace massif::vt;

namespace {
    // A REAL tile, so a footprint is not below the int16 quantum (at z0 a building is ~1e-7 tile).
    const TileId PARIS_Z16(16, 33193, 22545);

    using Ring = std::vector<cglib::vec2<float>>;

    Ring square(float x0, float y0, float side) {
        return { { x0, y0 }, { x0 + side, y0 }, { x0 + side, y0 + side }, { x0, y0 + side } };
    }

    struct VertexAnchor {
        float coordX;   // where the vertex is, to tell one footprint's vertices from another's
        float anchorX, anchorY;
    };

    // Every POLYGON3D vertex the layer packs, with its anchor read back from the texcoord slot.
    std::vector<VertexAnchor> build(const std::vector<Ring>& footprints, float& coordScale) {
        auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
        TileLayerBuilder builder("test", 0, PARIS_Z16, transformer, 256.0f, 1.0f);
        Polygon3DStyle style(ColorFunction(Color(1, 1, 1, 1)), std::optional<Transform>());
        TileLayerBuilder::Polygon3DProcessor processor = builder.createPolygon3DProcessor(style);
        long long id = 1;
        for (const Ring& ring : footprints) {
            TileLayerBuilder::VerticesList verticesList;
            verticesList.push_back(ring);
            processor(id++, verticesList, 0.0f, 10.0f);
        }

        std::vector<VertexAnchor> vertices;
        std::shared_ptr<TileLayer> layer = builder.buildTileLayer();
        for (const std::shared_ptr<TileGeometry>& geometry : layer->getGeometries()) {
            if (geometry->getType() != TileGeometry::Type::POLYGON3D) {
                continue;
            }
            const TileGeometry::VertexGeometryLayoutParameters& layout = geometry->getVertexGeometryLayoutParameters();
            TEST_CHECK(layout.texCoordOffset >= 0, "an extrusion keeps its texcoord slot for the anchor");
            TEST_CHECK(layout.texCoordScale == layout.coordScale, "at the coord scale, so one scale converts either");
            coordScale = layout.coordScale;
            const std::uint8_t* vertexData = geometry->getVertexGeometry().data();
            std::size_t count = geometry->getVertexGeometry().size() / layout.vertexSize;
            for (std::size_t i = 0; i < count; i++) {
                const std::uint8_t* vertex = vertexData + i * layout.vertexSize;
                const std::int16_t* coord = reinterpret_cast<const std::int16_t*>(vertex + layout.coordOffset);
                const std::int16_t* tex = reinterpret_cast<const std::int16_t*>(vertex + layout.texCoordOffset);
                vertices.push_back({ coord[0] / layout.coordScale, tex[0] / layout.texCoordScale, tex[1] / layout.texCoordScale });
            }
        }
        return vertices;
    }
}

void testExtrusionAnchor() {
    // Two footprints in the tile: each vertex carries its own footprint's centroid, flipped in y
    // like the coords (calculatePoint).
    float coordScale = 0;
    std::vector<VertexAnchor> vertices = build({ square(0.20f, 0.20f, 0.05f), square(0.50f, 0.50f, 0.10f) }, coordScale);
    TEST_CHECK(coordScale > 0 && !vertices.empty(), "the extrusions were packed");
    float tolerance = 1.0f / coordScale + 1.0e-4f;
    bool agree = true;
    for (const VertexAnchor& vertex : vertices) {
        float expectX = (vertex.coordX < 0.35f ? 0.225f : 0.55f);
        float expectY = 1.0f - expectX;
        agree = agree && std::abs(vertex.anchorX - expectX) < tolerance && std::abs(vertex.anchorY - expectY) < tolerance;
    }
    TEST_CHECK(agree, "every vertex carries the centroid of its own footprint");

    // A footprint whose centroid lies FAR outside this tile - a palace seen through a z20 piece of
    // it - must still pack its anchor: at the coords' scale it wrapped the int16, and the base was
    // read at a garbage position that differed per tile. The scale bends to the anchor.
    Ring palace = { { -6.0f, 0.2f }, { 1.5f, 0.2f }, { 1.5f, 0.3f }, { -6.0f, 0.3f } };
    float palaceScale = 0;
    std::vector<VertexAnchor> far = build({ palace }, palaceScale);
    TEST_CHECK(!far.empty() && palaceScale > 0 && palaceScale <= 4096.0f, "the coord scale shrinks for a far anchor");
    TEST_CHECK(std::abs(far[0].anchorX - (-2.25f)) < 1.0f / palaceScale + 1.0e-3f, "and the anchor reads back where the centroid is");
}
