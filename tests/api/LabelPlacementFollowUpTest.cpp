// What a label placement pass owes once done (renderers/workers/LabelPlacementFollowUp.h): the worker spun
// 140k empty passes a second, each asking for a redraw, while the surface was 0x0. The worker itself needs
// MapRenderer and is not covered; that a still, label-free map leaves its thread idle is a device check.

#include "renderers/workers/LabelPlacementFollowUp.h"

#include "TestCheck.h"

#include <cglib/mat.h>

#include <cmath>
#include <limits>

using namespace massif;

namespace {
    // ViewState::calculatePerspMat's frustum, with its divide by the surface height.
    cglib::mat4x4<double> perspective(double width, double height, double focusOffsetX) {
        double near = 10, far = 1000, tanHalfFOVY = std::tan(22.5 * 3.14159265358979 / 180);
        double aspectRatio = width / height;
        double top = near * tanHalfFOVY, bottom = -top;
        double left = bottom * aspectRatio, right = top * aspectRatio;
        double dx = 2 * near * tanHalfFOVY * focusOffsetX / height;
        return cglib::frustum4_matrix(left + dx, right + dx, bottom, top, near, far);
    }
}

void testLabelPlacementFollowUp() {
    cglib::mat4x4<double> zeroSurface = perspective(0, 0, 0);
    cglib::mat4x4<double> surface = perspective(1200, 800, 0);

    TEST_CHECK(zeroSurface != zeroSurface, "a 0x0 surface's view compares unequal to itself, which read as a moving camera");
    TEST_CHECK(!isLabelPlacementViewValid(zeroSurface), "so no label is placed for a 0x0 surface");
    TEST_CHECK(isLabelPlacementViewValid(surface), "but one is for a sized surface");
    TEST_CHECK(isLabelPlacementViewValid(perspective(1200, 800, 40)), "and with the focus point offset");
    cglib::mat4x4<double> infinite = surface;
    infinite(2, 3) = std::numeric_limits<double>::infinity();
    TEST_CHECK(!isLabelPlacementViewValid(infinite), "an infinite entry is as unplaceable as NaN");

    LabelPlacementFollowUp emptyStill = labelPlacementFollowUp(false, true, false);
    TEST_CHECK(!emptyStill.redraw && !emptyStill.continuation, "a pass with no label owes neither a redraw nor another pass");
    LabelPlacementFollowUp emptyMoved = labelPlacementFollowUp(false, true, true);
    TEST_CHECK(!emptyMoved.redraw && !emptyMoved.continuation, "not even when the camera moved during it");

    LabelPlacementFollowUp still = labelPlacementFollowUp(true, true, false);
    TEST_CHECK(still.redraw && !still.continuation, "a finished pass over labels redraws once and lets the thread sleep");
    LabelPlacementFollowUp moved = labelPlacementFollowUp(true, true, true);
    TEST_CHECK(moved.redraw && moved.continuation, "one placed for a camera that has since moved is redone");
    LabelPlacementFollowUp sliced = labelPlacementFollowUp(true, false, false);
    TEST_CHECK(sliced.redraw && sliced.continuation, "an unfinished cycle continues");
}
