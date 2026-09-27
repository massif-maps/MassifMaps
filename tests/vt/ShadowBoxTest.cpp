/*
 * The cascade light box (vt/ShadowBox.h): padded around its bounding sphere, centre rounded to a
 * world-anchored lattice. The caster pass is only skipped when the matrix repeats bit-for-bit, so a
 * box that slid with the camera re-rendered every terrain caster on every moving frame (measured on
 * the Crosscall: 43 caster passes over 97 panning frames, 10 once padded).
 *
 * NOT covered here: the caster cull against the box and the refresh decision in MapRenderer; both
 * need the renderer.
 */

#include "ShadowBox.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif::vt;

namespace {
    const double PADDING = 0.2;
    const int MAP_SIZE = 2048;

    bool holdsSphere(const ShadowBox& box, double x, double y, double depth, double radius) {
        double slack = 1.0e-9 * box.halfSize;
        return box.left <= x - radius + slack && box.right >= x + radius - slack && box.bottom <= y - radius + slack && box.top >= y + radius - slack
            && std::abs(box.centerDepth - depth) <= box.halfSize - radius + slack;
    }

    bool same(const ShadowBox& a, const ShadowBox& b) {
        return a.left == b.left && a.right == b.right && a.bottom == b.bottom && a.top == b.top && a.centerDepth == b.centerDepth;
    }
}

void testShadowBox() {
    // 1. Wherever the sphere lands, the padded, snapped box holds all of it: a sphere poking out is a
    // shadow cut off along a straight edge.
    {
        bool allHeld = true;
        for (int i = 0; i < 2000; i++) {
            double x = std::sin(i * 12.9898) * 43758.5453, y = std::cos(i * 78.233) * 1234.567, depth = i * 0.37;
            double radius = 50.0 + (i % 97) * 3.1;
            allHeld = allHeld && holdsSphere(ShadowBox::fit(x, y, depth, radius, PADDING, MAP_SIZE), x, y, depth, radius);
        }
        TEST_CHECK(allHeld, "the sphere always fits inside the padded box");
    }

    // 2. A camera panning in small steps keeps the same box for most of them: that is the whole point.
    {
        double radius = 400.0;
        ShadowBox previous = ShadowBox::fit(0, 0, 0, radius, PADDING, MAP_SIZE);
        int changes = 0, steps = 200;
        for (int i = 1; i <= steps; i++) {
            ShadowBox box = ShadowBox::fit(i * 2.0, i * 1.0, 0, radius, PADDING, MAP_SIZE);
            changes += same(box, previous) ? 0 : 1;
            previous = box;
        }
        TEST_CHECK(changes > 0 && changes <= steps / 20, "a slow pan moves the box once every many frames");
        TEST_CHECK(same(ShadowBox::fit(7, 3, 1, radius, PADDING, MAP_SIZE), ShadowBox::fit(7, 3, 1, radius, PADDING, MAP_SIZE)), "the fit is deterministic");
    }

    // 3. The box stays on whole texels of a world-anchored grid, or shadow edges crawl as it moves.
    {
        ShadowBox box = ShadowBox::fit(1234.5, -987.25, 10, 333.0, PADDING, MAP_SIZE);
        double texel = (box.right - box.left) / MAP_SIZE;
        double cells = box.left / texel;
        TEST_CHECK(std::abs(cells - std::round(cells)) < 1.0e-6, "the left edge sits on the texel lattice");
        TEST_CHECK(box.halfSize >= 333.0 * (1.0 + PADDING), "the padding is at least what was asked");
        TEST_CHECK(box.halfSize <= 333.0 * (1.0 + PADDING) * 1.125 + 1.0e-9, "... and the size quantisation wastes at most an eighth");
    }

    // 4. No padding degenerates to the old texel snap: the sphere still fits.
    {
        ShadowBox box = ShadowBox::fit(10.3, 20.7, 5.0, 100.0, 0.0, MAP_SIZE);
        TEST_CHECK(box.left <= 10.3 - 100.0 + (box.right - box.left) / MAP_SIZE, "unpadded, the box is off by at most a texel");
    }
}
