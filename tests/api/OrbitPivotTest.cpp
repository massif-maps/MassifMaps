/*
 * Tests for the orbit step (all/native/renderers/cameraevents/OrbitPivot.h): the tilt about a picked terrain point
 * that Options::setOrbitAroundPivot turns on.
 *
 * A point's screen position is checked as its direction in the camera's own frame (right, up, forward): the same
 * frame coordinates are the same pixel, whatever the projection. The view looks north at a slope, the pivot on a
 * flank 400 m below the focus and off to the side.
 *
 * NOT covered here: CameraTiltEvent itself (needs ViewState and Options), the terrain ray cast that picks the pivot
 * (TouchHandler::calculateTerrainHeight, needs the ElevationManager), and the web right-drag and two-finger tilt that
 * feed it. Those are the on-screen check named in the PR.
 */

#include "renderers/cameraevents/OrbitPivot.h"

#include <cmath>

using namespace massif;

#include "TestCheck.h"

namespace {

    cglib::vec3<double> inCameraFrame(const cglib::vec3<double>& camera, const cglib::vec3<double>& focus, const cglib::vec3<double>& up, const cglib::vec3<double>& point) {
        cglib::vec3<double> forward = cglib::unit(focus - camera);
        cglib::vec3<double> right = cglib::unit(cglib::vector_product(forward, up));
        cglib::vec3<double> trueUp = cglib::vector_product(right, forward);
        cglib::vec3<double> d = cglib::unit(point - camera);
        return cglib::vec3<double>(cglib::dot_product(d, right), cglib::dot_product(d, trueUp), cglib::dot_product(d, forward));
    }

    double tiltDegrees(const cglib::vec3<double>& camera, const cglib::vec3<double>& focus) {
        cglib::vec3<double> d = cglib::unit(camera - focus);
        return std::asin(d(2)) * 180.0 / 3.141592653589793;
    }

}

void testOrbitPivot() {
    const cglib::vec3<double> focus(0, 0, 1000);
    const cglib::vec3<double> camera(0, -4000, 4000);
    const cglib::vec3<double> up = cglib::unit(cglib::vec3<double>(0, 3000, 4000));
    const cglib::vec3<double> pivot(900, 1200, 600);
    const cglib::vec3<double> normal(0, 0, 1);
    const cglib::vec3<double> axis = cglib::vector_product(normal, up); // CameraTiltEvent's axis

    for (double degrees : { -20.0, -5.0, 5.0, 20.0 }) {
        double angle = degrees * 3.141592653589793 / 180.0;
        cglib::vec3<double> newCamera = camera, newFocus = focus, newUp = up;
        orbitAboutPivot(pivot, axis, angle, newCamera, newFocus, newUp);
        cglib::vec3<double> before = inCameraFrame(camera, focus, up, pivot);
        cglib::vec3<double> after = inCameraFrame(newCamera, newFocus, newUp, pivot);
        TEST_CHECK(cglib::length(after - before) < 1.0e-9, "the pivot keeps its screen position through the orbit");
        TEST_CHECK(std::abs(cglib::length(newCamera - newFocus) - cglib::length(camera - focus)) < 1.0e-6,
                   "the camera-to-focus distance is kept, so the zoom does not move");
        TEST_CHECK(std::abs((tiltDegrees(newCamera, newFocus) - tiltDegrees(camera, focus)) - degrees) < 1.0e-9,
                   "the view tilts by exactly the angle asked for");
        cglib::vec3<double> heading = newFocus - newCamera, oldHeading = focus - camera;
        TEST_CHECK(std::abs(heading(0) * oldHeading(1) - heading(1) * oldHeading(0)) < 1.0e-6, "the heading does not turn: a tilt is not a rotation");
    }

    // What the tilt did before: the same angle about the focus moves a pivot that is not the focus.
    {
        double angle = 20.0 * 3.141592653589793 / 180.0;
        cglib::vec3<double> newCamera = camera, newFocus = focus, newUp = up;
        orbitAboutPivot(focus, axis, angle, newCamera, newFocus, newUp);
        TEST_CHECK(newFocus == focus, "with the focus as pivot the focus stays exactly where it was");
        cglib::vec3<double> before = inCameraFrame(camera, focus, up, pivot);
        cglib::vec3<double> after = inCameraFrame(newCamera, newFocus, newUp, pivot);
        TEST_CHECK(cglib::length(after - before) > 1.0e-2, "the focus tilt it replaces slides a flank point across the screen");
    }
}
