/*
 * Tests for the camera clearance rule (all/native/terrain/CameraClearance.h): mapbox's
 * zoom-relative minimum height over the ground under the camera, and the zoom that lands the
 * camera on that shell when zooming about the focus.
 *
 * NOT covered here: what reads the rule. ViewState::getTerrainMaxZoom feeds it the camera
 * state, CameraZoomEvent clamps the request with it, and MapRenderer lifts a camera under the
 * shell by tilting - all past what the host link carries. That a pinch on a hillside really
 * stops at the shell without a jump, and that a pan into the hillside lifts the camera instead
 * of zooming it out, are device checks. See docs/internals/rendering/04-terrain.md.
 */

#include "terrain/CameraClearance.h"

#include <cmath>
#include <limits>

using namespace massif;

#include "TestCheck.h"

namespace {

    bool nearly(double value, double expected) {
        return std::fabs(value - expected) < 1.0e-6 * std::max(1.0, std::fabs(expected));
    }

    bool infinite(float value) {
        return value == std::numeric_limits<float>::infinity();
    }

    // Camera height at the given tilt (90 = straight down) and orbit, above a focus at focusZ.
    double cameraZ(double focusZ, double orbit, double tiltDeg) {
        return focusZ + orbit * std::sin(tiltDeg * M_PI / 180.0);
    }

    void testMinHeightIsAFractionOfTheSeaLevelOrbit() {
        // mapbox: mercatorZ(seaLevelZoom + 4) = (centerAltitude + orbit) / 16.
        TEST_CHECK(nearly(CameraClearance::minHeight(600, 480, 0, 0), (600 + 480) / 16.0),
                   "the clearance is a sixteenth of the ground height plus the orbit");
        TEST_CHECK(nearly(CameraClearance::minHeight(0, 480, 0, 0), 30),
                   "at sea level it is a sixteenth of the orbit alone");
        // Zooming in shrinks it - the point of the model: a fixed 60 m floor caps a phone at
        // zoom ~21 over flat ground, this one never blocks a zoom in on its own.
        TEST_CHECK(CameraClearance::minHeight(0, 60, 0, 0) < CameraClearance::minHeight(0, 480, 0, 0),
                   "and it falls with the orbit");
    }

    void testMinHeightFloors() {
        // mapbox clamps the zoom at maxZoom before adding the overzoom: the clearance never
        // shrinks below a sixteenth of the max-zoom orbit.
        TEST_CHECK(nearly(CameraClearance::minHeight(0, 10, 160, 0), 10),
                   "below the max-zoom orbit the clearance is the max-zoom one");
        TEST_CHECK(nearly(CameraClearance::minHeight(0, 480, 160, 0), 30),
                   "above it the orbit rules");
        // The app's explicit floor sits under both.
        TEST_CHECK(nearly(CameraClearance::minHeight(0, 480, 0, 60), 60),
                   "an app floor above the fraction wins");
        TEST_CHECK(nearly(CameraClearance::minHeight(600, 480, 0, 60), 67.5),
                   "and one below it does not");
        TEST_CHECK(nearly(CameraClearance::minHeight(-800, 100, 0, 0), 0),
                   "below sea level the sea-level orbit is clamped at zero, as in mapbox");
    }

    void testMaxZoomLandsOnTheShell() {
        // A camera straight down over flat ground at sea level: the clearance is orbit / 16, and
        // the height IS the orbit, so no zoom in ever breaks it.
        TEST_CHECK(infinite(CameraClearance::maxZoom(18, 0, cameraZ(0, 480, 90), 480, 0, 0, 0)),
                   "flat ground straight down is never bounded");
        // At tilt 60 over flat ground, the height is 0.866 orbit against a 1/16 orbit clearance:
        // still never bounded.
        TEST_CHECK(infinite(CameraClearance::maxZoom(18, 0, cameraZ(0, 480, 60), 480, 0, 0, 0)),
                   "nor is a tilted one");
        // Looking down a slope: the ground under the camera is 400 m above the focus, the camera
        // 416 m above the focus. Its clearance is 16 m against (600 + 480) / 16 = 67.5 needed:
        // already under the shell, so the bound is BELOW the current zoom.
        float bound = CameraClearance::maxZoom(18, 600, cameraZ(600, 480, 60), 480, 1000, 0, 0);
        TEST_CHECK(bound < 18, "a camera under the shell is bounded below its zoom");
        // Landing on the shell: scale s solves 600 + s * h - 1000 = (600 + s * 480) / 16, with
        // h the camera's height above the focus.
        double s = (1000 - 600 + 600 / 16.0) / (480 * std::sin(M_PI / 3) - 480 / 16.0);
        TEST_CHECK(nearly(bound, 18 - std::log2(s)), "and lands exactly on it");
        // A camera above the shell on the same slope, further out: bounded above its zoom.
        bound = CameraClearance::maxZoom(14, 600, cameraZ(600, 7680, 60), 7680, 1000, 0, 0);
        TEST_CHECK(bound > 14, "a camera clear of the shell may zoom in");
        double sIn = (1000 - 600 + 600 / 16.0) / (7680 * std::sin(M_PI / 3) - 7680 / 16.0);
        TEST_CHECK(nearly(bound, 14 - std::log2(sIn)), "to exactly the shell");
    }

    void testMaxZoomAppFloor() {
        // The app floor is the constant constraint: over flat ground straight down with a 60 m
        // floor, the orbit itself must stay above 60 - zoom in until the orbit is 60.
        float bound = CameraClearance::maxZoom(18, 0, cameraZ(0, 480, 90), 480, 0, 0, 60);
        TEST_CHECK(nearly(bound, 18 + 3), "a 60 m floor over a 480 m orbit allows exactly three more levels");
        // The max-zoom orbit floors the fraction the same way.
        bound = CameraClearance::maxZoom(18, 0, cameraZ(0, 480, 90), 480, 0, 960, 0);
        TEST_CHECK(nearly(bound, 18 + 3), "and so does the max-zoom orbit");
    }

    void testMaxZoomGivesUpWhereNoZoomHelps() {
        // A horizontal view (tilt 0) zooms along the ground: the height never changes, and the
        // bound would run off to minus infinity and throw the map to its minimum zoom.
        TEST_CHECK(infinite(CameraClearance::maxZoom(12, 600, cameraZ(600, 480, 0), 480, 1000, 0, 0)),
                   "a horizontal view is not bounded");
        // A look above the horizon (negative tilt) has the camera below the focus height.
        TEST_CHECK(infinite(CameraClearance::maxZoom(12, 600, 500, 480, 1000, 0, 0)),
                   "nor is a camera below the focus");
        // A grazing tilt where the height rises slower than the clearance (sin tilt < 1/16).
        TEST_CHECK(infinite(CameraClearance::maxZoom(12, 0, cameraZ(0, 480, 3), 480, 0, 0, 0)),
                   "nor a grazing one over flat ground");
    }

}

void testCameraClearance() {
    testMinHeightIsAFractionOfTheSeaLevelOrbit();
    testMinHeightFloors();
    testMaxZoomLandsOnTheShell();
    testMaxZoomAppFloor();
    testMaxZoomGivesUpWhereNoZoomHelps();
}
