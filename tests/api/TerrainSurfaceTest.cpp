/*
 * Tests for the terrain projection surface as a DECORATOR (all/native/terrain/TerrainProjectionSurface.h):
 * that it adds a height to whatever base surface it is given and forwards the shape, so the same
 * class serves the plane and the globe.
 *
 * The load-bearing checks are the planar ones. Terrain is planar-only today - TileLayer and
 * VectorLayer both still refuse to build it on a globe - so every planar result here has to be the
 * number the inherit-from-planar version produced, or this refactor changed the shipping map.
 *
 * NOT covered here: ElevationManager. The surface takes the light ElevationProvider interface, so
 * these run against a synthetic height field; that real DEM data drives it the same way is a device
 * check. Nor is picking covered on a globe - intersectRay marches the planar frame and is P4.
 * See docs/internals/rendering/18-globe.md.
 */

#include "components/ElevationProvider.h"
#include "projections/EPSG3857.h"
#include "projections/PlanarProjectionSurface.h"
#include "projections/SphericalProjectionSurface.h"
#include "terrain/TerrainProjectionSurface.h"
#include "utils/Const.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace massif;

#include "TestCheck.h"

namespace {

    bool nearly(double value, double expected, double tolerance = 1.0e-9) {
        return std::fabs(value - expected) <= tolerance * std::max(1.0, std::fabs(expected));
    }

    /** A height field with a known slope, so the gradient-derived normal has a value to check. */
    struct RampProvider : public ElevationProvider {
        double height = 0;
        double slopeX = 0;
        double slopeY = 0;
        bool hit = false;

        virtual double getDisplayHeight(double internalX, double internalY) const override {
            return height + slopeX * internalX + slopeY * internalY;
        }
        virtual void getDisplayGradient(double, double, double& dhdx, double& dhdy) const override {
            dhdx = slopeX;
            dhdy = slopeY;
        }
        virtual int getMaxDataZoom() const override { return 12; }
        virtual bool intersectRay(const cglib::ray3<double>&, double& t) const override {
            if (!hit) {
                return false;
            }
            t = 0.25;
            return true;
        }
        virtual void getMinMaxDisplayHeight(const MapTile&, double& minZ, double& maxZ) const override {
            minZ = 0;
            maxZ = height;
        }
        virtual unsigned int getVersion() const override { return 7; }
    };

    MapPos internalOf(double lon, double lat, double z = 0) {
        EPSG3857 proj;
        MapPos internal = proj.toInternal(proj.fromWgs84(MapPos(lon, lat, 0)));
        return MapPos(internal.getX(), internal.getY(), z);
    }

    /*
     * The regression that matters: over a planar base, every result is what the old
     * inherit-from-PlanarProjectionSurface implementation produced.
     */
    void testOverAPlaneItIsTheOldFlatBehaviour() {
        auto elevation = std::make_shared<RampProvider>();
        elevation->height = 1234.0;
        auto base = std::make_shared<PlanarProjectionSurface>();
        TerrainProjectionSurface terrain(base, elevation);

        MapPos mapPos = internalOf(7.05, 45.9, 50.0);
        cglib::vec3<double> pos = terrain.calculatePosition(mapPos);
        // The old code: (x, y, z + terrainZ + lift). x and y untouched, z lifted by the height.
        TEST_CHECK(pos(0) == mapPos.getX() && pos(1) == mapPos.getY(),
                   "a planar terrain position leaves x and y exactly alone");
        TEST_CHECK(pos(2) > mapPos.getZ() + 1234.0 && pos(2) < mapPos.getZ() + 1234.0 + 1.0,
                   "and lifts z by the terrain height plus the small drape lift");

        // Round trip: the height it added is the height it takes back off.
        MapPos back = terrain.calculateMapPos(pos);
        TEST_CHECK(nearly(back.getX(), mapPos.getX()) && nearly(back.getY(), mapPos.getY()) && nearly(back.getZ(), mapPos.getZ()),
                   "and mapping the position back returns the height above terrain it started with");

        // On flat ground the normal is straight up, exactly as the flat surface returned.
        cglib::vec3<double> normal = terrain.calculateNormal(mapPos);
        TEST_CHECK(nearly(normal(0), 0.0) && nearly(normal(1), 0.0) && nearly(normal(2), 1.0),
                   "over flat ground the normal is (0, 0, 1)");
    }

    void testTheNormalFollowsTheSlope() {
        auto elevation = std::make_shared<RampProvider>();
        elevation->slopeX = 0.5;
        auto base = std::make_shared<PlanarProjectionSurface>();
        TerrainProjectionSurface terrain(base, elevation);

        cglib::vec3<double> normal = terrain.calculateNormal(internalOf(7.05, 45.9));
        TEST_CHECK(nearly(cglib::length(normal), 1.0), "the slope normal is a unit vector");
        TEST_CHECK(normal(0) < -0.4 && normal(2) > 0,
                   "and tilts away from an uphill slope while still pointing up");
    }

    /*
     * The point of the refactor: the same class over a globe base puts the element on the SPHERE at
     * the terrain height, instead of on a plane. Nothing wires this up yet - both call sites still
     * refuse a spherical base - so this is what P3 turns on.
     */
    void testOverAGlobeItRidesTheSphere() {
        auto elevation = std::make_shared<RampProvider>();
        elevation->height = 1000.0;
        auto base = std::make_shared<SphericalProjectionSurface>();
        TerrainProjectionSurface terrain(base, elevation);

        MapPos mapPos = internalOf(7.05, 45.9, 0);
        cglib::vec3<double> terrainPos = terrain.calculatePosition(mapPos);
        cglib::vec3<double> groundPos = base->calculatePosition(mapPos);

        TEST_CHECK(cglib::length(terrainPos) > cglib::length(groundPos),
                   "a point on globe terrain sits further from the planet centre than the ground");
        // It rose along the local up, not along the world z the plane would have used.
        cglib::vec3<double> rise = terrainPos - groundPos;
        TEST_CHECK(cglib::dot_product(cglib::unit(rise), cglib::unit(groundPos)) > 0.9999,
                   "and it rose along the surface normal, not along world z");

        MapPos back = terrain.calculateMapPos(terrainPos);
        TEST_CHECK(nearly(back.getX(), mapPos.getX(), 1.0e-6) && nearly(back.getY(), mapPos.getY(), 1.0e-6) && nearly(back.getZ(), mapPos.getZ(), 1.0e-6),
                   "and the globe round trip returns the same height above terrain");
    }

    /*
     * Subdivision composes: terrain refines for the relief, then the base refines for the shape.
     * The join points must not be duplicated - the flat surface never produced a repeated vertex,
     * and a zero-length segment is a degenerate line join.
     */
    void testSubdivisionComposesWithoutDuplicatingJoins() {
        auto elevation = std::make_shared<RampProvider>();
        auto planar = std::make_shared<PlanarProjectionSurface>();
        TerrainProjectionSurface terrain(planar, elevation);

        // Long enough that the terrain threshold splits it many times.
        MapPos a = internalOf(7.00, 45.9);
        MapPos b = internalOf(7.40, 45.9);
        std::vector<MapPos> points;
        terrain.tesselateSegment(a, b, points);

        TEST_CHECK(points.size() > 2, "a long segment over terrain is subdivided");
        bool noDuplicates = true;
        for (std::size_t i = 0; i + 1 < points.size(); i++) {
            noDuplicates = noDuplicates && !(points[i].getX() == points[i + 1].getX() && points[i].getY() == points[i + 1].getY());
        }
        TEST_CHECK(noDuplicates, "and no two consecutive points are the same, so no join is doubled");
        TEST_CHECK(points.front().getX() == a.getX() && points.back().getX() == b.getX(),
                   "the endpoints survive subdivision unchanged");

        // Over a globe the composed output has to be fine enough that no piece still needs curving.
        // It always is, and by a wide margin: the terrain threshold is a DEM texel (at most
        // WORLD_SIZE / 256 internal, ~156 km) while the sphere's is EARTH_CIRCUMFERENCE / 120
        // (~334 km), so the base's own subdivision provably never adds a point. The composition is
        // there so that stays true if either threshold moves, not because it fires today.
        auto spherical = std::make_shared<SphericalProjectionSurface>();
        TerrainProjectionSurface globeTerrain(spherical, elevation);
        std::vector<MapPos> globePoints;
        MapPos far0 = internalOf(-40, 10);
        MapPos far1 = internalOf(40, 10);
        globeTerrain.tesselateSegment(far0, far1, globePoints);

        double worldPerMeter = 2.0 * Const::WORLD_SIZE / Const::EARTH_CIRCUMFERENCE;
        double thresholdMeters = Const::EARTH_CIRCUMFERENCE / 120.0;
        bool allShortEnough = globePoints.size() > 1;
        for (std::size_t i = 0; i + 1 < globePoints.size(); i++) {
            double meters = spherical->calculateDistance(spherical->calculatePosition(globePoints[i]),
                                                         spherical->calculatePosition(globePoints[i + 1])) / worldPerMeter;
            allShortEnough = allShortEnough && meters < thresholdMeters;
        }
        TEST_CHECK(allShortEnough,
                   "over a globe base no output piece is longer than the sphere's own split threshold");
    }

    /*
     * A terrain hit wins over the base's, and a miss falls through to it. On a globe base the
     * fallback is the sphere rather than a plane, which is the whole reason this forwards.
     */
    void testPickingPrefersTheTerrainThenFallsBackToTheBase() {
        auto elevation = std::make_shared<RampProvider>();
        auto base = std::make_shared<SphericalProjectionSurface>();
        TerrainProjectionSurface terrain(base, elevation);

        cglib::vec3<double> surface = base->calculatePosition(internalOf(0, 0));
        cglib::vec3<double> outward = cglib::unit(surface);
        cglib::ray3<double> down(surface + outward * 1000.0, -outward);

        // The direction is a unit vector, so t is a world length: the 1000 units of altitude.
        double t = 0;
        elevation->hit = false;
        TEST_CHECK(terrain.calculateHitPoint(down, 0, t) && nearly(t, 1000.0, 1.0e-6),
                   "with no terrain hit the ray falls through to the globe's own surface");
        elevation->hit = true;
        TEST_CHECK(terrain.calculateHitPoint(down, 0, t) && t == 0.25,
                   "and a terrain hit wins outright");
    }

    void testTheVersionIsCapturedForTheRebuildCheck() {
        auto elevation = std::make_shared<RampProvider>();
        auto base = std::make_shared<PlanarProjectionSurface>();
        TerrainProjectionSurface terrain(base, elevation);
        TEST_CHECK(terrain.getElevationVersion() == 7, "the elevation version is captured at construction");
        TEST_CHECK(terrain.getBase() == base, "and the base is kept, so VectorLayer can tell one from another");
    }
}

void testTerrainSurface() {
    testOverAPlaneItIsTheOldFlatBehaviour();
    testTheNormalFollowsTheSlope();
    testOverAGlobeItRidesTheSphere();
    testSubdivisionComposesWithoutDuplicatingJoins();
    testPickingPrefersTheTerrainThenFallsBackToTheBase();
    testTheVersionIsCapturedForTheRebuildCheck();
}
