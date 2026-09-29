// A tap's direction read back as azimuth/altitude (celestial/SkyDirection.h), what `sky.clicked` reports.
// Not covered: TouchHandler routing a sky tap to the layers, which needs the renderer.

#include "celestial/SkyDirection.h"
#include "projections/EPSG3857.h"
#include "projections/PlanarProjectionSurface.h"
#include "projections/SphericalProjectionSurface.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif;

namespace {
    // The placement CelestialRenderer does: local x east, y north, z up, turned into a world vector.
    cglib::vec3<double> worldDirection(const ProjectionSurface& surface, const MapPos& mapPos, double azimuthDeg, double altitudeDeg) {
        double az = azimuthDeg * Const::DEG_TO_RAD;
        double alt = altitudeDeg * Const::DEG_TO_RAD;
        return surface.calculateVector(mapPos, MapVec(std::cos(alt) * std::sin(az), std::cos(alt) * std::cos(az), std::sin(alt)));
    }

    bool roundTrips(const ProjectionSurface& surface, const MapPos& mapPos) {
        const double directions[][2] = { { 0, 30 }, { 90, 10 }, { 200, 60 }, { 359, 5 }, { 45, 89 } };
        for (const auto& direction : directions) {
            float azimuth = 0, altitude = 0;
            CalculateSkyDirection(surface, mapPos, worldDirection(surface, mapPos, direction[0], direction[1]), azimuth, altitude);
            double azimuthError = std::abs(std::remainder(azimuth - direction[0], 360.0));
            if (azimuthError > 1.0e-3 || std::abs(altitude - direction[1]) > 1.0e-3) {
                return false;
            }
        }
        return true;
    }
}

void testSkyDirection() {
    EPSG3857 proj;
    MapPos grenoble = proj.toInternal(proj.fromWgs84(MapPos(5.72, 45.19, 0)));

    PlanarProjectionSurface planar;
    TEST_CHECK(roundTrips(planar, grenoble), "on the plane, a placed direction reads back as the same azimuth and altitude");

    SphericalProjectionSurface spherical;
    TEST_CHECK(roundTrips(spherical, grenoble), "and on the globe, where the local frame turns with the place");

    float azimuth = 0, altitude = 0;
    CalculateSkyDirection(planar, grenoble, worldDirection(planar, grenoble, 300, 20), azimuth, altitude);
    TEST_CHECK(azimuth >= 0 && azimuth < 360 && std::abs(azimuth - 300) < 1.0e-3, "a westerly azimuth comes back as 0..360, not negative");
}
