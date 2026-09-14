/*
 * The view-projection matrix and its inverse, resolved once with the frame's camera (vt/ViewState.h).
 *
 * Label::setupCoordinateSystem snaps every point label's anchor to the screen pixel grid - that is
 * what keeps glyphs sharp - and to do it, it projected the anchor and un-projected the snapped
 * position. Both matrices are the same for every label in the frame, and it built them PER LABEL:
 * a 4x4 product and a 4x4 double INVERSE, 3589 times an interval on the Crosscall, inside the loop
 * that RenderStats calls buildMs.
 *
 * Caching them is only safe while they cannot disagree with the components they came from, which is
 * what this pins: nothing assigns projectionMatrix or cameraMatrix after construction today, and a
 * setter that did would have to refresh these too.
 *
 * NOT covered here: that the snap itself is unchanged - the label tests that check anchor positions
 * (LabelAnchorAlignTest, LabelPerspectiveTest, LineLabelTest) cover that, and they run the snap.
 */

#include "ViewState.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif::vt;

namespace {
    bool matricesClose(const cglib::mat4x4<double>& m1, const cglib::mat4x4<double>& m2, double epsilon) {
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                if (std::abs(m1(i, j) - m2(i, j)) > epsilon) {
                    return false;
                }
            }
        }
        return true;
    }

    ViewState buildViewState(float tilt, double eyeHeight = 3000.0) {
        cglib::vec3<double> eye(1000.0, -2000.0, eyeHeight);
        cglib::mat4x4<double> cameraMatrix = cglib::lookat4_matrix(eye, cglib::vec3<double>(0, 0, 0), cglib::vec3<double>(0, 0, 1));
        cglib::mat4x4<double> projectionMatrix = cglib::perspective4_matrix(1.0, 1.5, 1.0, 10.0, 100000.0);
        return ViewState(projectionMatrix, cameraMatrix, 15.0f, 0.0f, tilt, 1.5f, 1080.0f);
    }
}

void testViewStateMatrix() {
    ViewState viewState = buildViewState(60.0f);

    // The product, which the frustum is already built from and every label projection needs.
    TEST_CHECK(matricesClose(viewState.viewProjMatrix, viewState.projectionMatrix * viewState.cameraMatrix, 1.0e-9),
               "the cached view-projection is the product of the two it came from");

    // The inverse, which the anchor snap un-projects through. Checked as a round trip rather than
    // against another inverse, so it holds whatever the implementation does.
    TEST_CHECK(matricesClose(viewState.viewProjMatrix * viewState.invViewProjMatrix, cglib::mat4x4<double>::identity(), 1.0e-6),
               "and the cached inverse undoes it");

    // A point through both is itself: this IS the snap's round trip, at a real camera.
    cglib::vec3<double> position(123.0, -456.0, 78.0);
    cglib::vec4<double> clipPos = cglib::transform(cglib::vec4<double>(position(0), position(1), position(2), 1), viewState.viewProjMatrix);
    TEST_CHECK(clipPos(3) > 0, "the test point is in front of the camera");
    cglib::vec3<double> ndc(clipPos(0) / clipPos(3), clipPos(1) / clipPos(3), clipPos(2) / clipPos(3));
    cglib::vec3<double> roundTrip = cglib::transform_point(ndc, viewState.invViewProjMatrix);
    for (int i = 0; i < 3; i++) {
        if (std::abs(roundTrip(i) - position(i)) > 1.0e-3) {
            TEST_CHECK(false, "a point projected and un-projected through the pair is itself");
            return;
        }
    }
    TEST_CHECK(true, "a point projected and un-projected through the pair is itself");

    // A DEFAULT-constructed view state must be self-consistent too, or a label snapped through one
    // would be thrown to an arbitrary position rather than left where it is.
    ViewState defaultState;
    TEST_CHECK(matricesClose(defaultState.viewProjMatrix, defaultState.projectionMatrix * defaultState.cameraMatrix, 1.0e-9),
               "a default view state's cached product agrees with its components");
    TEST_CHECK(matricesClose(defaultState.viewProjMatrix * defaultState.invViewProjMatrix, cglib::mat4x4<double>::identity(), 1.0e-9),
               "and so does its inverse");

    // A COPY carries them, since the renderer and the culler each hold their own.
    ViewState copy = viewState;
    TEST_CHECK(matricesClose(copy.viewProjMatrix, viewState.viewProjMatrix, 0.0),
               "a copied view state keeps the resolved product");
    TEST_CHECK(matricesClose(copy.invViewProjMatrix, viewState.invViewProjMatrix, 0.0),
               "and the resolved inverse");

    // A different CAMERA does not share them - the obvious way a cache goes wrong. (Note the tilt
    // field alone would not: it does not feed these matrices, it only describes them.)
    ViewState other = buildViewState(60.0f, 5000.0);
    TEST_CHECK(!matricesClose(other.viewProjMatrix, viewState.viewProjMatrix, 1.0e-9),
               "a different camera resolves a different product");
}
