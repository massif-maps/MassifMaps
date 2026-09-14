/*
 * Where the focus sits in z against the ground that is actually drawn (terrain/FocusLift.h).
 *
 * The case this exists for: terrain lifts the focus onto the surface every frame, and switching
 * terrain OFF used to leave it there. The zoom is calibrated on dist(camera, focus), so from that
 * moment every distance described a camera nearer than it was - measured on the Crosscall at
 * focusZ 107 m over a ground back at z=0, which drew the map for a camera six times too close:
 * coarse tiles, tiny labels, and ground running out into the background colour.
 *
 * NOT covered here: that the renderer calls this on both arms of its elevation-manager branch -
 * that is the device check named in the PR (enable 3D terrain, move, disable it).
 */

#include "terrain/FocusLift.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif;

void testFocusLift() {
    // With terrain, the focus goes to the ground under it, from wherever it is.
    TEST_CHECK(focusLiftDelta(true, true, 220.0, 0.0) == 220.0, "a focus at sea level is lifted onto the terrain");
    TEST_CHECK(focusLiftDelta(true, true, 220.0, 220.0) == 0.0, "one already on the terrain does not move");
    TEST_CHECK(focusLiftDelta(true, true, 75.0, 107.0) == -32.0, "and one above it comes back down");

    // Nothing decoded under the focus yet: hold. Dropping it to sea level would make the camera
    // dive every time the DEM has not caught up with a pan.
    TEST_CHECK(focusLiftDelta(true, false, 0.0, 107.0) == 0.0, "an unknown terrain height holds the focus");
    TEST_CHECK(focusLiftDelta(true, false, 0.0, 0.0) == 0.0, "whatever the focus is at");

    // The bug, in one line: terrain off means the ground is at z=0, so the focus goes with it.
    TEST_CHECK(focusLiftDelta(false, false, 0.0, 107.0) == -107.0, "terrain off brings the focus back to the ground");
    TEST_CHECK(focusLiftDelta(false, true, 220.0, 107.0) == -107.0, "even if a height is still cached");
    TEST_CHECK(focusLiftDelta(false, false, 0.0, 0.0) == 0.0, "and a focus already there does not move");

    // Applying the delta always lands ON the drawn ground, which is the invariant that broke.
    for (double focusZ = -50.0; focusZ <= 400.0; focusZ += 37.0) {
        if (focusZ + focusLiftDelta(false, false, 0.0, focusZ) != 0.0) {
            TEST_CHECK(false, "terrain off always lands the focus at z=0");
            return;
        }
        if (focusZ + focusLiftDelta(true, true, 180.0, focusZ) != 180.0) {
            TEST_CHECK(false, "terrain on always lands the focus on the terrain");
            return;
        }
    }
    TEST_CHECK(true, "applying the delta lands the focus on the drawn ground, from any height");
}
