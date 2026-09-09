/*
 * Tests for the globe's projection surface (all/native/projections/SphericalProjectionSurface.h)
 * against the planar one, pinning the conventions the two share and the two they do not.
 *
 * These are the invariants terrain-on-globe rests on. An internal MapPos means the SAME place and
 * the SAME height on both surfaces, which is why every ElevationManager lookup - keyed by internal
 * x/y and returning a height in internal z - carries over to the globe unchanged. The two traps
 * below (the world scale, the non-unit normal) are what makes that not obvious.
 *
 * NOT covered here: anything that draws. Whether the globe renders, whether the sky's up vector
 * follows the surface, whether a DEM displaces the sphere - all past the host link and all device
 * checks. See docs/internals/rendering/18-globe.md.
 */

#include "projections/EPSG3857.h"
#include "projections/PlanarProjectionSurface.h"
#include "projections/SphericalProjectionSurface.h"
#include "utils/Const.h"

#include <cmath>

using namespace massif;

#include "TestCheck.h"

namespace {

    bool nearly(double value, double expected, double tolerance = 1.0e-9) {
        return std::fabs(value - expected) <= tolerance * std::max(1.0, std::fabs(expected));
    }

    // World units per metre at a position: the length of the local frame's up column. Not the
    // (2, 2) element - on the sphere that is up_z times the scale, and up_z is 1 only at the pole.
    double metersToWorld(const ProjectionSurface& surface, const cglib::vec3<double>& pos) {
        cglib::mat4x4<double> frame = surface.calculateLocalFrameMatrix(pos);
        return cglib::length(cglib::vec3<double>(frame(0, 2), frame(1, 2), frame(2, 2)));
    }

    // An internal MapPos for a WGS84 lon/lat, through the projection the SDK actually uses.
    MapPos internalOf(double lon, double lat, double heightMeters = 0) {
        EPSG3857 proj;
        MapPos internal = proj.toInternal(proj.fromWgs84(MapPos(lon, lat, 0)));
        return MapPos(internal.getX(), internal.getY(), heightMeters * Const::WORLD_SIZE / (Const::EARTH_CIRCUMFERENCE * std::cos(lat * Const::DEG_TO_RAD)));
    }

    /*
     * THE claim the whole globe/terrain plan rests on: in spherical mode the internal coordinates
     * are still Mercator, bit for bit the same numbers the planar mode uses. If this ever stops
     * holding, every elevation lookup keyed by internal x/y silently reads the wrong place on the
     * globe, and the DEM tile grids stop being shareable between the two modes.
     */
    void testInternalCoordinatesAreStillMercator() {
        SphericalProjectionSurface spherical;
        const double lons[] = { 0, 7.05, -122.4, 179.5 };
        const double lats[] = { 0, 45.9, -33.86, 71.2 };
        bool allMatch = true;
        for (double lon : lons) {
            for (double lat : lats) {
                MapPos expected = internalOf(lon, lat);
                MapPos actual = spherical.calculateMapPos(spherical.calculatePosition(expected));
                allMatch = allMatch && nearly(actual.getX(), expected.getX(), 1.0e-9) && nearly(actual.getY(), expected.getY(), 1.0e-9);
            }
        }
        TEST_CHECK(allMatch, "a spherical position maps back to the same internal Mercator x/y EPSG3857 gives");

        // And the mapping is the Mercator one, not merely self-consistent: y is atanh(sin lat)
        // scaled by WORLD_SIZE / 2pi, which is what makes a DEM tile grid address the same texels.
        MapPos paris = internalOf(2.3522, 48.8566);
        double expectedY = std::atanh(std::sin(48.8566 * Const::DEG_TO_RAD)) * Const::WORLD_SIZE / (2 * Const::PI);
        TEST_CHECK(nearly(paris.getY(), expectedY, 1.0e-9), "and that y really is atanh(sin lat), not some other vertical");
    }

    /*
     * The second half of the same claim: internal z means the same number of METRES above the
     * surface on both. The two surfaces disagree on world scale by exactly 2 (below), so this
     * only works because the factor is uniform - it cancels between the height and the radius.
     */
    void testHeightMeansTheSameMetresOnBothSurfaces() {
        PlanarProjectionSurface planar;
        SphericalProjectionSurface spherical;
        const double lats[] = { 0, 45.9, -33.86, 60.0 };
        bool allMatch = true;
        for (double lat : lats) {
            MapPos ground = internalOf(0, lat, 0);
            MapPos raised = internalOf(0, lat, 1000);

            // Planar: the height IS the world-space z, in units where 1 m is the local frame's scale.
            double planarMeters = (planar.calculatePosition(raised)(2) - planar.calculatePosition(ground)(2))
                                / metersToWorld(planar, planar.calculatePosition(ground));
            // Spherical: the height is a change of radius, in units where 1 m is its own frame's scale.
            double sphericalMeters = (cglib::length(spherical.calculatePosition(raised)) - cglib::length(spherical.calculatePosition(ground)))
                                   / metersToWorld(spherical, spherical.calculatePosition(ground));
            allMatch = allMatch && nearly(planarMeters, 1000.0, 1.0e-6) && nearly(sphericalMeters, 1000.0, 1.0e-6);
        }
        TEST_CHECK(allMatch, "1000 m of internal z lifts the surface by 1000 m on the plane and on the sphere alike");
    }

    /*
     * THE trap. The sphere has radius WORLD_SIZE / pi, so its equator is 2 * WORLD_SIZE of world
     * length while the planar map's equator is WORLD_SIZE. Every world-space LENGTH - the terrain
     * split thresholds, the camera clearance shell, the auto-flatten parallax - is therefore off
     * by exactly 2 if it is carried between the modes as a raw number.
     */
    void testTheSphericalWorldIsTwiceThePlanarScale() {
        PlanarProjectionSurface planar;
        SphericalProjectionSurface spherical;
        MapPos a = internalOf(0, 0);
        MapPos b = internalOf(0.5, 0);
        double planarDistance = planar.calculateDistance(planar.calculatePosition(a), planar.calculatePosition(b));
        double sphericalDistance = spherical.calculateDistance(spherical.calculatePosition(a), spherical.calculatePosition(b));
        TEST_CHECK(nearly(sphericalDistance / planarDistance, 2.0, 1.0e-6),
                   "the same two points are twice as far apart in the globe's world space");

        // The scale is uniform, so the local frame carries the same factor - which is why a
        // distance converted THROUGH the frame agrees and a raw one does not.
        double planarMeterScale = metersToWorld(planar, planar.calculatePosition(a));
        double sphericalMeterScale = metersToWorld(spherical, spherical.calculatePosition(a));
        TEST_CHECK(nearly(sphericalMeterScale / planarMeterScale, 2.0, 1.0e-6),
                   "and one metre is twice as many world units, so the two cancel");
        TEST_CHECK(nearly(sphericalDistance / sphericalMeterScale, planarDistance / planarMeterScale, 1.0e-6),
                   "leaving the distance in metres identical");
    }

    /*
     * The other trap, and a live one: calculateNormal returns InternalToSpherical, whose length is
     * 1 + height - NOT a unit vector once the position is off the surface. ViewState's
     * getFocusPosNormal hands that straight to SolidRenderer and BackgroundRenderer as a light
     * direction, so a camera focused above sea level scales the lighting by its own altitude.
     * The planar surface returns (0, 0, 1) always, which is why this never showed up.
     */
    void testTheNormalIsOnlyUnitLengthOnTheSurface() {
        SphericalProjectionSurface spherical;
        TEST_CHECK(nearly(cglib::length(spherical.calculateNormal(internalOf(7.05, 45.9, 0))), 1.0, 1.0e-9),
                   "on the surface the normal is a unit vector");
        double raisedLength = cglib::length(spherical.calculateNormal(internalOf(7.05, 45.9, 100000)));
        TEST_CHECK(raisedLength > 1.0 + 1.0e-6,
                   "100 km up it is longer than one, so a consumer must normalize it");

        // The local frame's up column, by contrast, IS normalized - so that is the safe source.
        TEST_CHECK(nearly(metersToWorld(spherical, spherical.calculatePosition(internalOf(7.05, 45.9, 100000))),
                          metersToWorld(spherical, spherical.calculatePosition(internalOf(7.05, 45.9, 0))), 1.0e-9),
                   "the local frame's up column keeps its scale at altitude");
    }

    void testTheLocalFrameIsOrthogonalAndEastNorthUp() {
        SphericalProjectionSurface spherical;
        cglib::vec3<double> pos = spherical.calculatePosition(internalOf(7.05, 45.9));
        cglib::mat4x4<double> frame = spherical.calculateLocalFrameMatrix(pos);
        cglib::vec3<double> east(frame(0, 0), frame(1, 0), frame(2, 0));
        cglib::vec3<double> north(frame(0, 1), frame(1, 1), frame(2, 1));
        cglib::vec3<double> up(frame(0, 2), frame(1, 2), frame(2, 2));
        TEST_CHECK(nearly(cglib::dot_product(east, north), 0.0, 1.0e-9) &&
                   nearly(cglib::dot_product(east, up), 0.0, 1.0e-9) &&
                   nearly(cglib::dot_product(north, up), 0.0, 1.0e-9),
                   "the local frame's three axes are mutually orthogonal");
        TEST_CHECK(cglib::dot_product(up, cglib::unit(pos)) > 0,
                   "up points away from the planet centre");
        TEST_CHECK(north(2) > 0, "north points towards the pole in the northern hemisphere");
        TEST_CHECK(nearly(frame(0, 3), pos(0), 1.0e-9) && nearly(frame(1, 3), pos(1), 1.0e-9) && nearly(frame(2, 3), pos(2), 1.0e-9),
                   "and its translation is the position it was built at");
    }

    /*
     * calculateHitPoint solves the infinite LINE against the sphere, so a ray aimed away from the
     * planet still reports a hit - behind the camera, at negative t. Every caller has to test t,
     * and ViewState::screenToWorld is the one that does. Pinned because a globe consumer that
     * copies the planar caller (where the plane is always ahead below the horizon) will not.
     */
    void testAHitBehindTheCameraIsReportedAsAHit() {
        SphericalProjectionSurface spherical;
        cglib::vec3<double> surface = spherical.calculatePosition(internalOf(0, 0));
        cglib::vec3<double> outward = cglib::unit(surface);
        cglib::vec3<double> camera = surface + outward * 1000.0;

        double t = 0;
        TEST_CHECK(spherical.calculateHitPoint(cglib::ray3<double>(camera, -outward), 0, t) && t > 0,
                   "a ray aimed at the planet hits it ahead of the camera");
        t = 0;
        TEST_CHECK(spherical.calculateHitPoint(cglib::ray3<double>(camera, outward), 0, t) && t < 0,
                   "a ray aimed at space still reports a hit, behind the camera - callers must check t");

        // A ray that misses the sphere entirely: no real root, so this one really is false.
        cglib::vec3<double> tangent(-outward(1), outward(0), 0);
        t = 0;
        TEST_CHECK(!spherical.calculateHitPoint(cglib::ray3<double>(camera + tangent * 1.0e7, tangent), 0, t),
                   "a ray whose line misses the planet is refused");
    }

    void testTheHitHeightSelectsTheShell() {
        SphericalProjectionSurface spherical;
        cglib::vec3<double> surface = spherical.calculatePosition(internalOf(0, 0));
        cglib::vec3<double> outward = cglib::unit(surface);
        cglib::vec3<double> camera = surface + outward * 100000.0;

        double tGround = 0, tShell = 0;
        spherical.calculateHitPoint(cglib::ray3<double>(camera, -outward), 0, tGround);
        // A shell one internal unit up, in the units calculateNearestPoint uses.
        spherical.calculateHitPoint(cglib::ray3<double>(camera, -outward), 1000.0, tShell);
        TEST_CHECK(tShell < tGround, "a ray coming down meets a raised shell before the ground");
        TEST_CHECK(nearly(cglib::length(spherical.calculateNearestPoint(camera, 1000.0)) - cglib::length(surface), 1000.0, 1.0e-6),
                   "and the nearest point on that shell sits exactly that far above the surface");
    }

    /*
     * Poles and the antimeridian, which the globe alone has to survive: VectorTileLayer adds two
     * pole tiles in spherical mode, and their geometry runs through this surface.
     */
    void testPolesAndTheAntimeridianStayFinite() {
        SphericalProjectionSurface spherical;
        cglib::vec3<double> northPole = spherical.calculatePosition(internalOf(0, 89.9999));
        TEST_CHECK(std::isfinite(cglib::norm(northPole)) && northPole(2) > 0,
                   "a position just short of the north pole is finite and above the equator plane");

        // Either side of the antimeridian is the same place on the globe, unlike on the plane
        // where they are a world apart.
        cglib::vec3<double> west = spherical.calculatePosition(internalOf(-179.999, 12.0));
        cglib::vec3<double> east = spherical.calculatePosition(internalOf(179.999, 12.0));
        TEST_CHECK(spherical.calculateDistance(west, east) < spherical.calculateDistance(west, spherical.calculatePosition(internalOf(0, 12.0))),
                   "the two sides of the antimeridian are neighbours on the globe");
    }

    void testGreatCircleDistanceIsTheArc() {
        SphericalProjectionSurface spherical;
        cglib::vec3<double> equator = spherical.calculatePosition(internalOf(0, 0));
        cglib::vec3<double> quarter = spherical.calculatePosition(internalOf(90, 0));
        // A quarter turn of a sphere of radius WORLD_SIZE / pi.
        TEST_CHECK(nearly(spherical.calculateDistance(equator, quarter), 0.5 * Const::WORLD_SIZE, 1.0e-9),
                   "a quarter turn around the equator is the arc, not the chord");
        TEST_CHECK(nearly(spherical.calculateDistance(equator, equator), 0.0, 1.0e-9),
                   "a point is no distance from itself");
    }

    /*
     * The globe subdivides a long segment and the plane does not - the difference that makes a
     * straight line follow the curve. The terrain decorator has to keep this, not replace it.
     */
    void testLongSegmentsAreSubdividedOnTheGlobeOnly() {
        PlanarProjectionSurface planar;
        SphericalProjectionSurface spherical;
        MapPos a = internalOf(-30, 10);
        MapPos b = internalOf(30, 10);

        std::vector<MapPos> planarPoints, sphericalPoints;
        planar.tesselateSegment(a, b, planarPoints);
        spherical.tesselateSegment(a, b, sphericalPoints);
        TEST_CHECK(planarPoints.size() == 2, "the plane leaves a long segment as its two endpoints");
        TEST_CHECK(sphericalPoints.size() > 2, "the globe splits it, so it follows the curve");

        std::vector<MapPos> shortPoints;
        spherical.tesselateSegment(a, internalOf(-29.999, 10), shortPoints);
        TEST_CHECK(shortPoints.size() == 2, "and leaves a short one alone");
    }
}

void testSphericalSurface() {
    testInternalCoordinatesAreStillMercator();
    testHeightMeansTheSameMetresOnBothSurfaces();
    testTheSphericalWorldIsTwiceThePlanarScale();
    testTheNormalIsOnlyUnitLengthOnTheSurface();
    testTheLocalFrameIsOrthogonalAndEastNorthUp();
    testAHitBehindTheCameraIsReportedAsAHit();
    testTheHitHeightSelectsTheShell();
    testPolesAndTheAntimeridianStayFinite();
    testGreatCircleDistanceIsTheArc();
    testLongSegmentsAreSubdividedOnTheGlobeOnly();
}
