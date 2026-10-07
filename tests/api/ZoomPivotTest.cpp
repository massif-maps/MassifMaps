/*
 * Tests for the zoom pivot shift (all/native/renderers/cameraevents/ZoomPivot.h): the focus shift that keeps a
 * gesture's ground point under the pointer while CameraZoomEvent scales the camera distance.
 *
 * The screen position of a point is checked as the direction from the camera to it: the zoom changes neither
 * rotation nor tilt, so one direction is one pixel. The view below looks across a valley the way the web report
 * did, with the pointer on ground 500 m below the focus.
 *
 * NOT covered here: CameraZoomEvent itself (needs ViewState and Options), the terrain ray cast that finds the
 * pointer's ground height (TouchHandler::calculateTerrainHeight, needs the ElevationManager), and the wheel's
 * route to it on the web (WebMapView). Those are the on-screen check named in the PR.
 */

#include "renderers/cameraevents/ZoomPivot.h"

#include <cmath>

using namespace massif;

#include "TestCheck.h"

namespace {

    cglib::vec3<double> direction(const cglib::vec3<double>& from, const cglib::vec3<double>& to) {
        cglib::vec3<double> d = to - from;
        return d * (1.0 / cglib::length(d));
    }

    // atan2 of cross and dot: acos cannot resolve an angle this small next to 1.
    double angleBetween(const cglib::vec3<double>& a, const cglib::vec3<double>& b) {
        return std::atan2(cglib::length(cglib::vector_product(a, b)), cglib::dot_product(a, b));
    }

}

void testZoomPivot() {
    const cglib::vec3<double> focus(0, 0, 1000);
    const cglib::vec3<double> camera(0, -4000, 4000); // tilted, looking north and down
    const cglib::vec3<double> pivot(800, 1500, 500);  // the pointer, on the valley floor below the focus

    for (double scale : { 0.5, 0.8, 1.25, 2.0 }) {
        cglib::vec3<double> shift;
        bool solved = pinnedZoomShift(focus, camera, pivot, scale, shift);
        cglib::vec3<double> newFocus = focus + shift;
        cglib::vec3<double> newCamera = newFocus + (camera - focus) * scale;
        TEST_CHECK(solved, "a pivot below the camera has a pinned shift");
        TEST_CHECK(shift(2) == 0.0, "the shift is horizontal: the focus keeps its height");
        TEST_CHECK(std::abs(cglib::length(newCamera - newFocus) - cglib::length(camera - focus) * scale) < 1.0e-6,
                   "the camera distance scales exactly, so the zoom stays calibrated");
        TEST_CHECK(angleBetween(direction(camera, pivot), direction(newCamera, pivot)) < 1.0e-9,
                   "the pivot stays on its screen ray: the ground under the pointer does not move");
    }

    // What CameraZoomEvent did before: the pivot taken at the focus height, shifted by (1 - scale).
    {
        double scale = 0.5;
        cglib::vec3<double> flatPivot(pivot(0), pivot(1), focus(2));
        cglib::vec3<double> newFocus = focus + (flatPivot - focus) * (1.0 - scale);
        cglib::vec3<double> newCamera = newFocus + (camera - focus) * scale;
        TEST_CHECK(angleBetween(direction(camera, pivot), direction(newCamera, pivot)) > 1.0e-3,
                   "the focus-height pivot it replaces let a valley point slide on screen");
    }

    {
        cglib::vec3<double> shift;
        cglib::vec3<double> levelPivot(800, 1500, focus(2));
        pinnedZoomShift(focus, camera, levelPivot, 0.5, shift);
        cglib::vec3<double> expected = (levelPivot - focus) * 0.5;
        TEST_CHECK(cglib::length(shift - expected) < 1.0e-6, "a pivot at the focus height gets the old (1 - scale) shift");
    }

    {
        cglib::vec3<double> shift;
        cglib::vec3<double> aboveCamera(800, 1500, camera(2) + 100);
        TEST_CHECK(!pinnedZoomShift(focus, camera, aboveCamera, 2.0, shift), "a pivot above the camera has no pinned shift and is refused");
    }
}
