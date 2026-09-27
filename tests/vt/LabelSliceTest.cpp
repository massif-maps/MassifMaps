// The floor under a rationed label-placement pass (vt/LabelSlice.h): one deadline is shared by every
// layer, so a later layer enters with it spent and, without a floor, never advances its cursor.

#include "LabelSlice.h"

using namespace massif::vt;

#include "TestCheck.h"

void testLabelSlice() {
    // A cursor resuming on a mask point must not stop immediately.
    TEST_CHECK(!labelSliceMayStop(2048, 2048, MIN_SLICE_LABELS), "a pass may not stop before it has done anything");
    TEST_CHECK(!labelSliceMayStop(96, 96, MIN_SLICE_LABELS), "and that holds at any multiple of the mask");
    TEST_CHECK(!labelSliceMayStop(0, 0, MIN_SLICE_LABELS), "including the very first pass of a cycle");

    TEST_CHECK(!labelSliceMayStop(2049, 2048, MIN_SLICE_LABELS), "nor one label in");
    TEST_CHECK(!labelSliceMayStop(2079, 2048, MIN_SLICE_LABELS), "nor one short of the floor");

    // The mask keeps the clock read off most iterations, so floor and mask must both hold.
    TEST_CHECK(labelSliceMayStop(2080, 2048, MIN_SLICE_LABELS), "at the floor, on the mask, it may stop");
    TEST_CHECK(!labelSliceMayStop(2081, 2048, MIN_SLICE_LABELS), "past the floor but off the mask it may not");
    TEST_CHECK(labelSliceMayStop(2112, 2048, MIN_SLICE_LABELS), "the next mask point may");

    TEST_CHECK(!labelSliceMayStop(2050, 2047, MIN_SLICE_LABELS), "an odd cursor is no different");
    TEST_CHECK(labelSliceMayStop(2080, 2047, MIN_SLICE_LABELS), "and stops at the first mask point past its floor");

    for (std::size_t cursor = 0; cursor < 200; cursor++) {
        std::size_t stop = cursor;
        while (!labelSliceMayStop(stop, cursor, MIN_SLICE_LABELS)) {
            stop++;
        }
        if (stop - cursor < MIN_SLICE_LABELS) {
            TEST_CHECK(false, "every cursor makes at least the floor of progress");
            break;
        }
    }
    TEST_CHECK(true, "every cursor makes at least the floor of progress");
}
