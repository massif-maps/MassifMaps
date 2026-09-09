/*
 * Tests for the metres-to-vertex-frame scale terrain uploads on the globe
 * (libs-massif/vt/src/vt/TerrainElevationScale.h), which is uElevationScale.x there.
 *
 * On a sphere a DEM height is RADIAL, so the whole displacement is one number per tile - and that
 * number is only meaningful together with the frame its vertices are in. Three different frames
 * reach setupTerrainUniforms: the tile's own (the grid path), a coordScale'd one (tile geometry)
 * and a translate-only one in internal coordinates (the shared ground the globe draws). They differ
 * by a factor of 40 at zoom 13 and by 256 more with a coordScale, and being wrong reads as a
 * PERFECTLY FLAT globe rather than as anything obviously broken - the reason this is pinned here
 * and not left to a screenshot.
 *
 * The anchor is geometric, not a repeat of the formula: raising a vertex by one earth radius must
 * put it twice as far from the planet's centre, and one metre of displacement must move the world
 * point by one metre whatever frame it was expressed in.
 *
 * NOT covered here: that the shader displaces along the normal it reconstructs (the sphere-point
 * uniforms are replicated here, but only the arithmetic - no GL), the DEM node uv inversion, and
 * whether the globe's surface LOOKS right. Device checks, see docs/internals/rendering/18-globe.md.
 */

#include "utils/Const.h"

#include <vt/TerrainElevationScale.h>
#include <vt/TileTransformer.h>

#include <cmath>

using namespace massif;

#include "TestCheck.h"

namespace {

    // The transformer the globe runs with - Options.cpp builds exactly this one. Its scale is the
    // sphere's RADIUS in internal units, which is why it is not WORLD_SIZE.
    const double SPHERE_RADIUS = Const::WORLD_SIZE / Const::PI;
    const double EARTH_RADIUS = 6378137.0; // metres, TileTransformer's own constant

    // Mont Blanc, the camera the globe's terrain was judged at.
    const vt::TileId TILE(13, 4265, 2929);

    std::shared_ptr<vt::SphericalTileTransformer> transformer() {
        return std::make_shared<vt::SphericalTileTransformer>(static_cast<float>(SPHERE_RADIUS));
    }

    bool nearly(double value, double expected, double tolerance = 1.0e-5) {
        return std::fabs(value - expected) <= tolerance * std::max(1.0, std::fabs(expected));
    }

    /** The three frames a terrain draw hands to setupTerrainUniforms, in the renderer's own terms. */
    cglib::mat4x4<double> gridFrame(const vt::TileTransformer& t) {
        return t.calculateTileMatrix(TILE, 1.0f); // renderTileMask, gridMode
    }

    cglib::mat4x4<double> geometryFrame(const vt::TileTransformer& t, float coordScale) {
        return t.calculateTileMatrix(TILE, 1.0f / coordScale); // tile geometry
    }

    cglib::mat4x4<double> groundFrame(const vt::TileTransformer& t) {
        // The shared ground: vertices are internal coordinates relative to the builder's origin.
        return cglib::translate4_matrix(t.calculateTileOrigin(TILE));
    }

    /** A tile-centre vertex in each frame, the way TileSurfaceBuilder and the geometry builders make it. */
    cglib::vec3<double> vertexIn(const cglib::mat4x4<double>& frame, const vt::TileTransformer& t, float coordScale) {
        cglib::vec3<double> local = cglib::vec3<double>::convert(t.createTileVertexTransformer(TILE)->calculatePoint(cglib::vec2<float>(0.5f, 0.5f)));
        return cglib::transform_point(local * static_cast<double>(coordScale), cglib::inverse(frame) * t.calculateTileMatrix(TILE, 1.0f / coordScale));
    }

    void testAHeightIsRadialAndAnEarthRadiusDoublesIt() {
        std::shared_ptr<vt::SphericalTileTransformer> t = transformer();
        std::shared_ptr<const vt::TileTransformer::VertexTransformer> vertexTransformer = t->createTileVertexTransformer(TILE);

        // Tile-local: the planet's centre is the tile origin pushed back, and the surface is one
        // sphere radius away from it - 2^zoom tile-local units, by calculatePoint's own scaling.
        cglib::vec3<double> centre = t->calculateTileOrigin(TILE) * (-(1 << TILE.zoom) / SPHERE_RADIUS);
        cglib::vec3<double> surface = cglib::vec3<double>::convert(vertexTransformer->calculatePoint(cglib::vec2<float>(0.5f, 0.5f)));
        double radiusLocal = cglib::length(surface - centre);
        TEST_CHECK(nearly(radiusLocal, 1 << TILE.zoom),
                   "the planet's radius is 2^zoom tile-local units");

        double earthRadiusLocal = vertexTransformer->calculateHeight(cglib::vec2<float>(0.5f, 0.5f), static_cast<float>(EARTH_RADIUS));
        TEST_CHECK(nearly(earthRadiusLocal, radiusLocal, 1.0e-4),
                   "... and one earth radius of height is exactly that many units");

        // The number 18-globe.md quotes: 4 km of relief at zoom 13.
        double reliefLocal = vertexTransformer->calculateHeight(cglib::vec2<float>(0.5f, 0.5f), 4000.0f);
        TEST_CHECK(nearly(reliefLocal, 4000.0 * (1 << TILE.zoom) / EARTH_RADIUS, 1.0e-4),
                   "4000 m is about 5.1 tile-local units at zoom 13");
    }

    /** One metre of uElevationScale.x must move the WORLD point by one metre, in every frame. */
    void checkOneMetre(const cglib::mat4x4<double>& frame, float coordScale, const char* what) {
        std::shared_ptr<vt::SphericalTileTransformer> t = transformer();
        double scale = vt::sphericalMetersToFrame(*t, TILE, frame);

        // What the shader does: uTerrainSphereOrigin + pos * uTerrainSphereScale is the unit-sphere
        // point, and the displacement rides its normal.
        cglib::vec3<double> pos = vertexIn(frame, *t, coordScale);
        cglib::vec3<double> sphereOrigin(frame(0, 3) / SPHERE_RADIUS, frame(1, 3) / SPHERE_RADIUS, frame(2, 3) / SPHERE_RADIUS);
        cglib::vec3<double> sphereScale(frame(0, 0) / SPHERE_RADIUS, frame(1, 1) / SPHERE_RADIUS, frame(2, 2) / SPHERE_RADIUS);
        cglib::vec3<double> spherePoint(sphereOrigin(0) + pos(0) * sphereScale(0),
                                        sphereOrigin(1) + pos(1) * sphereScale(1),
                                        sphereOrigin(2) + pos(2) * sphereScale(2));
        TEST_CHECK(nearly(cglib::length(spherePoint), 1.0, 1.0e-4),
                   "the frame's own uniforms recover a UNIT sphere point");

        cglib::vec3<double> displaced = pos + cglib::unit(spherePoint) * scale;
        double movedInternal = cglib::length(cglib::transform_point(displaced, frame) - cglib::transform_point(pos, frame));
        TEST_CHECK(nearly(movedInternal, SPHERE_RADIUS / EARTH_RADIUS, 1.0e-4), what);
    }

    void testOneMetreIsOneMetreInEveryFrame() {
        std::shared_ptr<vt::SphericalTileTransformer> t = transformer();
        checkOneMetre(gridFrame(*t), 1.0f, "one metre moves the ground one metre in the tile's own frame");
        checkOneMetre(geometryFrame(*t, 256.0f), 256.0f, "... in a coordScale'd geometry frame");
        checkOneMetre(groundFrame(*t), 1.0f, "... and in the shared ground's internal frame");
    }

    void testTheFramesReallyDoDiffer() {
        // Guards the test above against passing on a formula that ignores the frame: if the three
        // scales were equal, dropping the conversion would still pass. They differ by 40 and by 256.
        std::shared_ptr<vt::SphericalTileTransformer> t = transformer();
        double grid = vt::sphericalMetersToFrame(*t, TILE, gridFrame(*t));
        double geometry = vt::sphericalMetersToFrame(*t, TILE, geometryFrame(*t, 256.0f));
        double ground = vt::sphericalMetersToFrame(*t, TILE, groundFrame(*t));
        TEST_CHECK(nearly(geometry / grid, 256.0), "a coordScale'd frame scales with the coordScale");
        TEST_CHECK(nearly(ground / grid, SPHERE_RADIUS / (1 << TILE.zoom)),
                   "the internal frame is one tile-local unit apart - about 40x at zoom 13");
    }

    void testTheScaleIsTheSameAtEveryZoom() {
        // In INTERNAL coordinates a metre is a metre, whatever tile is being drawn: the shared
        // ground spans tiles of several zooms in ONE mesh, so a zoom-dependent scale there would
        // step at every LOD ring.
        std::shared_ptr<vt::SphericalTileTransformer> t = transformer();
        for (int zoom = 5; zoom <= 16; zoom++) {
            vt::TileId tileId(zoom, TILE.x >> std::max(0, TILE.zoom - zoom), TILE.y >> std::max(0, TILE.zoom - zoom));
            cglib::mat4x4<double> frame = cglib::translate4_matrix(t->calculateTileOrigin(tileId));
            if (!nearly(vt::sphericalMetersToFrame(*t, tileId, frame), SPHERE_RADIUS / EARTH_RADIUS, 1.0e-4)) {
                TEST_CHECK(false, "the internal-frame scale is zoom-independent");
                return;
            }
        }
        TEST_CHECK(true, "the internal-frame scale is zoom-independent");
    }

}

void testGlobeElevationScale() {
    testAHeightIsRadialAndAnEarthRadiusDoublesIt();
    testOneMetreIsOneMetreInEveryFrame();
    testTheFramesReallyDoDiffer();
    testTheScaleIsTheSameAtEveryZoom();
}
