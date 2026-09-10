/*
 * maplibre's CollisionIndex viewportPadding: labels are placed against a viewport grown by a band
 * of screen pixels, so a name is already placed - and, through skipFade, already at full opacity -
 * by the time it scrolls in. Placed only inside the true viewport, a label starts its fade at the
 * edge and is still fading when it has reached the middle of the screen.
 *
 * Ours scales that band by sin(tilt): 100 screen pixels at the horizon are kilometres of map and
 * thousands of labels, while at top-down they are 100 pixels of map.
 *
 * NOT covered here: the culler's grid and the skipFade itself (LabelCuller is not in this link).
 * What this pins is that the padded frustum a label is placed against really does reach past the
 * viewport, and by how much.
 */

#include "ViewState.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif::vt;

namespace {
    /** A camera looking straight down from `height`, with a 90 degree vertical field of view. */
    ViewState buildViewState(float tilt, float resolution) {
        cglib::vec3<double> eye(0, 0, 100);
        cglib::mat4x4<double> cameraMatrix = cglib::lookat4_matrix(eye, cglib::vec3<double>(0, 0, 0), cglib::vec3<double>(0, 1, 0));
        cglib::mat4x4<double> projectionMatrix = cglib::perspective4_matrix(1.0, 1.0, 1.0, 1.0, 1000.0);
        return ViewState(projectionMatrix, cameraMatrix, 0, 0, tilt, 1.0f, resolution);
    }

    /** How far along +x the frustum still admits a point on the ground plane. */
    double reachAlongX(const cglib::frustum3<double>& frustum) {
        double lo = 0, hi = 100000;
        for (int i = 0; i < 60; i++) {
            double mid = (lo + hi) * 0.5;
            cglib::bbox3<double> point(cglib::vec3<double>(mid, 0, 0), cglib::vec3<double>(mid, 0, 0));
            (frustum.inside(point) ? lo : hi) = mid;
        }
        return lo;
    }
}

void testLabelPadding() {
    // Top-down: the full band, which is maplibre's own constant.
    ViewState down = buildViewState(90.0f, 1000.0f);
    TEST_CHECK(std::abs(down.labelPadding - ViewState::MAX_LABEL_PADDING) < 0.01f,
               "looking straight down the band is maplibre's 100 px");

    // The padded frustum has to admit MORE of the world than the plain one, in the ratio the extra
    // pixels ask for: 100 px on each side of a 1000 px viewport is 20% more world.
    double plain = reachAlongX(down.frustum);
    double padded = reachAlongX(down.labelFrustum);
    TEST_CHECK(padded > plain, "the padded frustum reaches past the viewport");
    double expected = plain * (1000.0 + 2 * 100.0) / 1000.0;
    TEST_CHECK(std::abs(padded - expected) / expected < 0.01,
               "and it reaches exactly the extra pixels asked for");

    // Tilted towards the horizon the band shrinks, because a screen pixel there covers far more map.
    ViewState tilted = buildViewState(30.0f, 1000.0f);
    TEST_CHECK(tilted.labelPadding < down.labelPadding, "tilting towards the horizon shrinks the band");
    TEST_CHECK(std::abs(tilted.labelPadding - ViewState::MAX_LABEL_PADDING * 0.5f) < 0.01f,
               "by sin(tilt) - half the band at 30 degrees");

    // ...but never to nothing: a label still needs somewhere to be placed before it crosses the edge.
    ViewState horizon = buildViewState(0.0f, 1000.0f);
    TEST_CHECK(std::abs(horizon.labelPadding - ViewState::MIN_LABEL_PADDING) < 0.01f,
               "at the horizon it stops at the floor rather than reaching zero");
    TEST_CHECK(reachAlongX(horizon.labelFrustum) > reachAlongX(horizon.frustum),
               "and the floor still reaches past the viewport");
}
