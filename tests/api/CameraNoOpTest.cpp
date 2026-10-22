// What counts as a camera event that leaves the view where it is
// (renderers/components/CameraNoOp.h): the float noise of a repeated moveTo to the spot the
// camera is at must read as "no change", a real move of a pixel must not, and the request of an
// animation tolerates a GPS fix a fraction of a pixel away.
//
// NOT covered here: that MapRenderer::calculateCameraEvent and BaseMapView::flyTo ask it; that
// is a device check (the cullWorkerUpdates counter stays flat under a repeated moveTo).

#include "renderers/components/CameraNoOp.h"
#include "renderers/components/FlightPath.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif;

void testCameraNoOp() {
    // Zoom 19 over a 1080 px wide viewport: world units are 2^20 wide, 256 px tiles.
    const double world = double(1 << 20);
    const double pixel = world / (256.0 * std::pow(2.0, 19));
    const double span = 1080 * pixel;
    const double noise = CameraNoOp::noiseTolerance(span);

    double x = -123456.789012345;
    double roundTrip = x * (1.0 + 4.0e-16) - x;
    TEST_CHECK(CameraNoOp::samePos(roundTrip, 0, 0, noise, true), "the rounding of a position round trip is not a move");
    TEST_CHECK(CameraNoOp::samePos(1.0e-3 * pixel, 0, 0, noise, true), "a thousandth of a pixel is not a move");
    TEST_CHECK(!CameraNoOp::samePos(0.01 * pixel, 0, 0, noise, true), "a hundredth of a pixel is");
    TEST_CHECK(!CameraNoOp::samePos(0, pixel, 0, noise, true), "and so is a pixel, along y too");
    TEST_CHECK(CameraNoOp::samePos(0, 0, 0, CameraNoOp::noiseTolerance(0), true), "with no view size an identical position is still the same");
    TEST_CHECK(!CameraNoOp::samePos(roundTrip, 0, 0, CameraNoOp::noiseTolerance(0), true), "but nothing else is");

    const double deadBand = CameraNoOp::deadBandTolerance(world, 19);
    TEST_CHECK(std::abs(deadBand - 0.25 * pixel) < 1.0e-12, "the dead band is a quarter of a tile pixel");
    TEST_CHECK(CameraNoOp::samePos(0.2 * pixel, -0.2 * pixel, 0, deadBand, false), "a fix a fifth of a pixel away is inside it");
    TEST_CHECK(!CameraNoOp::samePos(0.3 * pixel, 0, 0, deadBand, false), "three tenths is a move");
    TEST_CHECK(CameraNoOp::deadBandTolerance(world, 10) > 500 * CameraNoOp::deadBandTolerance(world, 19), "and it follows the zoom");

    // A target that carries a GPS altitude over a focus the ground rule keeps at 0.
    TEST_CHECK(CameraNoOp::samePos(0, 0, 300 * pixel, noise, false), "a height the ground rule rewrites is not a move");
    TEST_CHECK(!CameraNoOp::samePos(0, 0, 300 * pixel, noise, true), "unless nothing rewrites it (free roam)");

    TEST_CHECK(CameraNoOp::sameAngle(0.0f, false), "no rotation is no change");
    TEST_CHECK(CameraNoOp::sameAngle(360.0f, false), "a full turn is no change");
    TEST_CHECK(CameraNoOp::sameAngle(-360.0f + 1.0e-5f, false), "and the other way round");
    TEST_CHECK(CameraNoOp::sameAngle(1.0e-5f, false), "float noise on an angle is no change");
    TEST_CHECK(!CameraNoOp::sameAngle(0.01f, false), "a hundredth of a degree is");
    TEST_CHECK(!CameraNoOp::sameAngle(359.99f - 360.0f, false), "also when it is negative");
    TEST_CHECK(CameraNoOp::sameAngle(0.05f, true) && CameraNoOp::sameAngle(359.95f, true), "but not within the dead band of a request");
    TEST_CHECK(!CameraNoOp::sameAngle(0.2f, true), "a fifth of a degree is a turn");

    TEST_CHECK(CameraNoOp::sameZoom(0.0f, false), "no zoom change is no change");
    TEST_CHECK(CameraNoOp::sameZoom(2.0e-6f, false), "float noise on a zoom is no change");
    TEST_CHECK(!CameraNoOp::sameZoom(1.0e-3f + 1.0e-4f, false), "a thousandth of a level is");
    TEST_CHECK(CameraNoOp::sameZoom(5.0e-4f, true), "inside the dead band of a request it is not");

    // flyTo to the view the camera has: a flight whose two ends are the same width over no distance.
    // Every frame of it must ask for the zoom it started at, or it is a camera event on every frame.
    FlightPath flight;
    double w = 1080 * pixel;
    flight.setup(w, w, 0, 1.42);
    bool everyFrameAtStart = true;
    for (int i = 0; i <= 20; i++) {
        double ratio = 0, zoomDelta = 0;
        flight.sample(i / 20.0, ratio, zoomDelta);
        everyFrameAtStart = everyFrameAtStart && CameraNoOp::sameZoom(static_cast<float>(zoomDelta), false);
    }
    TEST_CHECK(everyFrameAtStart, "a flight to the view it starts from never leaves its zoom");
    flight.setup(w, w * (1.0 + 1.0e-9), 1.0e-7 * w, 1.42);
    double ratio = 0, zoomDelta = 0;
    flight.sample(0.5, ratio, zoomDelta);
    TEST_CHECK(CameraNoOp::sameZoom(static_cast<float>(zoomDelta), false), "nor one that is a float round trip away from it");
}
