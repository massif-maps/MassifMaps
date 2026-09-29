/*
 * The CPU copy of an extrusion that labels are ray-tested against (vt/ExtrusionOccluder.h), which
 * replaced drawing every building a second time into a depth texture. Checked: a ray through a
 * building is blocked and one over its roof is not, the height scale (the zoom ramp and grow-in)
 * moves the roof, a ray that stops short of the wall is clear, a base not yet resolved occludes
 * nothing, and the base the DEM resolves lifts the building with it.
 *
 * Which labels fade on a device is a screenshot at the Grenoble camera in
 * docs/internals/rendering/06-labels.mdx.
 */

#include "ExtrusionOccluder.h"
#include "TileGeometry.h"

#include <cstring>
#include <vector>

#include "TestCheck.h"

using namespace massif::vt;

namespace {

    // A 0.2 x 0.2 box on the tile's centre, 100 height units tall: four walls and a roof, as
    // packGeometry lays them out (int16 xy, int16 height, float base).
    std::shared_ptr<TileGeometry> makeBox(float base) {
        TileGeometry::VertexGeometryLayoutParameters params;
        params.dimensions = 2;
        params.coordOffset = 0;
        params.heightOffset = 4;
        params.baseOffset = 8;
        params.vertexSize = 12;
        params.coordScale = 1000.0f;
        params.heightScale = 1.0f;

        const float corners[4][2] = { { 0.4f, 0.4f }, { 0.6f, 0.4f }, { 0.6f, 0.6f }, { 0.4f, 0.6f } };
        VertexArray<std::uint8_t> vertexGeometry;
        vertexGeometry.fill(0, 8 * params.vertexSize);
        for (int i = 0; i < 8; i++) {
            std::int16_t coord[2] = { static_cast<std::int16_t>(corners[i % 4][0] * params.coordScale), static_cast<std::int16_t>(corners[i % 4][1] * params.coordScale) };
            std::int16_t height = (i < 4 ? 0 : 100);
            std::memcpy(&vertexGeometry[i * params.vertexSize + params.coordOffset], coord, sizeof(coord));
            std::memcpy(&vertexGeometry[i * params.vertexSize + params.heightOffset], &height, sizeof(height));
            std::memcpy(&vertexGeometry[i * params.vertexSize + params.baseOffset], &base, sizeof(base));
        }
        VertexArray<std::uint16_t> indices;
        for (std::uint16_t i = 0; i < 4; i++) {
            std::uint16_t j = (i + 1) % 4;
            indices.append(i, j, static_cast<std::uint16_t>(j + 4));
            indices.append(i, static_cast<std::uint16_t>(j + 4), static_cast<std::uint16_t>(i + 4));
        }
        indices.append(4, 5, 6);
        indices.append(4, 6, 7);
        indices.append(0, 1, 2); // the floor: never an occluder
        return std::make_shared<TileGeometry>(TileGeometry::Type::POLYGON3D, 1.0f, TileGeometry::StyleParameters(), params,
                                              std::move(vertexGeometry), std::move(indices),
                                              std::vector<std::pair<std::size_t, long long>>(),
                                              std::vector<std::pair<std::size_t, std::uint16_t>>());
    }

    // From x = 0 to x = 1 across the tile's middle at height z; heightScale 0.01 makes the roof z = 1.
    bool blocked(const ExtrusionOccluder& occluder, const TileGeometry& geometry, double z, double heightScale = 0.01, double t1 = 1.0) {
        return occluder.intersects(cglib::vec3<double>(0.0, 0.5, z), cglib::vec3<double>(1.0, 0.0, 0.0), 0.0, t1, geometry, heightScale, 0.0);
    }

}

void testExtrusionOccluder() {
    std::shared_ptr<TileGeometry> box = makeBox(0.0f);
    std::shared_ptr<const ExtrusionOccluder> occluder = ExtrusionOccluder::build(*box);
    TEST_CHECK(occluder != nullptr, "a box builds an occluder");
    if (!occluder) {
        return;
    }
    TEST_CHECK(blocked(*occluder, *box, 0.5), "a ray through the walls is blocked");
    TEST_CHECK(!blocked(*occluder, *box, 1.5), "one over the roof is not");
    TEST_CHECK(!blocked(*occluder, *box, 0.5, 0.004), "the height scale lowers the roof under it");
    TEST_CHECK(!blocked(*occluder, *box, 0.5, 0.01, 0.35), "a ray stopping short of the wall is clear");
    TEST_CHECK(occluder->intersects(cglib::vec3<double>(0.5, 0.0, 0.5), cglib::vec3<double>(0.0, 1.0, 0.0), 0.0, 1.0, *box, 0.01, 0.0), "and along the other axis");
    // A camera ray: from high over the tile's corner down to a point on the ground behind the box.
    cglib::vec3<double> eye(0.0, 0.0, 3.0), behind(0.8, 0.8, 0.0);
    TEST_CHECK(occluder->intersects(eye, behind - eye, 0.0, 0.99, *box, 0.01, 0.0), "a descending ray to the ground behind the building is blocked");
    cglib::vec3<double> front(0.3, 0.3, 0.0);
    TEST_CHECK(!occluder->intersects(eye, front - eye, 0.0, 0.99, *box, 0.01, 0.0), "and one to the ground in front of it is not");

    std::shared_ptr<TileGeometry> unresolved = makeBox(TileGeometry::UNRESOLVED_BASE);
    std::shared_ptr<const ExtrusionOccluder> unresolvedOccluder = ExtrusionOccluder::build(*unresolved);
    TEST_CHECK(unresolvedOccluder && !blocked(*unresolvedOccluder, *unresolved, 0.5), "a building whose base is not resolved occludes nothing");

    std::shared_ptr<TileGeometry> raised = makeBox(2.0f);
    std::shared_ptr<const ExtrusionOccluder> raisedOccluder = ExtrusionOccluder::build(*raised);
    TEST_CHECK(raisedOccluder && !blocked(*raisedOccluder, *raised, 0.5) && blocked(*raisedOccluder, *raised, 2.5), "the resolved base lifts the whole building");
}
