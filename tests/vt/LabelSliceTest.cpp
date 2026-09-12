/*
 * The floor under a rationed label-placement pass (vt/LabelSlice.h).
 *
 * One deadline is shared by every layer in a pass, so the layers whose turn comes after it is spent
 * enter with it already gone. Without a floor they stop on their first iteration, leave the cursor
 * exactly where it was, and never place a label again - measured on the Crosscall, two of three
 * layers sat at 2048 of 4075 and 96 of 172 for as long as the map was panned. Both stuck cursors
 * were multiples of 32, which is the whole reason the clock-read mask could not carry this alone.
 *
 * NOT covered here: that a pass then completes a cycle, which needs the culler and a view - that is
 * the device check named in the PR.
 */

#include "LabelSlice.h"

using namespace massif::vt;

#include "TestCheck.h"

void testLabelSlice() {
    // The bug, in one line: resuming exactly on a multiple of 32 used to allow an immediate stop.
    TEST_CHECK(!labelSliceMayStop(2048, 2048, MIN_SLICE_LABELS), "a pass may not stop before it has done anything");
    TEST_CHECK(!labelSliceMayStop(96, 96, MIN_SLICE_LABELS), "and that holds at any multiple of the mask");
    TEST_CHECK(!labelSliceMayStop(0, 0, MIN_SLICE_LABELS), "including the very first pass of a cycle");

    // Nor part-way through the floor.
    TEST_CHECK(!labelSliceMayStop(2049, 2048, MIN_SLICE_LABELS), "nor one label in");
    TEST_CHECK(!labelSliceMayStop(2079, 2048, MIN_SLICE_LABELS), "nor one short of the floor");

    // Once the floor is met it may stop, and only on the mask - the mask is what keeps the clock
    // read off most iterations, so the two conditions have to hold together.
    TEST_CHECK(labelSliceMayStop(2080, 2048, MIN_SLICE_LABELS), "at the floor, on the mask, it may stop");
    TEST_CHECK(!labelSliceMayStop(2081, 2048, MIN_SLICE_LABELS), "past the floor but off the mask it may not");
    TEST_CHECK(labelSliceMayStop(2112, 2048, MIN_SLICE_LABELS), "the next mask point may");

    // A cursor off the mask still gets its floor, and still only stops on a mask point.
    TEST_CHECK(!labelSliceMayStop(2050, 2047, MIN_SLICE_LABELS), "an odd cursor is no different");
    TEST_CHECK(labelSliceMayStop(2080, 2047, MIN_SLICE_LABELS), "and stops at the first mask point past its floor");

    // Whatever happens, progress is bounded: a pass can never examine fewer than the floor.
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
