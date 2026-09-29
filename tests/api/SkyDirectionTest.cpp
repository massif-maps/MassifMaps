// The sky placement CelestialRenderer draws with and its inverse, what `sky.clicked` reports (celestial/SkyDirection.h).
// Not covered: CelestialRenderer's own GL paths, and TouchHandler routing a sky tap to the layers - both need the renderer.

#include "celestial/SkyDirection.h"
#include "projections/EPSG3857.h"
#include "projections/PlanarProjectionSurface.h"
#include "projections/SphericalProjectionSurface.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif;

namespace {
    // The placement CelestialRenderer does.
    cglib::vec3<double> worldDirection(const ProjectionSurface& surface, const MapPos& mapPos, double azimuthDeg, double altitudeDeg) {
        double az = azimuthDeg * Const::DEG_TO_RAD;
        double alt = altitudeDeg * Const::DEG_TO_RAD;
        return SkyFrame(surface, mapPos).toWorld(cglib::vec3<double>(std::cos(alt) * std::sin(az), std::cos(alt) * std::cos(az), std::sin(alt)));
    }

    // Read against the surface normal, not the frame's own up, so a tilted frame cannot pass.
    bool drawnAtTrueAltitude(const ProjectionSurface& surface, const MapPos& mapPos) {
        cglib::vec3<double> normal = cglib::unit(surface.calculateNormal(mapPos));
        const double directions[][2] = { { 0, 5 }, { 90, 30 }, { 200, 45 }, { 300, 70 } };
        for (const auto& direction : directions) {
            double drawn = std::asin(cglib::dot_product(cglib::unit(worldDirection(surface, mapPos, direction[0], direction[1])), normal)) * Const::RAD_TO_DEG;
            if (std::abs(drawn - direction[1]) > 1.0e-6) {
                return false;
            }
        }
        return true;
    }

    // Two stars 90 degrees apart must stay 90 degrees apart, or a constellation is drawn distorted.
    bool keepsSeparation(const ProjectionSurface& surface, const MapPos& mapPos) {
        cglib::vec3<double> first = worldDirection(surface, mapPos, 0, 30);
        cglib::vec3<double> second = worldDirection(surface, mapPos, 180, 60);
        double separation = std::acos(cglib::dot_product(first, second) / (cglib::norm(first) * cglib::norm(second))) * Const::RAD_TO_DEG;
        return std::abs(separation - 90) < 1.0e-6;
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

    TEST_CHECK(drawnAtTrueAltitude(planar, grenoble), "on the plane, a direction at altitude A is drawn A above the horizon");
    TEST_CHECK(drawnAtTrueAltitude(spherical, grenoble), "and on the globe, where the surface's up is 1/cos(lat) longer than east and north");
    TEST_CHECK(keepsSeparation(spherical, grenoble), "on the globe, two directions 90 degrees apart are drawn 90 degrees apart");

    float azimuth = 0, altitude = 0;
    CalculateSkyDirection(planar, grenoble, worldDirection(planar, grenoble, 300, 20), azimuth, altitude);
    TEST_CHECK(azimuth >= 0 && azimuth < 360 && std::abs(azimuth - 300) < 1.0e-3, "a westerly azimuth comes back as 0..360, not negative");
}
