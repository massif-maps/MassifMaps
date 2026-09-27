/*
 * Tests for the sky's observer frame (all/native/renderers/utils/SkyFrame.h): the rotation that
 * puts a world-space view ray into the local east/north/up the sky shader's whole model assumes,
 * and the camera height that model marches the atmosphere from.
 *
 * The load-bearing check is that the planar frame is exactly the identity and the planar camera
 * height is exactly the number the shader had before, because that is what makes this change
 * invisible on the plane.
 *
 * NOT covered here: the shader. That rotating the ray really fixes the horizon, the star fade and
 * the ground wedge on the globe is a device check - and so is the atmosphere, which marches from
 * u_cameraHeight. See docs/internals/rendering/18-globe.md.
 */

#include "projections/EPSG3857.h"
#include "projections/PlanarProjectionSurface.h"
#include "projections/SphericalProjectionSurface.h"
#include "renderers/utils/SkyFrame.h"
#include "utils/Const.h"

#include <cmath>

using namespace massif;

#include "TestCheck.h"

namespace {

    bool nearly(double value, double expected, double tolerance = 1.0e-6) {
        return std::fabs(value - expected) <= tolerance * std::max(1.0, std::fabs(expected));
    }

    MapPos internalOf(double lon, double lat, double heightMeters = 0) {
        EPSG3857 proj;
        MapPos internal = proj.toInternal(proj.fromWgs84(MapPos(lon, lat, 0)));
        return MapPos(internal.getX(), internal.getY(), heightMeters * Const::WORLD_SIZE / (Const::EARTH_CIRCUMFERENCE * std::cos(lat * Const::DEG_TO_RAD)));
    }

    /*
     * The regression this whole file exists for: on the plane the frame is the identity, so the
     * shader multiplies its ray by 1 and every angle it derives is the number it was before.
     */
    void testThePlanarFrameIsExactlyTheIdentity() {
        PlanarProjectionSurface planar;
        bool identity = true;
        for (double lat : { 0.0, 45.9, -33.86, 71.2 }) {
            cglib::mat3x3<float> frame = SkyFrame::orientation(planar, planar.calculatePosition(internalOf(7.05, lat)));
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    identity = identity && frame(r, c) == (r == c ? 1.0f : 0.0f);
                }
            }
        }
        TEST_CHECK(identity, "the planar frame is the identity at every latitude, exactly");
    }

    /*
     * On the globe the point of the rotation: the up at the focus becomes +Z, which is the axis
     * the shader reads as the elevation angle.
     */
    void testTheGlobeFrameTakesTheLocalUpToZ() {
        SphericalProjectionSurface spherical;
        bool allUp = true;
        for (double lat : { 0.0, 45.9, -33.86, 71.2 }) {
            cglib::vec3<double> pos = spherical.calculatePosition(internalOf(7.05, lat));
            cglib::mat3x3<float> frame = SkyFrame::orientation(spherical, pos);
            cglib::vec3<float> up = cglib::vec3<float>::convert(cglib::unit(spherical.calculateNormal(spherical.calculateMapPos(pos))));
            cglib::vec3<float> rotated = cglib::transform(up, frame);
            allUp = allUp && nearly(rotated(0), 0.0, 1.0e-5) && nearly(rotated(1), 0.0, 1.0e-5) && nearly(rotated(2), 1.0, 1.0e-5);
        }
        TEST_CHECK(allUp, "the surface normal at the focus rotates onto +Z");

        // Away from the focus it does not, which is the difference from the flat-earth up the
        // shader used: a ray towards the horizon really is at the horizon.
        cglib::vec3<double> focus = spherical.calculatePosition(internalOf(0, 0));
        cglib::mat3x3<float> frame = SkyFrame::orientation(spherical, focus);
        cglib::vec3<float> farNormal = cglib::vec3<float>::convert(cglib::unit(spherical.calculatePosition(internalOf(60, 0))));
        TEST_CHECK(cglib::transform(farNormal, frame)(2) < 0.55f,
                   "a normal 60 degrees away tilts far off the local up, as it should");
    }

    void testTheGlobeFrameIsOrthonormalAndEastNorthUp() {
        SphericalProjectionSurface spherical;
        cglib::vec3<double> pos = spherical.calculatePosition(internalOf(7.05, 45.9));
        cglib::mat3x3<float> frame = SkyFrame::orientation(spherical, pos);

        // Rows are the axes; orthonormal rows mean the transpose really is the inverse.
        cglib::vec3<float> east(frame(0, 0), frame(0, 1), frame(0, 2));
        cglib::vec3<float> north(frame(1, 0), frame(1, 1), frame(1, 2));
        cglib::vec3<float> up(frame(2, 0), frame(2, 1), frame(2, 2));
        TEST_CHECK(nearly(cglib::length(east), 1.0, 1.0e-5) && nearly(cglib::length(north), 1.0, 1.0e-5) && nearly(cglib::length(up), 1.0, 1.0e-5),
                   "the frame's rows are unit vectors, so it is a rotation and not a scale");
        TEST_CHECK(nearly(cglib::dot_product(east, north), 0.0, 1.0e-5) &&
                   nearly(cglib::dot_product(east, up), 0.0, 1.0e-5) &&
                   nearly(cglib::dot_product(north, up), 0.0, 1.0e-5),
                   "and they are mutually orthogonal");

        // A point due east of the focus lands on +x, one due north on +y - the axis order the
        // star azimuth (atan(rayDir.y, rayDir.x)) and the sun direction both read.
        cglib::vec3<float> toEast = cglib::vec3<float>::convert(cglib::unit(spherical.calculatePosition(internalOf(7.15, 45.9)) - pos));
        cglib::vec3<float> toNorth = cglib::vec3<float>::convert(cglib::unit(spherical.calculatePosition(internalOf(7.05, 46.0)) - pos));
        TEST_CHECK(cglib::transform(toEast, frame)(0) > 0.99f, "east of the focus is +x in the frame");
        TEST_CHECK(cglib::transform(toNorth, frame)(1) > 0.99f, "north of the focus is +y");
    }

    /*
     * The other half of the planar regression: the exact expression the shader was fed before.
     */
    void testThePlanarCameraHeightIsUnchanged() {
        PlanarProjectionSurface planar;
        bool allMatch = true;
        for (double heightMeters : { 0.0, 120.0, 9000.0, 400000.0 }) {
            cglib::vec3<double> camera = planar.calculatePosition(internalOf(7.05, 45.9, heightMeters));
            float before = static_cast<float>(camera(2) * Const::EARTH_CIRCUMFERENCE / Const::WORLD_SIZE);
            allMatch = allMatch && SkyFrame::cameraHeight(planar, camera) == before;
        }
        TEST_CHECK(allMatch, "the planar camera height is bit for bit the expression it replaced");
    }

    void testTheTwoSurfacesReportTheSameCameraHeight() {
        PlanarProjectionSurface planar;
        SphericalProjectionSurface spherical;
        bool allMatch = true;
        for (double lat : { 0.0, 45.9, -33.86 }) {
            for (double heightMeters : { 120.0, 9000.0, 400000.0 }) {
                MapPos camera = internalOf(7.05, lat, heightMeters);
                allMatch = allMatch && nearly(SkyFrame::cameraHeight(spherical, spherical.calculatePosition(camera)),
                                              SkyFrame::cameraHeight(planar, planar.calculatePosition(camera)), 1.0e-4);
            }
        }
        TEST_CHECK(allMatch, "the same altitude reads the same on the globe as on the plane");

        // It rises with altitude and is zero on the ground - the atmosphere march needs both.
        cglib::vec3<double> ground = spherical.calculatePosition(internalOf(7.05, 45.9, 0));
        cglib::vec3<double> high = spherical.calculatePosition(internalOf(7.05, 45.9, 9000));
        TEST_CHECK(nearly(SkyFrame::cameraHeight(spherical, ground), 0.0, 1.0e-3), "a camera on the globe's surface is at height zero");
        TEST_CHECK(SkyFrame::cameraHeight(spherical, high) > SkyFrame::cameraHeight(spherical, ground) + 1000.0f,
                   "and a higher one reads higher");
    }

    /*
     * Over a pole the globe's internal z diverges - atanh(1) - and the local frame's east axis
     * collapses to zero length. Both would reach the shader as a NaN otherwise: one as an
     * atmosphere origin, the other as a rotation that annihilates the ray.
     */
    void testAPoleCameraDoesNotProduceGarbage() {
        SphericalProjectionSurface spherical;
        cglib::vec3<double> pole(0, 0, Const::WORLD_SIZE / Const::PI * 1.05);

        TEST_CHECK(SkyFrame::cameraHeight(spherical, pole) == 0.0f,
                   "a camera over the pole reports height zero rather than infinity");

        cglib::mat3x3<float> frame = SkyFrame::orientation(spherical, pole);
        bool finite = true;
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                finite = finite && std::isfinite(frame(r, c));
            }
        }
        TEST_CHECK(finite, "and the frame there is finite rather than a NaN rotation");
        TEST_CHECK(frame(0, 0) == 1.0f && frame(1, 1) == 1.0f && frame(2, 2) == 1.0f,
                   "falling back to the identity, which leaves the ray untouched");
    }
}

void testSkyFrame() {
    testThePlanarFrameIsExactlyTheIdentity();
    testTheGlobeFrameTakesTheLocalUpToZ();
    testTheGlobeFrameIsOrthonormalAndEastNorthUp();
    testThePlanarCameraHeightIsUnchanged();
    testTheTwoSurfacesReportTheSameCameraHeight();
    testAPoleCameraDoesNotProduceGarbage();
}
