/*
 * Tests for the tile LOD rule (all/native/layers/TileLODRule.h), maplibre's
 * createCalculateTileZoomFunction expressed as the two corrections our screen-area rule needs.
 *
 * The two rules decompose identically - level = const - log2(distance) + (p/2)*log2(cos theta),
 * p = 1 for the area rule (docs/internals/rendering/02-tiles.md) - so the port is those two
 * numbers, not a new walk: an exponent on the area's cos term, and a level drop applied to every
 * tile in the frame that bounds how many more tiles a pitched view may ask for than a flat one.
 * The second one is inert at maplibre's own defaults and the test below pins that, because it is
 * the opposite of what it sounds like: it bounds the cost of asking for a GENTLER far field.
 *
 * NOT ported: the reference PITCH GATE (maplibre's allowVariableZoom, mapbox's MIN_LOD_PITCH),
 * which keeps every tile at the camera's zoom below ~60 degrees of pitch. It cost a device round -
 * see the note in docs/internals/rendering/02-tiles.md. It is affordable for them because their
 * maxPitch IS 60, so the horizon is never on screen; we draw to tangram's view distance, and one
 * zoom level over that much ground is a map that does not move.
 *
 * NOT covered here: that the walk then picks those tiles. calculateVisibleTiles needs Options and
 * the renderer (see ../README.md) - a device check, at the camera named in the PR.
 */

#include "layers/TileLODRule.h"

#include <cmath>

using namespace massif;

#include "TestCheck.h"

namespace {
    const double DEG = 3.14159265358979323846 / 180.0;
    // maplibre's defaults, and the fov its own comment sizes them at.
    const double MAX_LEVELS = 9.314;
    const double COUNT_RATIO = 3.0;
    const double FOV = 36.87;
}

void testTileLODRule() {
    // maplibre's defaults come out at p = 1, which IS our area rule - so adopting their formula
    // does not silently move the near field. Anything else here would be a porting bug.
    double p = lodCosThetaPower(MAX_LEVELS, FOV);
    TEST_CHECK(std::abs(p - 1.0) < 0.01, "maplibre's defaults reproduce the area rule exactly (p = 1)");

    // Fewer levels allowed on screen = a gentler decay toward the horizon, more = harsher. The
    // sign matters more than the value: getting it backwards refines the horizon and coarsens the
    // near field, which looks like the rule working and costs 4x the tiles.
    TEST_CHECK(lodCosThetaPower(4.0, FOV) < p, "allowing fewer zoom levels on screen decays more slowly");
    TEST_CHECK(lodCosThetaPower(16.0, FOV) > p, "allowing more zoom levels on screen decays faster");

    // A top-down view asks for no more tiles than a top-down view, so it sheds nothing. Every
    // style that renders correctly at tilt 90 today has to stay untouched.
    TileLODRule topDown = calculateTileLODRule(MAX_LEVELS, COUNT_RATIO, FOV, 0.0);
    TEST_CHECK(topDown.uniformLevelDrop == 0.0, "a top-down view is inside the tile budget by definition");

    // SURPRISE, and the reason the cap alone is not a perf fix: at p >= 1 the integrand cos^(p-1)
    // is at most 1, so a pitched view never counts as asking for MORE tiles than a flat one and the
    // ratio is inert. maplibre's own defaults sit exactly there. The cap exists to bound the bill
    // of asking for a GENTLER decay (p < 1), not to rescue the default.
    TEST_CHECK(calculateTileLODRule(MAX_LEVELS, COUNT_RATIO, FOV, 85 * DEG).uniformLevelDrop == 0.0,
               "at maplibre's own defaults the tile count cap never binds");

    // With a gentler decay asked for, it binds - and only at a grazing pitch.
    const double GENTLE = 4.0;
    TileLODRule pitch30 = calculateTileLODRule(GENTLE, COUNT_RATIO, FOV, 30 * DEG);
    TileLODRule pitch65 = calculateTileLODRule(GENTLE, COUNT_RATIO, FOV, 65 * DEG);
    TileLODRule pitch85 = calculateTileLODRule(GENTLE, COUNT_RATIO, FOV, 85 * DEG);
    TEST_CHECK(pitch30.uniformLevelDrop == 0.0, "a moderate pitch still fits the budget");
    TEST_CHECK(pitch65.uniformLevelDrop > 0.0, "a grazing pitch sheds levels to stay in budget");
    TEST_CHECK(pitch85.uniformLevelDrop > pitch65.uniformLevelDrop, "the closer to the horizon, the more is shed");

    // A larger ratio buys a bigger pitched tile count, so it sheds less - that is the knob a
    // desktop profile raises and a phone profile lowers.
    TileLODRule generous = calculateTileLODRule(GENTLE, 8.0, FOV, 85 * DEG);
    TEST_CHECK(generous.uniformLevelDrop < pitch85.uniformLevelDrop, "a larger tile count ratio sheds fewer levels");

    // The drop is in LEVELS, so it stays a sane number - a rule that shed 5 levels at the horizon
    // would coarsen the near field with it, since it applies to every tile in the frame.
    TEST_CHECK(pitch85.uniformLevelDrop < 3.0, "even at the horizon the uniform drop stays bounded");

    // The integral is what both of the above are built on; cos^0 over a range is the range.
    TEST_CHECK(std::abs(lodIntegralOfCosXByP(0.0, 0.0, 1.0) - 1.0) < 1.0e-9, "cos^0 integrates to the interval width");
    TEST_CHECK(std::abs(lodIntegralOfCosXByP(1.0, 0.0, 1.5707963) - 1.0) < 5.0e-3, "cos integrates to 1 over a quarter turn");
}
