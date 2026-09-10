/*
 * Tests for the arithmetic the TERRAIN_SPHERICAL shader path depends on
 * (libs-massif/vt/src/vt/GLTileRendererShaders.h, applyTerrain).
 *
 * The shader cannot run here, so the functions it added - terrainSpherePoint,
 * terrainSphereToMercator and terrainSphereMercatorDelta - are reproduced below EXACTLY as the GLSL
 * writes them (and in the PRECISION the GPU runs them in, which is its own test), fed the uniform
 * values GLTileRenderer::setupTerrainUniforms computes, and checked against the transformer the
 * geometry was actually built with. That is the part worth testing: an inversion that disagrees
 * with the transformer by any amount puts the DEM sample in the wrong place, and on a globe there
 * is no flat reference frame to notice it in.
 *
 * NOT covered here: that any of it draws. The displacement direction, the skirt pass-through and
 * the disabled lattice clamp are all device checks. See docs/internals/rendering/18-globe.md.
 */

#include "vt/TileSurface.h"
#include "vt/TileSurfaceBuilder.h"
#include "vt/TileTransformer.h"
#include "vt/TerrainElevationScale.h"

#include <cmath>
#include <memory>
#include <set>
#include <vector>

using namespace massif;
using namespace massif::vt;

#include "TestCheck.h"

namespace {

    const double EARTH_CIRCUMFERENCE = 40075016.68558;
    const double WORLD_SIZE = 1 << 20;

    bool nearly(double value, double expected, double tolerance) {
        return std::fabs(value - expected) <= tolerance;
    }

    /** The uniforms setupTerrainUniforms uploads for a tile, derived the same way it derives them. */
    struct SphereUniforms {
        cglib::vec3<double> origin;
        cglib::vec3<double> scale;
        cglib::vec4<double> nodeUV;

        SphereUniforms(const TileTransformer& transformer, const TileId& tileId,
                       const cglib::vec2<double>& nodeOrigin, const cglib::vec2<double>& nodeSize) {
            cglib::mat4x4<double> frame = transformer.calculateTileMatrix(tileId, 1.0f);
            double sphereRadius = transformer.calculateTileMatrix(TileId(0, 0, 0), 1.0f)(0, 0);
            origin = cglib::vec3<double>(frame(0, 3), frame(1, 3), frame(2, 3)) * (1.0 / sphereRadius);
            scale = cglib::vec3<double>(frame(0, 0), frame(1, 1), frame(2, 2)) * (1.0 / sphereRadius);
            double internalPerRadian = sphereRadius * 0.5;
            nodeUV = cglib::vec4<double>(nodeOrigin(0) / internalPerRadian, nodeOrigin(1) / internalPerRadian,
                                         internalPerRadian / nodeSize(0), internalPerRadian / nodeSize(1));
        }
    };

    // GLSL: uTerrainSphereOrigin + pos * uTerrainSphereScale
    cglib::vec3<double> terrainSpherePoint(const SphereUniforms& u, const cglib::vec3<float>& pos) {
        return cglib::vec3<double>(u.origin(0) + pos(0) * u.scale(0),
                                   u.origin(1) + pos(1) * u.scale(1),
                                   u.origin(2) + pos(2) * u.scale(2));
    }

    // GLSL: atan(p.y, p.x), 0.5 * log((1 + rz) / (1 - rz))
    cglib::vec2<double> terrainSphereToMercator(const cglib::vec3<double>& p) {
        double len = cglib::length(p);
        double rz = std::min(0.999999, std::max(-0.999999, p(2) / len));
        return cglib::vec2<double>(std::atan2(p(1), p(0)), 0.5 * std::log((1.0 + rz) / (1.0 - rz)));
    }

    // GLSL: (mercator - uTerrainSphereNodeUV.xy) * uTerrainSphereNodeUV.zw
    cglib::vec2<double> shaderNodeUV(const SphereUniforms& u, const cglib::vec3<float>& pos) {
        cglib::vec2<double> mercator = terrainSphereToMercator(terrainSpherePoint(u, pos));
        double x = mercator(0) - u.nodeUV(0);
        x -= 6.283185307179586 * std::floor(x * 0.15915494309189535 + 0.5);
        return cglib::vec2<double>(x * u.nodeUV(2), (mercator(1) - u.nodeUV(1)) * u.nodeUV(3));
    }

    /** The tile's own internal-coordinate bounds, which is what a DEM node texture covers. */
    void tileInternalBounds(const TileId& tileId, cglib::vec2<double>& origin, cglib::vec2<double>& size) {
        int tileMask = (1 << tileId.zoom) - 1;
        double zoomScale = 1.0 / (1 << tileId.zoom);
        origin = cglib::vec2<double>(((tileId.x & tileMask) * zoomScale - 0.5) * WORLD_SIZE,
                                     ((tileMask - tileId.y) * zoomScale - 0.5) * WORLD_SIZE);
        size = cglib::vec2<double>(zoomScale * WORLD_SIZE, zoomScale * WORLD_SIZE);
    }

    void testOnlyTheSphereReportsItself() {
        DefaultTileTransformer planar(static_cast<float>(WORLD_SIZE));
        SphericalTileTransformer sphere(static_cast<float>(WORLD_SIZE / 3.1415926535897932));
        TEST_CHECK(!planar.isSpherical(), "the planar transformer is not spherical");
        TEST_CHECK(sphere.isSpherical(), "the spherical one is");
    }

    /*
     * THE check. The shader reconstructs the DEM uv from a curved vertex position; it has to land
     * on the same texel the tile's own geometry says it should.
     */
    void testTheShaderInversionRecoversTheTileUV() {
        SphericalTileTransformer sphere(static_cast<float>(WORLD_SIZE / 3.1415926535897932));
        // TileId is (zoom, x, y). Zoom 0 is left out: one tile spans a full 2pi, which the
        // longitude wrap cannot represent, and TerrainOptions::getMinZoom keeps terrain far above
        // it. The last two sit either side of the antimeridian, which is what the wrap is for.
        const TileId tileIds[] = { TileId(4, 8, 5), TileId(10, 700, 400), TileId(12, 2100, 1400), TileId(6, 0, 20), TileId(6, 63, 20) };

        bool interiorMatch = true;
        double worstInterior = 0, worstBorder = 0;
        for (const TileId& tileId : tileIds) {
            cglib::vec2<double> nodeOrigin, nodeSize;
            tileInternalBounds(tileId, nodeOrigin, nodeSize);
            SphereUniforms uniforms(sphere, tileId, nodeOrigin, nodeSize);
            std::shared_ptr<const TileTransformer::VertexTransformer> vertexTransformer = sphere.createTileVertexTransformer(tileId);

            for (float u : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
                for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
                    cglib::vec3<float> pos = vertexTransformer->calculatePoint(cglib::vec2<float>(u, v));
                    cglib::vec2<double> uv = shaderNodeUV(uniforms, pos);
                    // The node texture spans the tile, and its v runs with internal y - which is
                    // the tile's 1 - v (tileToEPSG3857 flips it).
                    double error = std::max(std::fabs(uv(0) - u), std::fabs(uv(1) - (1.0 - v)));
                    bool border = (u == 0.0f || u == 1.0f || v == 0.0f || v == 1.0f);
                    if (border) {
                        worstBorder = std::max(worstBorder, error);
                    } else {
                        worstInterior = std::max(worstInterior, error);
                        interiorMatch = interiorMatch && error < 1.0e-4;
                    }
                }
            }
        }
        TEST_CHECK(interiorMatch, "the shader's spherical inversion recovers the tile uv it was built from");
        TEST_CHECK(worstInterior < 1.0e-4, "to a ten-thousandth of a tile, well inside one DEM texel");

        // A BORDER vertex is NOT on the sphere: calculatePoint deliberately linearises tile edges
        // so that neighbouring tiles share them and no crack opens. Inverting one therefore returns
        // the chord's position rather than the nominal uv - up to a percent of a tile out at zoom 6,
        // where the whole edge is one chord. That is not an error: the vertex really is there, and
        // sampling the DEM where the vertex IS is what displaces it correctly. What must hold is
        // the next test, that both tiles agree on where that is.
        TEST_CHECK(worstBorder > worstInterior * 10, "a linearised tile border does not invert to its nominal uv");
    }

    /*
     * THE property the linearised border has to keep: two tiles meeting at an edge must invert
     * their shared vertices to the SAME place, or they sample different DEM texels there and the
     * displaced surface tears along every tile boundary.
     */
    void testNeighbouringTilesSampleTheSharedEdgeIdentically() {
        SphericalTileTransformer sphere(static_cast<float>(WORLD_SIZE / 3.1415926535897932));
        bool eastWestAgree = true, northSouthAgree = true;
        double worstGap = 0;
        const cglib::vec2<double> unitOrigin(0, 0), unitSize(1, 1);

        for (const TileId& left : { TileId(6, 20, 20), TileId(10, 700, 400), TileId(6, 63, 20) }) {
            TileId right(left.zoom, (left.x + 1) & ((1 << left.zoom) - 1), left.y);
            TileId below(left.zoom, left.x, left.y + 1);

            std::shared_ptr<const TileTransformer::VertexTransformer> l = sphere.createTileVertexTransformer(left);
            std::shared_ptr<const TileTransformer::VertexTransformer> r = sphere.createTileVertexTransformer(right);
            std::shared_ptr<const TileTransformer::VertexTransformer> b = sphere.createTileVertexTransformer(below);
            SphereUniforms lu(sphere, left, unitOrigin, unitSize);
            SphereUniforms ru(sphere, right, unitOrigin, unitSize);
            SphereUniforms bu(sphere, below, unitOrigin, unitSize);

            for (float t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
                // The left tile's east edge is the right tile's west edge.
                cglib::vec2<double> a = terrainSphereToMercator(terrainSpherePoint(lu, l->calculatePoint(cglib::vec2<float>(1.0f, t))));
                cglib::vec2<double> c = terrainSphereToMercator(terrainSpherePoint(ru, r->calculatePoint(cglib::vec2<float>(0.0f, t))));
                // A circular difference: the pair across the antimeridian is one tile apart, not
                // a world apart, and atan already folded both into (-pi, pi].
                double dx = a(0) - c(0);
                dx -= 6.283185307179586 * std::floor(dx * 0.15915494309189535 + 0.5);
                double gapX = std::fabs(dx);
                worstGap = std::max(worstGap, std::max(gapX, std::fabs(a(1) - c(1))));
                eastWestAgree = eastWestAgree && gapX < 1.0e-6 && std::fabs(a(1) - c(1)) < 1.0e-6;

                // ... and its v = 1 edge is the v = 0 edge of the tile below. tileToEPSG3857 flips
                // v, so v = 1 is the SOUTH edge and the tile below it is the one with y + 1.
                cglib::vec2<double> d = terrainSphereToMercator(terrainSpherePoint(lu, l->calculatePoint(cglib::vec2<float>(t, 1.0f))));
                cglib::vec2<double> e = terrainSphereToMercator(terrainSpherePoint(bu, b->calculatePoint(cglib::vec2<float>(t, 0.0f))));
                // Circular again: a tile at the antimeridian has both edges at +/- pi, and atan
                // can land on either sign for the two tiles.
                double dx2 = d(0) - e(0);
                dx2 -= 6.283185307179586 * std::floor(dx2 * 0.15915494309189535 + 0.5);
                double gap2 = std::max(std::fabs(dx2), std::fabs(d(1) - e(1)));
                worstGap = std::max(worstGap, gap2);
                northSouthAgree = northSouthAgree && gap2 < 1.0e-6;
            }
        }
        TEST_CHECK(eastWestAgree, "tiles either side of a shared meridian invert that edge to the same place");
        TEST_CHECK(northSouthAgree, "and so do tiles above and below a shared parallel");
        TEST_CHECK(worstGap < 1.0e-6, "so the DEM sample cannot tear along a tile boundary");
    }

    /*
     * The skirt drop is a GLOBE-ONLY vertex attribute. On the plane the drop is folded into the
     * vertex z as a sentinel, which costs nothing; on a sphere that would destroy the curved
     * position the shader displaces from, so it travels separately. The plane must not pay for it.
     */
    void testTheSkirtAttributeIsGlobeOnly() {
        auto planar = std::make_shared<DefaultTileTransformer>(static_cast<float>(WORLD_SIZE));
        auto sphere = std::make_shared<SphericalTileTransformer>(static_cast<float>(WORLD_SIZE / 3.1415926535897932));
        TileId tileId(6, 20, 20);

        auto build = [&](const std::shared_ptr<const TileTransformer>& transformer, bool skirts) {
            TileSurfaceBuilder builder(transformer);
            builder.setOrigin(transformer->calculateTileOrigin(tileId));
            builder.setVisibleTiles(std::set<TileId>{ tileId });
            builder.setTerrainSkirts(skirts);
            return builder.buildTileSurface(tileId);
        };

        std::vector<std::shared_ptr<TileSurface>> planarSurfaces = build(planar, true);
        std::vector<std::shared_ptr<TileSurface>> sphereSurfaces = build(sphere, true);
        std::vector<std::shared_ptr<TileSurface>> sphereNoSkirts = build(sphere, false);
        TEST_CHECK(!planarSurfaces.empty() && !sphereSurfaces.empty(), "both surfaces build with skirts on");
        if (planarSurfaces.empty() || sphereSurfaces.empty()) {
            return;
        }
        const TileSurface::VertexGeometryLayoutParameters& planarLayout = planarSurfaces.front()->getVertexGeometryLayoutParameters();
        const TileSurface::VertexGeometryLayoutParameters& sphereLayout = sphereSurfaces.front()->getVertexGeometryLayoutParameters();

        TEST_CHECK(planarLayout.skirtOffset < 0, "a planar surface carries no skirt attribute");
        TEST_CHECK(sphereLayout.skirtOffset >= 0, "a spherical one does");
        // Against the SAME surface without skirts, so the comparison isolates the attribute
        // rather than the normals and binormals a sphere carries and a plane does not.
        TEST_CHECK(!sphereNoSkirts.empty() && sphereNoSkirts.front()->getVertexGeometryLayoutParameters().skirtOffset < 0,
                   "a spherical surface without skirts carries no skirt attribute either");
        TEST_CHECK(!sphereNoSkirts.empty() && sphereLayout.vertexSize == sphereNoSkirts.front()->getVertexGeometryLayoutParameters().vertexSize + static_cast<int>(sizeof(float)),
                   "and when it is there it costs exactly one float per vertex");

        // The plane still folds the drop into z; the globe leaves the position alone, which is the
        // whole reason for the attribute.
        auto lowestZ = [](const std::shared_ptr<TileSurface>& surface) {
            const TileSurface::VertexGeometryLayoutParameters& layout = surface->getVertexGeometryLayoutParameters();
            const VertexArray<std::uint8_t>& data = surface->getVertexGeometry();
            float lowest = 0;
            for (std::size_t i = 0; i + layout.vertexSize <= data.size(); i += layout.vertexSize) {
                const float* coord = reinterpret_cast<const float*>(&data[i] + layout.coordOffset);
                lowest = std::min(lowest, coord[2]);
            }
            return lowest;
        };
        TEST_CHECK(lowestZ(planarSurfaces.front()) < -900000.0f, "the plane still encodes a skirt as a sentinel z");
        TEST_CHECK(lowestZ(sphereSurfaces.front()) > -900000.0f, "the globe never does, so its vertex stays on the sphere");
    }

    /*
     * A height on a sphere is radial: unlike the plane, it does NOT grow with latitude. The shader
     * relies on this by setting the Mercator terms to zero so its cosh is 1.
     */
    void testSphericalHeightHasNoLatitudeStretch() {
        SphericalTileTransformer sphere(static_cast<float>(WORLD_SIZE / 3.1415926535897932));
        DefaultTileTransformer planar(static_cast<float>(WORLD_SIZE));

        // Two tiles at very different latitudes, same zoom.
        std::shared_ptr<const TileTransformer::VertexTransformer> equator = sphere.createTileVertexTransformer(TileId(5, 16, 16));
        std::shared_ptr<const TileTransformer::VertexTransformer> north = sphere.createTileVertexTransformer(TileId(5, 16, 4));
        float atEquator = equator->calculateHeight(cglib::vec2<float>(0.5f, 0.5f), 1000.0f);
        float atNorth = north->calculateHeight(cglib::vec2<float>(0.5f, 0.5f), 1000.0f);
        TEST_CHECK(nearly(atEquator, atNorth, 1.0e-9), "a spherical height is the same at any latitude");

        // The plane's is not, which is exactly the term the shader's cosh supplies there.
        std::shared_ptr<const TileTransformer::VertexTransformer> planarNorth = planar.createTileVertexTransformer(TileId(5, 16, 4));
        std::shared_ptr<const TileTransformer::VertexTransformer> planarEquator = planar.createTileVertexTransformer(TileId(5, 16, 16));
        TEST_CHECK(planarNorth->calculateHeight(cglib::vec2<float>(0.5f, 0.5f), 1000.0f) >
                   planarEquator->calculateHeight(cglib::vec2<float>(0.5f, 0.5f), 1000.0f) * 1.2f,
                   "while the plane's grows with latitude, which is why only one of them needs the cosh");

        // And it is the 2 * PI tile-local convention, not the plane's.
        double expected = 1000.0 * (1 << 5) / EARTH_CIRCUMFERENCE * 2 * 3.1415926535897932;
        TEST_CHECK(nearly(atEquator, expected, 1.0e-6), "the spherical height is calculateHeight's own 2 * PI convention");
    }

    /**
     * uBaseScale on a sphere. An extrusion's base arrives in INTERNAL z units, which carry
     * Mercator's 1/cos(latitude) stretch, and terrainTexture.metersToInternal is the EQUATOR value -
     * so the conversion owes a cosh the planar path takes per vertex through vElevCosh. Without it
     * a building's base rose 1.52x the ground under it at Paris and its walls stretched.
     */
    void testTheExtrusionBaseRisesWithItsGround() {
        const double PI = 3.1415926535897932;
        SphericalTileTransformer sphere(static_cast<float>(WORLD_SIZE / PI));
        const TileId tileId(14, 8300, 5636); // Paris

        // The tile-centre latitude, DefaultTileTransformer::calculateHeight's own formula.
        double mercY = 2 * PI * ((tileId.y + 0.5) / (1 << tileId.zoom) - 0.5);
        double latitude = PI * 0.5 - 2 * std::atan(std::exp(mercY));
        TEST_CHECK(nearly(std::cosh(mercY), 1.0 / std::cos(latitude), 1.0e-9),
                   "the cosh of the tile-centre mercator y IS the 1/cos(latitude) stretch");
        TEST_CHECK(std::cosh(mercY) > 1.4, "and at Paris it is worth ~1.5, not a rounding term");

        // The two conversions the shader adds together: the ground's, straight from metres, and the
        // base's, through the internal z units the CPU resolved it in.
        cglib::mat4x4<double> frame = sphere.calculateTileMatrix(tileId, 1.0f);
        double metersToFrame = sphericalMetersToFrame(sphere, tileId, frame);
        double metersToInternalEquator = WORLD_SIZE / EARTH_CIRCUMFERENCE;
        double baseScale = metersToFrame / (metersToInternalEquator * std::cosh(mercY));

        double elevation = 35.0; // metres, about what Paris stands at
        double baseInternal = elevation * metersToInternalEquator / std::cos(latitude);
        TEST_CHECK(nearly(baseInternal * baseScale, elevation * metersToFrame, 1.0e-9),
                   "a base of N metres lands exactly where a ground of N metres does");

        // Which the equator-only divisor did not: that is the bug, and its size.
        double wrongScale = metersToFrame / metersToInternalEquator;
        TEST_CHECK(nearly(baseInternal * wrongScale, elevation * metersToFrame * std::cosh(mercY), 1.0e-9),
                   "without the cosh it lands 1/cos(latitude) too high");
    }

    /**
     * A globe ground tile was TWO triangles at every zoom from 6 up, so the terrain that displaces
     * it was sampled at the tile's four corners and the drape stretched across the chord between
     * them. The curvature tesselation only splits an edge longer than 1/64 of the equator; the
     * lattice is what the plane has had all along. maplibre floors the same number at 32
     * (vertical_perspective_projection.ts, "visibly warped at high zooms").
     */
    void testTheGroundLatticeIsNotLeftToTheCurvatureSplit() {
        const double PI = 3.1415926535897932;
        const double radius = WORLD_SIZE / PI;
        auto sphere = std::make_shared<SphericalTileTransformer>(static_cast<float>(radius));
        const TileId tileId(14, 8300, 5636); // Paris

        auto positions = [](const std::shared_ptr<TileSurface>& surface) {
            const TileSurface::VertexGeometryLayoutParameters& layout = surface->getVertexGeometryLayoutParameters();
            std::vector<cglib::vec3<double>> points;
            for (std::size_t offset = layout.coordOffset; offset + 12 <= surface->getVertexGeometry().size(); offset += layout.vertexSize) {
                const float* coord = reinterpret_cast<const float*>(&surface->getVertexGeometry()[offset]);
                points.emplace_back(coord[0], coord[1], coord[2]);
            }
            return points;
        };

        TileSurfaceBuilder plain(sphere);
        std::vector<std::shared_ptr<TileSurface>> plainSurfaces = plain.buildTileSurface(tileId);
        TEST_CHECK(plainSurfaces.size() == 1 && plainSurfaces[0]->getIndicesCount() == 6,
                   "without a lattice the whole tile is two triangles");
        TEST_CHECK(positions(plainSurfaces[0]).size() <= 6, "its vertices are corners - nothing inside the tile");

        TileSurfaceBuilder grid(sphere);
        grid.setGridResolution(64);
        std::vector<std::shared_ptr<TileSurface>> gridSurfaces = grid.buildTileSurface(tileId);
        unsigned int indices = 0;
        for (const std::shared_ptr<TileSurface>& surface : gridSurfaces) {
            indices += surface->getIndicesCount();
        }
        TEST_CHECK(indices == 64 * 64 * 2 * 3, "the lattice is the plane's own: 64 cells per side");

        // And every one of its vertices is ON the ball - the point of building them on the CPU.
        double worst = 0;
        for (const std::shared_ptr<TileSurface>& surface : gridSurfaces) {
            for (const cglib::vec3<double>& point : positions(surface)) {
                worst = std::max(worst, std::fabs(cglib::length(point) - radius));
            }
        }
        TEST_CHECK(worst < radius * 1.0e-6, "every lattice vertex sits on the sphere, not on a chord");
    }

    /*
     * The inversion above is exercised in DOUBLE, and it is exact there. The GPU runs it in fp32,
     * where forming the absolute unit-sphere point first is fatal: a tile is 1e-5 of the sphere, so
     * the tile-local detail is below the ulp of an O(1) coordinate. That is the shaky drape, the
     * break at every tile border and the buildings cut wrong at one - all three from one function.
     * terrainSphereMercatorDelta keeps the offset small end to end; both forms are reproduced here
     * exactly as the GLSL writes them, in float.
     */
    void testTheShaderInversionSurvivesFloatPrecision() {
        const double PI = 3.1415926535897932;
        SphericalTileTransformer sphere(static_cast<float>(WORLD_SIZE / PI));
        double sphereRadius = sphere.calculateTileMatrix(TileId(0, 0, 0), 1.0f)(0, 0);
        const TileId tileIds[] = { TileId(14, 8300, 5636), TileId(16, 33202, 22546), TileId(18, 132808, 90185) };

        double worstAbsolute = 0, worstRelative = 0;
        for (const TileId& tileId : tileIds) {
            cglib::mat4x4<double> frame = sphere.calculateTileMatrix(tileId, 1.0f);
            cglib::vec3<float> o(static_cast<float>(frame(0, 3) / sphereRadius), static_cast<float>(frame(1, 3) / sphereRadius), static_cast<float>(frame(2, 3) / sphereRadius));
            cglib::vec3<float> scale(static_cast<float>(frame(0, 0) / sphereRadius), static_cast<float>(frame(1, 1) / sphereRadius), static_cast<float>(frame(2, 2) / sphereRadius));

            // sphereFrameMercator, and the tile origin the uv uniform carries in either form.
            cglib::vec3<double> od(frame(0, 3) / sphereRadius, frame(1, 3) / sphereRadius, frame(2, 3) / sphereRadius);
            double orz = od(2) / cglib::length(od);
            cglib::vec2<double> frameMerc(std::atan2(od(1), od(0)), 0.5 * std::log((1.0 + orz) / (1.0 - orz)));
            double tileCount = static_cast<double>(1 << tileId.zoom);
            double tileMercX = (tileId.x / tileCount - 0.5) * 2 * PI;
            double tileMercY = ((tileCount - 1 - tileId.y) / tileCount - 0.5) * 2 * PI;
            float invTileSize = static_cast<float>(tileCount / (2 * PI));

            std::shared_ptr<const TileTransformer::VertexTransformer> vertexTransformer = sphere.createTileVertexTransformer(tileId);
            for (int j = 1; j < 16; j++) {
                for (int i = 1; i < 16; i++) {
                    float u = i / 16.0f, v = j / 16.0f;
                    cglib::vec3<float> pos = vertexTransformer->calculatePoint(cglib::vec2<float>(u, v));
                    cglib::vec3<float> d(pos(0) * scale(0), pos(1) * scale(1), pos(2) * scale(2));

                    // What it used to do: p = o + d, invert, subtract an O(1) Mercator origin.
                    cglib::vec3<float> p = o + d;
                    float len = std::sqrt(p(0) * p(0) + p(1) * p(1) + p(2) * p(2));
                    float rz = std::min(0.999999f, std::max(-0.999999f, p(2) / len));
                    float ax = std::atan2(p(1), p(0)) - static_cast<float>(tileMercX);
                    float ay = 0.5f * std::log((1.0f + rz) / (1.0f - rz)) - static_cast<float>(tileMercY);
                    ax -= 6.283185307179586f * std::floor(ax * 0.15915494309189535f + 0.5f);
                    worstAbsolute = std::max(worstAbsolute, static_cast<double>(std::max(std::fabs(ax * invTileSize - u), std::fabs(ay * invTileSize - (1.0f - v)))));

                    // terrainSphereMercatorDelta: every quantity stays the size of the tile.
                    float od2 = o(0) * d(0) + o(1) * d(1) + o(2) * d(2), dd = d(0) * d(0) + d(1) * d(1) + d(2) * d(2);
                    float dlen = std::sqrt(1.0f + 2.0f * od2 + dd);
                    float lenM1 = (2.0f * od2 + dd) / (dlen + 1.0f);
                    float dsz = (d(2) - o(2) * lenM1) / dlen;
                    float x = dsz / std::max(1.0e-6f, 1.0f - o(2) * (o(2) + dsz));
                    float dMercY = std::fabs(x) < 0.01f ? x * (1.0f + x * x * 0.33333333f) : 0.5f * std::log((1.0f + x) / (1.0f - x));
                    float dLon = std::atan2(o(0) * d(1) - o(1) * d(0), o(0) * (o(0) + d(0)) + o(1) * (o(1) + d(1)));
                    float rx = dLon - static_cast<float>(tileMercX - frameMerc(0));
                    float ry = dMercY - static_cast<float>(tileMercY - frameMerc(1));
                    worstRelative = std::max(worstRelative, static_cast<double>(std::max(std::fabs(rx * invTileSize - u), std::fabs(ry * invTileSize - (1.0f - v)))));
                }
            }
        }
        // Measured against a 1024-texel drape tile, which is what the error is visible in.
        TEST_CHECK(worstAbsolute * 1024 > 2.0, "the absolute form is out by drape TEXELS by zoom 18");
        TEST_CHECK(worstRelative * 1024 < 0.01, "the relative one holds a hundredth of a texel at every zoom");
    }

    /*
     * An extrusion reads the ground at ONE point per building. TileLayerBuilder stores that point
     * and GLTileRenderer::resolveExtrusionBases reads it back through SpanResolver::tileMatrix2D,
     * which is a FLAT tile matrix - so what is stored has to be the tile's unit square, y flipped.
     * calculatePoint IS that on a plane, and a curved position on a globe: every building then read
     * its base kilometres away, a different distance per tile, which steps at every tile border and
     * grows with the tile. Hence the flip written out rather than taken from the transformer.
     */
    void testTheExtrusionAnchorIsTheTileSquareAndNotACurvedPoint() {
        const double PI = 3.1415926535897932;
        SphericalTileTransformer sphere(static_cast<float>(WORLD_SIZE / PI));
        DefaultTileTransformer plane(static_cast<float>(WORLD_SIZE));
        const TileId tileId(16, 33202, 22546); // Paris

        std::shared_ptr<const TileTransformer::VertexTransformer> sphereVertex = sphere.createTileVertexTransformer(tileId);
        std::shared_ptr<const TileTransformer::VertexTransformer> planeVertex = plane.createTileVertexTransformer(tileId);

        bool planarUnchanged = true;
        double worstCurved = 0, worstFlat = 0;
        for (int j = 0; j <= 4; j++) {
            for (int i = 0; i <= 4; i++) {
                cglib::vec2<float> uv(i / 4.0f, j / 4.0f);
                cglib::vec2<float> flat(uv(0), 1.0f - uv(1)); // what TileLayerBuilder stores now

                // resolveExtrusionBases un-flips: (x, 1 - y) has to come back to the uv it was built at.
                cglib::vec3<float> curved = sphereVertex->calculatePoint(uv);
                worstCurved = std::max(worstCurved, static_cast<double>(std::max(std::fabs(curved(0) - uv(0)), std::fabs((1.0f - curved(1)) - uv(1)))));
                worstFlat = std::max(worstFlat, static_cast<double>(std::max(std::fabs(flat(0) - uv(0)), std::fabs((1.0f - flat(1)) - uv(1)))));

                // ...and on a plane the flip IS calculatePoint, so nothing about the plane moves.
                cglib::vec3<float> planar = planeVertex->calculatePoint(uv);
                planarUnchanged = planarUnchanged && planar(0) == flat(0) && planar(1) == flat(1);
            }
        }
        TEST_CHECK(worstFlat == 0.0, "the flipped unit square round-trips to the tile uv, exactly");
        TEST_CHECK(worstCurved > 1.0, "the curved point is whole TILES away from it");
        TEST_CHECK(planarUnchanged, "and on a plane the two are the same point, so nothing there changes");
    }

    /*
     * The antimeridian wrap the relative form still needs. terrainSphereMercatorDelta's longitude
     * comes from atan, so it is the true offset modulo 2pi - and a COARSE STAND-IN frame (an
     * ancestor serving a finer target while it loads) sits up to a world away in longitude, so most
     * of its vertices wrap. The uv uniform is measured from the same frame and wrapped the same
     * way, so their difference is right modulo 2pi and the small answer comes back by wrapping it.
     * Dropped once, on the argument that a relative form has nothing to wrap: the ground smeared
     * across whole tiles at low zoom, because the uv landed a world outside the texture.
     */
    void testACoarseFrameStillNeedsTheAntimeridianWrap() {
        const double PI = 3.1415926535897932;
        SphericalTileTransformer sphere(static_cast<float>(WORLD_SIZE / PI));
        double sphereRadius = sphere.calculateTileMatrix(TileId(0, 0, 0), 1.0f)(0, 0);
        const TileId frameTileId(1, 0, 0); // origin at longitude 180, which every vertex is far from

        cglib::mat4x4<double> frame = sphere.calculateTileMatrix(frameTileId, 1.0f);
        cglib::vec3<float> o(static_cast<float>(frame(0, 3) / sphereRadius), static_cast<float>(frame(1, 3) / sphereRadius), static_cast<float>(frame(2, 3) / sphereRadius));
        cglib::vec3<float> scale(static_cast<float>(frame(0, 0) / sphereRadius), static_cast<float>(frame(1, 1) / sphereRadius), static_cast<float>(frame(2, 2) / sphereRadius));
        double frameLon = std::atan2(frame(1, 3), frame(0, 3));

        std::shared_ptr<const TileTransformer::VertexTransformer> vertexTransformer = sphere.createTileVertexTransformer(frameTileId);
        int wrapped = 0, total = 0;
        double worstUnwrapped = 0, worstWrapped = 0;
        for (int j = 0; j <= 8; j++) {
            for (int i = 0; i <= 8; i++) {
                cglib::vec3<float> pos = vertexTransformer->calculatePoint(cglib::vec2<float>(i / 8.0f, j / 8.0f));
                cglib::vec3<float> d(pos(0) * scale(0), pos(1) * scale(1), pos(2) * scale(2));

                double trueDelta = std::atan2(static_cast<double>(o(1) + d(1)), static_cast<double>(o(0) + d(0))) - frameLon;
                float dLon = std::atan2(o(0) * d(1) - o(1) * d(0), o(0) * (o(0) + d(0)) + o(1) * (o(1) + d(1)));

                // terrainSphereRelative, against an origin ON the frame - so the answer IS trueDelta.
                float merc = dLon - 0.0f;
                merc -= 6.283185307179586f * std::floor(merc * 0.15915494309189535f + 0.5f);
                double target = trueDelta - 6.283185307179586 * std::floor(trueDelta * 0.15915494309189535 + 0.5);

                total++;
                if (std::fabs(dLon - trueDelta) > 1.0) {
                    wrapped++;
                }
                worstUnwrapped = std::max(worstUnwrapped, std::fabs(dLon - trueDelta));
                // A vertex exactly half a world away wraps either way, and one does at u = 0.5 here.
                if (std::fabs(std::fabs(target) - PI) > 1.0e-3) {
                    worstWrapped = std::max(worstWrapped, std::fabs(merc - target));
                }
            }
        }
        TEST_CHECK(wrapped * 2 > total, "most vertices of a coarse frame come back a whole world out");
        TEST_CHECK(worstUnwrapped > 5.0, "and the raw offset is out by 2pi, not by a rounding term");
        TEST_CHECK(worstWrapped < 1.0e-5, "the wrap puts every one of them back");
    }
}

void testSphericalTerrain() {
    testOnlyTheSphereReportsItself();
    testTheShaderInversionRecoversTheTileUV();
    testNeighbouringTilesSampleTheSharedEdgeIdentically();
    testTheSkirtAttributeIsGlobeOnly();
    testSphericalHeightHasNoLatitudeStretch();
    testTheExtrusionBaseRisesWithItsGround();
    testTheGroundLatticeIsNotLeftToTheCurvatureSplit();
    testTheShaderInversionSurvivesFloatPrecision();
    testTheExtrusionAnchorIsTheTileSquareAndNotACurvedPoint();
    testACoarseFrameStillNeedsTheAntimeridianWrap();
}
