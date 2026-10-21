// What counts as a camera event that leaves the view where it is
// (renderers/components/CameraNoOp.h): the float noise of a repeated moveTo to the spot the
// camera is at must read as "no change", a real move of a pixel must not.

#include "renderers/components/CameraNoOp.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif;

void testCameraNoOp() {
    // Zoom 19 over a 1080 px wide viewport: world units are 2^20 wide, 256 px tiles.
    const double pixel = double(1 << 20) / (256.0 * std::pow(2.0, 19));
    const double span = 1080 * pixel;

    double x = -123456.789012345;
    double roundTrip = x * (1.0 + 4.0e-16);
    TEST_CHECK(CameraNoOp::sameCoord(x, roundTrip, span), "the rounding of a position round trip is not a move");
    TEST_CHECK(CameraNoOp::sameCoord(x, x + 1.0e-3 * pixel, span), "a thousandth of a pixel is not a move");
    TEST_CHECK(!CameraNoOp::sameCoord(x, x + 0.01 * pixel, span), "a hundredth of a pixel is");
    TEST_CHECK(!CameraNoOp::sameCoord(x, x + pixel, span), "and so is a pixel");
    TEST_CHECK(CameraNoOp::sameCoord(x, x, 0), "with no view size an identical position is still the same");
    TEST_CHECK(!CameraNoOp::sameCoord(x, roundTrip, 0), "but nothing else is");

    TEST_CHECK(CameraNoOp::sameAngle(0.0f), "no rotation is no change");
    TEST_CHECK(CameraNoOp::sameAngle(360.0f), "a full turn is no change");
    TEST_CHECK(CameraNoOp::sameAngle(-360.0f + 1.0e-5f), "and the other way round");
    TEST_CHECK(CameraNoOp::sameAngle(1.0e-5f), "float noise on an angle is no change");
    TEST_CHECK(!CameraNoOp::sameAngle(0.01f), "a hundredth of a degree is");
    TEST_CHECK(!CameraNoOp::sameAngle(359.99f - 360.0f), "also when it is negative");

    TEST_CHECK(CameraNoOp::sameZoom(0.0f), "no zoom change is no change");
    TEST_CHECK(CameraNoOp::sameZoom(2.0e-6f), "float noise on a zoom is no change");
    TEST_CHECK(!CameraNoOp::sameZoom(1.0e-3f), "a thousandth of a level is");
}
