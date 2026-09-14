/*
 * How far the map is drawn (graphics/ViewDistance.h).
 *
 * The case this exists for: the DrawDistance ceiling scaled the ZOOM-derived orbit distance alone,
 * so dropping the camera near the ground in the Alps shortened the view with every zoom step and
 * cut off peaks a few kilometres away - while the tile walk, which scales the same height by a
 * larger factor, had already fetched them. Mapbox has no zoom-only ceiling at all: its far plane is
 * 10 * cameraAltitude / cos(pitch) (src/geo/projection/far_z.ts).
 *
 * The second case, which is why the cap is in here too: with ViewDistanceFactor 0 the cull envelope
 * stops at the far plane ALONE (CullWorker::calculateEnvelope), so a ceiling free to grow with the
 * camera's altitude is a tile walk free to grow with it.
 *
 * NOT covered here: the ray-derived horizon and the near plane, which need a whole ViewState - the
 * device run at the Crosscall camera named in the PR, with the PROF VIEW line, is what checks those.
 */

#include "graphics/ViewDistance.h"

#include "TestCheck.h"

using namespace massif;

namespace {
    // A tile width big enough that the 127-tile cap is never the binding limit below.
    const double WIDE = 1.0e9;
}

void testViewDistance() {
    // Zoomed out, the orbit is the larger of the two and nothing changes - which is what keeps a
    // flat world map drawing exactly as far as it did.
    TEST_CHECK(ViewDistance::cameraHeight(50000.0, 2600.0) == 50000.0, "a distant camera is its orbit distance");
    TEST_CHECK(ViewDistance::drawCeiling(50000.0, 16.0, WIDE) == 800000.0, "and the ceiling is DrawDistance multiples of it");

    // Near the ground on a mountain the two part company, and the altitude is the honest one: the
    // camera is 2.4 km above sea level whatever the zoom says about its orbit.
    TEST_CHECK(ViewDistance::cameraHeight(1000.0, 2400.0) == 2400.0, "a low camera in the mountains is its altitude");
    TEST_CHECK(ViewDistance::drawCeiling(1000.0, 16.0, WIDE) < ViewDistance::drawCeiling(ViewDistance::cameraHeight(1000.0, 2400.0), 16.0, WIDE),
               "which draws further than the orbit alone would");

    // The bug in one line: on the orbit alone the ceiling halves with every zoom step, so the view
    // shortens as the camera comes down. On the height it holds while the altitude does.
    double altitude = 2400.0;
    double previous = 0;
    for (double orbit = 4000.0; orbit >= 250.0; orbit *= 0.5) {
        double ceiling = ViewDistance::drawCeiling(ViewDistance::cameraHeight(orbit, altitude), 16.0, WIDE);
        if (previous > 0 && ceiling > previous) {
            TEST_CHECK(false, "the ceiling never grows as the camera descends");
            return;
        }
        previous = ceiling;
    }
    TEST_CHECK(previous == altitude * 16.0, "and it stops shrinking once the altitude is the larger of the two");

    // Monotone in both arguments, which is what makes it safe to raise: no camera loses view.
    TEST_CHECK(ViewDistance::cameraHeight(1000.0, 2400.0) >= 1000.0, "the height is never below the orbit");
    TEST_CHECK(ViewDistance::cameraHeight(1000.0, 0.0) == 1000.0, "a camera at sea level is unchanged");
    TEST_CHECK(ViewDistance::cameraHeight(1000.0, -50.0) == 1000.0, "and so is one below it");

    // Tangram's guard: 127 tile widths, whatever the height asks for.
    TEST_CHECK(ViewDistance::tileWalkCap(100.0) == 12700.0, "the cap is 127 tile widths");
    TEST_CHECK(ViewDistance::drawCeiling(1.0e6, 16.0, 100.0) == 12700.0, "a camera high above a small tile is capped by it");
    TEST_CHECK(ViewDistance::drawCeiling(100.0, 16.0, 100.0) == 1600.0, "and a camera under the cap is not touched by it");

    // The cap holds for every altitude, which is what bounds the tile walk with ViewDistanceFactor 0.
    for (double alt = 1.0e3; alt <= 1.0e8; alt *= 10.0) {
        if (ViewDistance::drawCeiling(ViewDistance::cameraHeight(500.0, alt), 16.0, 611.0) > ViewDistance::tileWalkCap(611.0)) {
            TEST_CHECK(false, "no altitude draws past the tile walk cap");
            return;
        }
    }
    TEST_CHECK(true, "no altitude draws past the tile walk cap");
}
