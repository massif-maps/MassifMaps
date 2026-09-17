/*
 * Tests for FlightPath.h and UnitBezier.h, against mapbox-gl-js/src/ui/camera.ts.
 *
 * NOT covered: that AnimationHandler feeds it the VIEWPORT's span (ViewState::getSpanPerZoom) and
 * that the focus moves along the ratio - both need the view state. docs/internals/rendering/01-frame.md.
 */

#include "renderers/components/FlightPath.h"
#include "utils/UnitBezier.h"

#include <cmath>

using namespace massif;

#include "TestCheck.h"

namespace {

    const double RHO = 1.42;

    bool nearly(double value, double expected, double tolerance = 1.0e-6) {
        return std::fabs(value - expected) < tolerance * std::max(1.0, std::fabs(expected));
    }

    // The span at a zoom, for a viewport whose span at zoom 0 is 1.
    double span(double zoom) {
        return std::pow(2.0, -zoom);
    }

    void testStraightZoomlessMoveIsSymmetric() {
        FlightPath path;
        // One screenful across, same zoom at both ends: nothing distinguishes the two halves.
        path.setup(span(12), span(12), span(12), RHO);

        double ratioMid = 0, zoomMid = 0, ratioQ = 0, zoomQ = 0, ratioTQ = 0, zoomTQ = 0;
        path.sample(0.5, ratioMid, zoomMid);
        path.sample(0.25, ratioQ, zoomQ);
        path.sample(0.75, ratioTQ, zoomTQ);

        TEST_CHECK(nearly(ratioMid, 0.5, 1.0e-9), "halfway across at halfway through");
        TEST_CHECK(nearly(ratioQ + ratioTQ, 1.0, 1.0e-9), "the two halves mirror each other");
        TEST_CHECK(nearly(zoomQ, zoomTQ, 1.0e-9), "and pull back by the same amount");
        TEST_CHECK(zoomMid < 0, "the camera is pulled OUT at the middle of the path");
    }

    void testEndsLandOnTheTargets() {
        FlightPath path;
        path.setup(span(10), span(15), span(10) * 40, RHO);

        double ratio = 0, zoomDelta = 0;
        path.sample(0, ratio, zoomDelta);
        TEST_CHECK(nearly(ratio, 0, 1.0e-9) && nearly(zoomDelta, 0, 1.0e-9), "k=0 is the start");
        path.sample(1, ratio, zoomDelta);
        TEST_CHECK(nearly(ratio, 1.0, 1.0e-6), "k=1 has crossed the whole ground path");
        TEST_CHECK(nearly(zoomDelta, 5.0, 1.0e-6), "and arrived at the target zoom");
    }

    void testWOfSMatchesMapboxForAKnownMove() {
        // No zoom change, 10 screenfuls across: the deepest pull-back is their -log2(cosh(r0)).
        FlightPath path;
        path.setup(1.0, 1.0, 10.0, RHO);

        double rho2 = RHO * RHO;
        double b = (1.0 - 1.0 + rho2 * rho2 * 100.0) / (2 * 1.0 * rho2 * 10.0);
        double r0 = std::log(std::sqrt(b * b + 1) - b);
        double expectedDeepest = -std::log(std::cosh(r0)) / std::log(2.0);

        double ratio = 0, zoomDelta = 0;
        path.sample(0.5, ratio, zoomDelta);
        TEST_CHECK(nearly(zoomDelta, expectedDeepest, 1.0e-9),
                   "the deepest pull-back is mapbox's -log2(cosh(r0))");
        TEST_CHECK(nearly(path.getLength(), -2 * r0 / RHO, 1.0e-9),
                   "and S is theirs, (r1 - r0) / rho, symmetric here");
    }

    void testAScreenfulOfTravelBarelyPullsBack() {
        // What the tile-vs-viewport units bug cost: a 256-px tile on a 2400-px screen turns one
        // screenful of travel into 3.125, and a sub-level pull-back into over 1.5.
        FlightPath viewport, tile;
        viewport.setup(1.0, 1.0, 1.0, RHO);
        tile.setup(1.0, 1.0, 3.125, RHO);

        double ratio = 0, viewportZoom = 0, tileZoom = 0;
        viewport.sample(0.5, ratio, viewportZoom);
        tile.sample(0.5, ratio, tileZoom);
        TEST_CHECK(viewportZoom > -1.0, "one screenful across costs less than a zoom level");
        TEST_CHECK(tileZoom < -1.5, "the same move in tile units dives over one and a half");
        TEST_CHECK(nearly(viewportZoom - tileZoom, std::log(3.125) / std::log(2.0), 0.35),
                   "the error is the constant log2(screen / tile), not a scale");
    }

    void testPureZoomHasNoPath() {
        FlightPath path;
        path.setup(span(10), span(14), 0.0, RHO);

        double ratio = 0, zoomDelta = 0;
        path.sample(0.5, ratio, zoomDelta);
        TEST_CHECK(nearly(zoomDelta, 2.0, 1.0e-6), "a pure zoom is exponential: half the levels at half way");
        path.sample(1.0, ratio, zoomDelta);
        TEST_CHECK(nearly(zoomDelta, 4.0, 1.0e-6), "and all of them at the end");
        TEST_CHECK(path.getLength() > 0, "S is the log of the zoom ratio, not zero");
    }

    void testNoPathAndNoZoomEasesInsteadOfSnapping() {
        // Issue #179: S = 0 means nothing to say, NOT that the move is over. mapbox uses easeTo.
        FlightPath path;
        path.setup(span(12), span(12), 0.0, RHO);
        TEST_CHECK(nearly(path.getLength(), 0, 1.0e-12), "no path and no zoom is a zero-length S");

        double ratio = 0, zoomDelta = 0;
        path.sample(0.25, ratio, zoomDelta);
        TEST_CHECK(nearly(ratio, 0.25, 1.0e-9), "a quarter through is a quarter of the way, not all of it");
        path.sample(0.5, ratio, zoomDelta);
        TEST_CHECK(nearly(ratio, 0.5, 1.0e-9), "and halfway is halfway");
        TEST_CHECK(nearly(zoomDelta, 0, 1.0e-9), "with no zoom to give");
    }

    void testSubThresholdPathStillEases() {
        // A move below the w0 * 1e-6 threshold must still ease, or a caller that also changed
        // rotation sees the rotation ease while the position jumps under it.
        FlightPath path;
        path.setup(span(12), span(12), span(12) * 1.0e-9, RHO);

        double ratio = 0, zoomDelta = 0;
        path.sample(0.5, ratio, zoomDelta);
        TEST_CHECK(nearly(ratio, 0.5, 1.0e-9), "a sub-threshold move eases rather than snapping");
    }

    void testDurationFollowsThePathLength() {
        FlightPath near, far;
        near.setup(1.0, 1.0, 1.0, RHO);
        far.setup(1.0, 1.0, 100.0, RHO);

        double nearSeconds = near.suggestedDuration(1.2, 0.25);
        double farSeconds = far.suggestedDuration(1.2, 0.25);
        TEST_CHECK(farSeconds > nearSeconds, "a longer move takes longer");
        TEST_CHECK(farSeconds < nearSeconds * 100, "but a hundred times as far is nothing like a hundred times as long");
        TEST_CHECK(nearly(far.suggestedDuration(1.2, 0.25), far.getLength() / 1.2, 1.0e-9),
                   "the duration is S over their V");

        FlightPath still;
        still.setup(1.0, 1.0, 0.0, RHO);
        TEST_CHECK(nearly(still.suggestedDuration(1.2, 0.25), 0.25, 1.0e-9),
                   "a zero-length path still gets the floor, not an instant move");
    }

    void testCssEaseCurve() {
        // cubic-bezier(0.25, 0.1, 0.25, 1) - the curve mapbox-gl and maplibre-gl default to.
        UnitBezier ease(0.25, 0.1, 0.25, 1.0);
        TEST_CHECK(nearly(ease.solve(0), 0, 1.0e-9), "starts at 0");
        TEST_CHECK(nearly(ease.solve(1), 1.0, 1.0e-9), "ends at 1");
        TEST_CHECK(nearly(ease.solve(0.5), 0.8024, 2.0e-3), "and is well past halfway at halfway");

        // The point of easing: the last tenth of the time covers almost none of the move.
        TEST_CHECK(ease.solve(1.0) - ease.solve(0.9) < 0.1 * 0.5, "it lands slowly");
        TEST_CHECK(ease.solve(0.1) - ease.solve(0) < 0.1, "and leaves gently");

        UnitBezier linear(0.0, 0.0, 1.0, 1.0);
        TEST_CHECK(nearly(linear.solve(0.37), 0.37, 1.0e-6), "a straight bezier is the identity");
    }

    void testCssEaseIsMonotonic() {
        UnitBezier ease(0.25, 0.1, 0.25, 1.0);
        double previous = -1;
        for (int i = 0; i <= 100; i++) {
            double value = ease.solve(i / 100.0);
            if (!(value >= previous - 1.0e-9)) {
                TEST_CHECK(false, "the ease curve never goes backwards");
                return;
            }
            previous = value;
        }
        TEST_CHECK(true, "the ease curve never goes backwards");
    }

}

void testFlightPath() {
    testStraightZoomlessMoveIsSymmetric();
    testEndsLandOnTheTargets();
    testWOfSMatchesMapboxForAKnownMove();
    testAScreenfulOfTravelBarelyPullsBack();
    testPureZoomHasNoPath();
    testNoPathAndNoZoomEasesInsteadOfSnapping();
    testSubThresholdPathStillEases();
    testDurationFollowsThePathLength();
    testCssEaseCurve();
    testCssEaseIsMonotonic();
}
