/*
 * How far a label may be from the camera before it is not worth placing.
 *
 * A pitched view reaches many times further than the point it is centred on, and a label glyph is
 * screen-space: it does not shrink with distance the way the geometry it names does. So the horizon
 * band fills with full-size labels for features kilometres away. Measured at Paris z16.5: 950 live
 * labels looking down, 5001 at tilt 30 - see performance-log 27.
 *
 * Ported from maplibre's `CollisionIndex.perspectiveRatioCutoff` and mapbox's
 * `minPerspectiveRatio`. The trap this guards is the UNIT: the cut is against the camera-to-centre
 * distance, not metres. A metre cap cuts a city block at z18 and nothing at all at z10, because
 * camera-to-centre shrinks with zoom while a metre does not.
 */

#include "LabelDistance.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif::vt;

void testLabelDistance() {
    // At the point the view is centred on, the ratio is 1 and nothing is ever cut.
    TEST_CHECK(LabelDistance::perspectiveRatio(1000, 1000) == 1.0f,
               "a label at the view centre has ratio 1");
    TEST_CHECK(LabelDistance::perspectiveRatio(1000, 500) > 1.0f,
               "and one nearer than the centre is above 1");

    // maplibre's 0.6 cuts past 5x the camera-to-centre distance. Check the boundary from both
    // sides, since an off-by-one in the direction of the compare is invisible on a still map.
    TEST_CHECK(LabelDistance::perspectiveRatio(1000, 5000) == 0.6f,
               "5x the centre distance is exactly the cutoff ratio");
    TEST_CHECK(LabelDistance::perspectiveRatio(1000, 4999) > LabelDistance::PERSPECTIVE_RATIO_CUTOFF,
               "just inside 5x is kept");
    TEST_CHECK(LabelDistance::perspectiveRatio(1000, 5001) < LabelDistance::PERSPECTIVE_RATIO_CUTOFF,
               "and just outside it is cut");

    // The ratio is SCALE-FREE - that is the whole reason it is not in metres. Zooming in shrinks
    // the camera-to-centre distance and the label distance together, so the same label survives.
    TEST_CHECK(LabelDistance::perspectiveRatio(1000, 3000) == LabelDistance::perspectiveRatio(10, 30),
               "the ratio depends only on the ratio of the distances, not their size");

    // It falls toward 0.5 but never reaches it, so a cutoff of 0.5 or below cuts nothing at all.
    TEST_CHECK(LabelDistance::perspectiveRatio(1, 1000000) > 0.5f,
               "an extremely distant label still has a ratio above 0.5");
    TEST_CHECK(LabelDistance::cutoffDistance(1000, 0.5f) == 0,
               "so a cutoff at 0.5 has no finite distance behind it");
    // Tolerant: the cutoffs are floats, so 0.6f - 0.5 is 0.10000002 and the quotient lands just
    // short of 5000. A metre either way is meaningless here; being off by a FACTOR would not be.
    TEST_CHECK(std::abs(LabelDistance::cutoffDistance(1000, 0.6f) - 5000) < 1.0,
               "and 0.6 is 5x the camera-to-centre distance");
    TEST_CHECK(std::abs(LabelDistance::cutoffDistance(1000, 0.55f) - 10000) < 1.0,
               "while mapbox's 0.55 is 10x, as its own comment says");

    // A caller that has not set the view yet must not have every label cut out from under it.
    TEST_CHECK(LabelDistance::perspectiveRatio(0, 1000) == 1.0f,
               "no camera-to-centre distance means no cut");
    TEST_CHECK(LabelDistance::perspectiveRatio(1000, 0) == 1.0f,
               "and a label exactly at the camera is kept rather than dividing by zero");
}
