/*
 * The vertex slot an extrusion's CPU-resolved ground is patched into.
 *
 * The base has to be the SAME for every vertex of a building. A vertex-shader sample could not
 * promise that - the elevation texture bound is the one the TILE BEING DRAWN carries, so a
 * footprint spanning two tiles was sampled through two textures, got two bases, and tore open
 * along the tile line. GLTileRenderer::resolveExtrusionBases asks the global ElevationManager once
 * per footprint instead and writes the answer here.
 *
 * What is testable without a GL context is the patching itself: the byte offsets, and the dirty
 * range the renderer re-uploads. Both are silent when wrong - a bad offset corrupts a neighbouring
 * attribute rather than failing.
 */

#include "TileGeometry.h"

#include <cstring>
#include <vector>

#include "TestCheck.h"

using namespace massif::vt;

namespace {

    /** A geometry with one float base slot per vertex, laid out as packGeometry does. */
    std::shared_ptr<TileGeometry> makeExtrusion(std::size_t vertexCount) {
        TileGeometry::VertexGeometryLayoutParameters params;
        params.dimensions = 3;
        params.coordOffset = 0;
        params.vertexSize = 8;              // 3 x int16, padded to 4
        params.baseOffset = params.vertexSize;
        params.vertexSize += sizeof(float);
        params.coordScale = 1.0f;

        VertexArray<std::uint8_t> vertexGeometry;
        vertexGeometry.fill(0, vertexCount * params.vertexSize);
        for (std::size_t i = 0; i < vertexCount; i++) { // as packGeometry does
            float unresolved = TileGeometry::UNRESOLVED_BASE;
            std::memcpy(&vertexGeometry[i * params.vertexSize + params.baseOffset], &unresolved, sizeof(float));
        }
        VertexArray<std::uint16_t> indices;
        for (std::size_t i = 0; i < vertexCount; i++) {
            indices.append(static_cast<std::uint16_t>(i));
        }
        return std::make_shared<TileGeometry>(TileGeometry::Type::POLYGON3D, 1.0f,
                                              TileGeometry::StyleParameters(), params,
                                              std::move(vertexGeometry), std::move(indices),
                                              std::vector<std::pair<std::size_t, long long>>(),
                                              std::vector<std::pair<std::size_t, std::uint16_t>>());
    }

    float readBase(const std::shared_ptr<TileGeometry>& geometry, std::size_t vertexIndex) {
        const TileGeometry::VertexGeometryLayoutParameters& params = geometry->getVertexGeometryLayoutParameters();
        float value = 0;
        std::memcpy(&value, geometry->getVertexGeometry().data() + vertexIndex * params.vertexSize + params.baseOffset, sizeof(float));
        return value;
    }

}

void testExtrusionBase() {
    std::shared_ptr<TileGeometry> geometry = makeExtrusion(4);

    TEST_CHECK(!geometry->isBaseResolved(), "an extrusion starts unresolved");
    // The sentinel is the safety property: an extrusion whose elevation never arrives must still
    // be DRAWN, falling back to the ground under each vertex. Zero would bury it at sea level, and
    // skipping the draw loses it outright - which is what a first attempt at this did.
    TEST_CHECK(readBase(geometry, 0) == TileGeometry::UNRESOLVED_BASE,
               "and its base slot carries the sentinel, not a plausible height");
    TEST_CHECK(TileGeometry::UNRESOLVED_BASE < -1.0e29f,
               "which is past the threshold polygon3DVsh tests against");
    TEST_CHECK(!geometry->getDirtyVertexBytes(), "with nothing to re-upload");

    // A base written into the slot reads back, and asks for exactly its own bytes.
    TEST_CHECK(geometry->setVertexBase(1, 512.25f), "writing a base reports a change");
    TEST_CHECK(readBase(geometry, 1) == 512.25f, "and the value round-trips");
    TEST_CHECK(readBase(geometry, 0) == TileGeometry::UNRESOLVED_BASE &&
               readBase(geometry, 2) == TileGeometry::UNRESOLVED_BASE,
               "without touching its neighbours - a wrong offset would corrupt them silently");

    const TileGeometry::VertexGeometryLayoutParameters& params = geometry->getVertexGeometryLayoutParameters();
    std::size_t expectedFirst = 1 * params.vertexSize + params.baseOffset;
    TEST_CHECK(geometry->getDirtyVertexBytes().has_value(), "and marks a range to re-upload");
    TEST_CHECK(geometry->getDirtyVertexBytes()->first == expectedFirst,
               "the range starts at that vertex's base slot");
    TEST_CHECK(geometry->getDirtyVertexBytes()->second == expectedFirst + sizeof(float),
               "and covers one float");

    // Re-resolving after an elevation change is the common case, and most bases do not move. A
    // no-op write must not dirty anything, or every DEM tile re-uploads every building in view.
    TEST_CHECK(!geometry->setVertexBase(1, 512.25f), "writing the same base again is a no-op");
    TEST_CHECK(geometry->getDirtyVertexBytes()->second == expectedFirst + sizeof(float),
               "and does not grow the dirty range");

    // A second vertex extends the range to span both, since the renderer uploads one interval.
    TEST_CHECK(geometry->setVertexBase(3, -7.5f), "a second vertex is written");
    TEST_CHECK(geometry->getDirtyVertexBytes()->first == expectedFirst, "the range still starts there");
    TEST_CHECK(geometry->getDirtyVertexBytes()->second == 3 * params.vertexSize + params.baseOffset + sizeof(float),
               "and now reaches the far vertex");

    geometry->clearDirtyVertexBytes();
    TEST_CHECK(!geometry->getDirtyVertexBytes(), "the upload clears it");

    geometry->setBaseResolved(true);
    geometry->setBaseElevationVersion(7);
    TEST_CHECK(geometry->isBaseResolved() && geometry->getBaseElevationVersion() == 7,
               "a resolved extrusion records the elevation version it was resolved against");

    // The vertex data is what the base pass rewrites every time a DEM tile lands, so releasing it
    // after the first upload - which is what a geometry with no patchable slot does - would leave
    // nothing to patch.
    geometry->releaseVertexArrays();
    TEST_CHECK(!geometry->getVertexGeometry().empty(), "a base slot pins the vertex data");
    TEST_CHECK(readBase(geometry, 3) == -7.5f, "so the bases survive it");
}
