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

    void testMinHeightIsAFractionOfTheCameraAltitude() {
        // mapbox: mercatorZ(seaLevelZoom + 4) = altitude / 16, but on the camera's REAL altitude,
        // not mapbox's centerAltitude + orbit - see the header.
        TEST_CHECK(nearly(CameraClearance::minHeight(cameraZ(600, 480, 90), 0, 0), (600 + 480) / 16.0),
                   "straight down it is a sixteenth of the ground height plus the orbit");
        TEST_CHECK(nearly(CameraClearance::minHeight(cameraZ(0, 480, 90), 0, 0), 30),
                   "at sea level a sixteenth of the orbit alone");
        // Zooming in shrinks it - the point of the model: a fixed 60 m floor caps a phone at
        // zoom ~21 over flat ground, this one never blocks a zoom in on its own.
        TEST_CHECK(CameraClearance::minHeight(cameraZ(0, 60, 90), 0, 0) < CameraClearance::minHeight(cameraZ(0, 480, 90), 0, 0),
                   "and it falls with the orbit");
    }

    void testMinHeightAtLowTiltFollowsTheCamera() {
        // The regression this pins. At tilt 20 the camera is orbit * sin(20) above the focus, so
        // mapbox's focusZ + orbit shell is 1 / sin(20) = 2.9x taller than the camera's own
        // altitude, and lifts a camera that is plainly clear of the ground.
        double focusZ = 1500, orbit = 5760;                  // Chamonix at zoom 14, projected units
        double camera = cameraZ(focusZ, orbit, 20);
        TEST_CHECK(nearly(CameraClearance::minHeight(camera, 0, 0), camera / 16.0),
                   "the shell follows the camera's real altitude");
        TEST_CHECK(CameraClearance::minHeight(camera, 0, 0) < (focusZ + orbit) / 16.0 * 0.5,
                   "at tilt 20 that is under half the orbit shell");
        // A camera 370 over a ridge: clear of its own shell, under the orbit one.
        double terrainZ = 3100;
        TEST_CHECK(camera - terrainZ > CameraClearance::minHeight(camera, 0, 0),
                   "a camera over a ridge at low tilt stays put");
        TEST_CHECK(camera - terrainZ < (focusZ + orbit) / 16.0,
                   "where the orbit shell would have lifted it");
    }

    void testMinHeightFloors() {
        // mapbox clamps the zoom at maxZoom before adding the overzoom: the clearance never
        // shrinks below a sixteenth of the max-zoom orbit.
        TEST_CHECK(nearly(CameraClearance::minHeight(10, 160, 0), 10),
                   "below the max-zoom orbit the clearance is the max-zoom one");
        TEST_CHECK(nearly(CameraClearance::minHeight(480, 160, 0), 30),
                   "above it the altitude rules");
        // The app's explicit floor sits under both.
        TEST_CHECK(nearly(CameraClearance::minHeight(480, 0, 60), 60),
                   "an app floor above the fraction wins");
        TEST_CHECK(nearly(CameraClearance::minHeight(1080, 0, 60), 67.5),
                   "and one below it does not");
        TEST_CHECK(nearly(CameraClearance::minHeight(-700, 0, 0), 0),
                   "below sea level the altitude is clamped at zero, as in mapbox");
    }

    void testTargetHeightLandsOnTheShellItRaises() {
        // The lift is a fixed point: rising raises the shell it has to clear. Flat ground at 600,
        // so the camera must reach 600 / (1 - 1/16) = 640 and clear the ground by its own 40.
        double h = CameraClearance::targetHeight(600, 600, 0, 0);
        TEST_CHECK(nearly(h, 40), "over flat ground the lift is a fifteenth of the ground height");
        TEST_CHECK(nearly(600 + h - 600, CameraClearance::minHeight(600 + h, 0, 0)),
                   "and it lands exactly on the shell it raised");
        // Under a rise, the focus 400 below the ground under the camera.
        h = CameraClearance::targetHeight(600, 1000, 0, 0);
        TEST_CHECK(nearly(1000.0 / (1 - 1 / 16.0) - 600, h), "a rise pushes it that much higher");
        TEST_CHECK(nearly(600 + h - 1000, CameraClearance::minHeight(600 + h, 0, 0)),
                   "still exactly on the shell");
        // An app floor above the fraction takes over, and the target is terrainZ + floor.
        h = CameraClearance::targetHeight(600, 1000, 0, 200);
        TEST_CHECK(nearly(h, 600), "an app floor above the fraction sets the lift");
        TEST_CHECK(nearly(600 + h - 1000, 200), "landing a floor's worth over the ground");
    }

    void testMaxZoomLandsOnTheShell() {
        // A camera straight down over flat ground at sea level: the clearance is orbit / 16, and
        // the height IS the orbit, so no zoom in ever breaks it.
        TEST_CHECK(infinite(CameraClearance::maxZoom(18, 0, cameraZ(0, 480, 90), 0, 0, 0)),
                   "flat ground straight down is never bounded");
        // At tilt 60 over flat ground, the height is 0.866 orbit against its own sixteenth:
        // still never bounded.
        TEST_CHECK(infinite(CameraClearance::maxZoom(18, 0, cameraZ(0, 480, 60), 0, 0, 0)),
                   "nor is a tilted one");
        // Looking down a slope: the ground under the camera is 400 m above the focus, the camera
        // 416 m above the focus. Its clearance is 16 m against 1015.7 / 16 = 63.5 needed:
        // already under the shell, so the bound is BELOW the current zoom.
        float bound = CameraClearance::maxZoom(18, 600, cameraZ(600, 480, 60), 1000, 0, 0);
        TEST_CHECK(bound < 18, "a camera under the shell is bounded below its zoom");
        // Landing on the shell: scale s solves 600 + s * h - 1000 = (600 + s * h) / 16, with
        // h the camera's height above the focus.
        double s = (1000 - 600 + 600 / 16.0) / (480 * std::sin(M_PI / 3) * (1 - 1 / 16.0));
        TEST_CHECK(nearly(bound, 18 - std::log2(s)), "and lands exactly on it");
        // A camera above the shell on the same slope, further out: bounded above its zoom.
        bound = CameraClearance::maxZoom(14, 600, cameraZ(600, 7680, 60), 1000, 0, 0);
        TEST_CHECK(bound > 14, "a camera clear of the shell may zoom in");
        double sIn = (1000 - 600 + 600 / 16.0) / (7680 * std::sin(M_PI / 3) * (1 - 1 / 16.0));
        TEST_CHECK(nearly(bound, 14 - std::log2(sIn)), "to exactly the shell");
    }

    void testMaxZoomAppFloor() {
        // The app floor is the constant constraint: over flat ground straight down with a 60 m
        // floor, the orbit itself must stay above 60 - zoom in until the orbit is 60.
        float bound = CameraClearance::maxZoom(18, 0, cameraZ(0, 480, 90), 0, 0, 60);
        TEST_CHECK(nearly(bound, 18 + 3), "a 60 m floor over a 480 m orbit allows exactly three more levels");
        // The max-zoom orbit floors the fraction the same way.
        bound = CameraClearance::maxZoom(18, 0, cameraZ(0, 480, 90), 0, 960, 0);
        TEST_CHECK(nearly(bound, 18 + 3), "and so does the max-zoom orbit");
    }

    void testMaxZoomGivesUpWhereNoZoomHelps() {
        // A horizontal view (tilt 0) zooms along the ground: the height never changes, and the
        // bound would run off to minus infinity and throw the map to its minimum zoom.
        TEST_CHECK(infinite(CameraClearance::maxZoom(12, 600, cameraZ(600, 480, 0), 1000, 0, 0)),
                   "a horizontal view is not bounded");
        // A look above the horizon (negative tilt) has the camera below the focus height.
        TEST_CHECK(infinite(CameraClearance::maxZoom(12, 600, 500, 1000, 0, 0)),
                   "nor is a camera below the focus");
        // With the shell on the camera's own altitude, no tilt is too grazing to be worth a zoom
        // out - a rise under a 3 degree view bounds it, where the orbit shell gave up at
        // sin(tilt) < 1/16 and let the camera through the hill.
        TEST_CHECK(!infinite(CameraClearance::maxZoom(12, 0, cameraZ(0, 480, 3), 100, 0, 0)),
                   "a grazing view under a rise is bounded");
        TEST_CHECK(infinite(CameraClearance::maxZoom(12, 0, cameraZ(0, 480, 3), 0, 0, 0)),
                   "and over flat ground at sea level nothing binds at any tilt");
    }

    void testTheShellHeightLandsExactlyOnTheShell() {
        // What the focus lifts the camera to. The shell moves with the camera, so this is a fixed
        // point: the height it answers must itself satisfy minHeight at that height.
        bool onShell = true;
        for (double terrainZ : { 0.0, 100.0, 3842.0 }) {
            for (double floorZ : { 0.0, 50.0 }) {
                double cameraZ = CameraClearance::shellCameraZ(terrainZ, 0, floorZ);
                double required = CameraClearance::minHeight(cameraZ, 0, floorZ);
                onShell = onShell && std::fabs((cameraZ - terrainZ) - required) <= 1.0e-9 * std::max(1.0, required);
            }
        }
        TEST_CHECK(onShell, "the shell height clears the shell it implies, at every ground height");
        TEST_CHECK(CameraClearance::shellCameraZ(500, 0, 0) > 500, "and it is above the ground it stands on");
        // The old expression, kept as the camera height above a focus - the two must not drift.
        TEST_CHECK(CameraClearance::targetHeight(120, 500, 0, 0) == CameraClearance::shellCameraZ(500, 0, 0) - 120,
                   "targetHeight is the same height, measured from the focus");
    }

    void testTheFocusFollowsTheGroundOnlyNearTheShell() {
        // The focus is pinned to the ground so the zoom means "distance to the terrain". Pinned at
        // EVERY altitude - mapbox's rule - a pan across a ridge lifts the camera with it, which
        // reads as the whole view bobbing from far above the ground.
        double shell = 200;
        TEST_CHECK(CameraClearance::focusFollow(0, shell) == 1.0, "under the shell the focus is on the ground");
        TEST_CHECK(CameraClearance::focusFollow(shell, shell) == 1.0, "... and exactly at it too");
        TEST_CHECK(CameraClearance::focusFollow(shell * CameraClearance::FOLLOW_BAND, shell) == 0.0,
                   "a band above it the focus is back at sea level");
        TEST_CHECK(CameraClearance::focusFollow(shell * 100, shell) == 0.0, "and stays there however high");

        // Monotone in between, so a slow climb hands the ground over smoothly instead of stepping.
        double previous = 1.0;
        bool monotone = true;
        for (double h = shell; h <= shell * CameraClearance::FOLLOW_BAND; h += shell * 0.1) {
            double follow = CameraClearance::focusFollow(h, shell);
            monotone = monotone && follow <= previous + 1.0e-12 && follow >= 0.0 && follow <= 1.0;
            previous = follow;
        }
        TEST_CHECK(monotone, "and falls monotonically between the two, inside [0, 1]");

        // No shell (no terrain options, no floor, a camera at sea level) leaves the old behaviour.
        TEST_CHECK(CameraClearance::focusFollow(1000, 0) == 1.0, "with no shell at all the focus still follows");
    }

}

void testCameraClearance() {
    testMinHeightIsAFractionOfTheCameraAltitude();
    testMinHeightAtLowTiltFollowsTheCamera();
    testMinHeightFloors();
    testTargetHeightLandsOnTheShellItRaises();
    testMaxZoomLandsOnTheShell();
    testMaxZoomAppFloor();
    testMaxZoomGivesUpWhereNoZoomHelps();
    testTheFocusFollowsTheGroundOnlyNearTheShell();
    testTheShellHeightLandsExactlyOnTheShell();
}
