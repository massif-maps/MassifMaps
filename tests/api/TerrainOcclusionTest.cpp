/*
 * Whether a label anchor is behind the terrain (terrain/TerrainOcclusion.h).
 *
 * The case this exists for, measured at Grenoble from the south of La Bastille: POIs in the city and
 * along the slope below the fort were dropped, always the same ones whatever camera the 3D switch
 * started from, and they reappeared as soon as the camera rose. That is geometry, not tuning - an
 * anchor sits on the terrain, but the surface DRAWN under it differs by a small vertical error (the
 * mesh chord, a coarser elevation level, the half-resolution read-back), and along the view ray that
 * error is dz / sin(angle to the ground). At a grazing angle a metre of height is tens of metres of
 * depth, so the label reads as behind its own ground. Raising the flat tolerance to 0.5 did nothing,
 * because a flat relative slack does not know the angle.
 *
 * The depth SPREAD across the samples already measures the angle, so the slack comes from it.
 *
 * NOT covered here: the sampling itself (TerrainRenderer needs a GL depth read-back) and the value of
 * GRAZING_SLACK, which is a device judgement at the Grenoble camera named in the PR.
 */

#include "terrain/TerrainOcclusion.h"

#include "TestCheck.h"

using namespace massif;

void testTerrainOcclusion() {
    const float tolerance = 1.01f; // the default: MIN_OCCLUSION_TOLERANCE over 1.0

    // Top-down, or any view meeting the ground square on: the neighbourhood is all at one depth, so
    // there is no spread and the test is exactly the relative one it always was.
    TEST_CHECK(TerrainOcclusion::isBehind(2000.0f, 1000.0f, 0.0f, tolerance), "a label well behind flat ground is occluded");
    TEST_CHECK(!TerrainOcclusion::isBehind(1000.0f, 1000.0f, 0.0f, tolerance), "one exactly on it is not");
    TEST_CHECK(!TerrainOcclusion::isBehind(1005.0f, 1000.0f, 0.0f, tolerance), "and one inside the 1% slack is not");
    TEST_CHECK(TerrainOcclusion::isBehind(1020.0f, 1000.0f, 0.0f, tolerance), "just past the slack, it is");

    // The bug: a grazing view. The same 20-unit disagreement that reads as "behind" on flat ground is
    // nothing at all when the terrain's own depth swings 300 units across a few pixels.
    TEST_CHECK(!TerrainOcclusion::isBehind(1020.0f, 1000.0f, 300.0f, tolerance), "a grazing view gives the anchor room");
    TEST_CHECK(!TerrainOcclusion::isBehind(1250.0f, 1000.0f, 300.0f, tolerance), "and keeps giving it up to the local spread");

    // It is not unbounded: a label genuinely behind a ridge is still occluded, grazing or not.
    TEST_CHECK(TerrainOcclusion::isBehind(5000.0f, 1000.0f, 300.0f, tolerance), "a label far behind is still occluded");

    // Monotone in the label's own distance: moving a label further away never makes it LESS occluded.
    bool wasBehind = false;
    for (float labelW = 900.0f; labelW <= 5000.0f; labelW += 50.0f) {
        bool behind = TerrainOcclusion::isBehind(labelW, 1000.0f, 120.0f, tolerance);
        if (wasBehind && !behind) {
            TEST_CHECK(false, "occlusion is monotone in the label's distance");
            return;
        }
        wasBehind = behind;
    }
    TEST_CHECK(true, "occlusion is monotone in the label's distance");

    // More spread is more slack, never less - the whole point of taking it from the neighbourhood.
    TEST_CHECK(!TerrainOcclusion::isBehind(1400.0f, 1000.0f, 500.0f, tolerance), "a steeper grazing angle gives more room");
    TEST_CHECK(TerrainOcclusion::isBehind(1400.0f, 1000.0f, 50.0f, tolerance), "and a shallower one gives less");

    // A negative spread cannot happen (farthest >= nearest), but it must not eat the slack if it does.
    TEST_CHECK(!TerrainOcclusion::isBehind(1005.0f, 1000.0f, -100.0f, tolerance), "a negative spread is clamped away");

    // The style's own tolerance still works on top: raising it lets partly hidden features label.
    TEST_CHECK(!TerrainOcclusion::isBehind(1400.0f, 1000.0f, 0.0f, 1.5f), "a generous tolerance still applies");
}
