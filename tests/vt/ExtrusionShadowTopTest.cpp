/*
 * The building TOP every extrusion wall vertex carries (TileLayerBuilder::packTopHeight, attribute
 * byte 2). polygon3DVsh moves a sun-facing-away face's shadow lookup there: below the roof the face
 * is always in its own building's shadow, so only a lookup above it can tell whether ANOTHER
 * caster hides its sky (docs/internals/rendering/08-lighting-sky-fog.md).
 *
 * Asserted: the code never undershoots the roof (a probe under it reads its own building), stays
 * within one step (~6%) of it, never packs to 0, and rides every wall and pitched-slope vertex in
 * the builder's own height units once decoded at the tile centre - the conversion the renderer's
 * uShadowTopScale repeats. A metres-vs-tile-units mix-up put the probe kilometres up in the first
 * draft of this fix and every back wall read as unshadowed.
 *
 * NOT covered: the shader's decode and the lookup itself (GL, a device check), nor the renderer
 * setting uShadowTopScale.
 */

#include "TileLayerBuilder.h"

#include "TestCheck.h"

#include <cmath>
#include <string>
#include <vector>

using namespace massif::vt;

namespace {
    const TileId GRENOBLE_Z16(16, 33812, 23525);

    float decodeTop(std::int8_t code) {
        return std::exp2(code / 12.0f) - 1.0f;
    }

    struct WallVertex {
        float height; // builder units, unpacked
        std::int8_t side;
        std::int8_t top;
    };

    std::vector<WallVertex> extrude(float maxHeight, const Polygon3DStyle& style, float edgeRadius) {
        auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
        TileLayerBuilder builder("test", 0, GRENOBLE_Z16, transformer, 4096.0f, 1.0f);
        builder.setPolygon3DEdgeRadius(edgeRadius);
        TileLayerBuilder::Polygon3DProcessor processor = builder.createPolygon3DProcessor(style);
        TileLayerBuilder::VerticesList verticesList;
        verticesList.push_back({ { 0.40f, 0.40f }, { 0.46f, 0.40f }, { 0.46f, 0.43f }, { 0.40f, 0.43f } });
        processor(1, verticesList, 0.0f, maxHeight);

        std::vector<WallVertex> out;
        std::shared_ptr<TileLayer> layer = builder.buildTileLayer();
        for (const std::shared_ptr<TileGeometry>& geometry : layer->getGeometries()) {
            if (geometry->getType() != TileGeometry::Type::POLYGON3D) {
                continue;
            }
            const TileGeometry::VertexGeometryLayoutParameters& layout = geometry->getVertexGeometryLayoutParameters();
            const std::uint8_t* data = geometry->getVertexGeometry().data();
            std::size_t count = geometry->getVertexGeometry().size() / layout.vertexSize;
            for (std::size_t i = 0; i < count; i++) {
                const std::uint8_t* vertex = data + i * layout.vertexSize;
                const std::int8_t* attribs = reinterpret_cast<const std::int8_t*>(vertex + layout.attribsOffset);
                const std::int16_t* height = reinterpret_cast<const std::int16_t*>(vertex + layout.heightOffset);
                out.push_back({ height[0] / layout.heightScale, attribs[1], attribs[2] });
            }
        }
        return out;
    }

    // What every wall vertex must carry, and that no wall vertex stands above the decoded top.
    void checkWalls(const std::vector<WallVertex>& vertices, float top, bool slopes, const char* what) {
        float localPerMeter = DefaultTileTransformer(1.0f).createTileVertexTransformer(GRENOBLE_Z16)->calculateHeight(cglib::vec2<float>(0.5f, 0.5f), 1.0f);
        std::int8_t expected = TileLayerBuilder::packTopHeight(top);
        int walls = 0, wrong = 0, above = 0;
        for (const WallVertex& v : vertices) {
            // 127 = a wall, 64 = a pitched slope's eave; a flat roof (0) never faces away from the sun.
            if (v.side != 127 && !(slopes && v.side == 64)) {
                continue;
            }
            walls++;
            wrong += (v.top != expected);
            above += (v.height > decodeTop(v.top) * localPerMeter * 1.001f);
        }
        TEST_CHECK(walls > 0, (std::string(what) + ": the extrusion emits wall vertices").c_str());
        TEST_CHECK(wrong == 0, (std::string(what) + ": every wall vertex carries the building's packed top").c_str());
        TEST_CHECK(above == 0, (std::string(what) + ": decoded at the tile centre, the top is at or above every wall vertex in the builder's height units").c_str());
    }
}

void testExtrusionShadowTop() {
    bool roundsUp = true, oneStep = true;
    for (float h : { 0.3f, 2.0f, 9.5f, 20.0f, 57.3f, 300.0f, 828.0f }) {
        float decoded = decodeTop(TileLayerBuilder::packTopHeight(h));
        roundsUp = roundsUp && decoded >= h;
        oneStep = oneStep && decoded <= h + (1.0f + h) * (std::exp2(1.0f / 12.0f) - 1.0f) + 1.0e-3f;
    }
    TEST_CHECK(roundsUp, "packTopHeight never decodes below the roof, so the probe clears its own building");
    TEST_CHECK(oneStep, "packTopHeight decodes within one twelfth of an octave of the roof");
    TEST_CHECK(TileLayerBuilder::packTopHeight(0.0f) == 1 && TileLayerBuilder::packTopHeight(-5.0f) == 1, "a building at or under the ground still packs a non-zero top");
    TEST_CHECK(TileLayerBuilder::packTopHeight(1.0e5f) == 127, "a height past the code range clamps to 127 rather than wrapping");

    Polygon3DStyle flat(ColorFunction(Color(1, 1, 1, 1)), std::optional<Transform>());
    checkWalls(extrude(24.0f, flat, 0.0f), 24.0f, false, "flat roof");
    checkWalls(extrude(24.0f, flat, 1.0f), 24.0f, false, "flat roof with an edge radius");

    Polygon3DStyle gabled(ColorFunction(Color(1, 1, 1, 1)), std::optional<Transform>(), RoofShape::GABLED, 6.0f);
    checkWalls(extrude(24.0f, gabled, 0.0f), 30.0f, true, "gabled roof (top at the ridge)");
}
