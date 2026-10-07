// Whether a label anchor is behind the terrain (terrain/TerrainOcclusion.h). A small height error
// becomes dz / sin(angle) of depth at a grazing view, so the slack scales with the sampled depth spread.

#include "terrain/TerrainOcclusion.h"

#include "TestCheck.h"

using namespace massif;

namespace {
    // The CPU ray march behind a label outside the read-back depth (TileRenderer's ray fallback). The
    // ground rises toward the label at 0.3 height per unit; the camera looks down at it from x = 0.
    void testSegmentBlocked() {
        const double maxFraction = 1.0 / 1.01;
        const double margin = 30.0;
        const cglib::vec3<double> camera(0, 0, 600);
        auto slope = [](double x, double, double& z) { z = 0.3 * x; return true; };
        auto blocked = [&](const cglib::vec3<double>& label, auto ground) {
            return TerrainOcclusion::isSegmentBlocked(camera, label, maxFraction, 5.0, 1.0, 10000.0, margin, ground);
        };

        TEST_CHECK(!blocked(cglib::vec3<double>(1000, 0, 300), slope), "a label on open ground is seen");
        TEST_CHECK(!blocked(cglib::vec3<double>(1000, 0, 280), slope), "an anchor 20 under the live ground is not hidden by the slope it stands on");

        auto ridge = [](double x, double, double& z) { z = (x > 400 && x < 600) ? 500.0 : 0.3 * x; return true; };
        TEST_CHECK(blocked(cglib::vec3<double>(1000, 0, 300), ridge), "a ridge between camera and label hides it");
        TEST_CHECK(blocked(cglib::vec3<double>(1000, 0, 280), ridge), "and still does for a lagging anchor");

        auto bumpAt = [](double from, double to) {
            return [from, to](double x, double, double& z) { z = (x > from && x < to) ? 0.3 * x + 80.0 : 0.3 * x; return true; };
        };
        TEST_CHECK(!blocked(cglib::vec3<double>(1000, 0, 300), bumpAt(975, 988)), "a bump in the label's own cell does not hide it");
        TEST_CHECK(blocked(cglib::vec3<double>(1000, 0, 300), bumpAt(900, 930)), "the same bump a few cells out does");

        auto nothing = [](double, double, double&) { return false; };
        TEST_CHECK(!blocked(cglib::vec3<double>(1000, 0, 300), nothing), "no loaded ground blocks nothing");
        TEST_CHECK(!blocked(cglib::vec3<double>(1000, 0, 5000), ridge), "a label above every summit is seen");
    }
}

void testTerrainOcclusion() {
    const float tolerance = 1.01f; // default: 1.0 + MIN_OCCLUSION_TOLERANCE

    // No spread (top-down): the plain relative test.
    TEST_CHECK(TerrainOcclusion::isBehind(2000.0f, 1000.0f, 0.0f, tolerance), "a label well behind flat ground is occluded");
    TEST_CHECK(!TerrainOcclusion::isBehind(1000.0f, 1000.0f, 0.0f, tolerance), "one exactly on it is not");
    TEST_CHECK(!TerrainOcclusion::isBehind(1005.0f, 1000.0f, 0.0f, tolerance), "and one inside the 1% slack is not");
    TEST_CHECK(TerrainOcclusion::isBehind(1020.0f, 1000.0f, 0.0f, tolerance), "just past the slack, it is");

    TEST_CHECK(!TerrainOcclusion::isBehind(1020.0f, 1000.0f, 300.0f, tolerance), "a grazing view gives the anchor room");
    TEST_CHECK(!TerrainOcclusion::isBehind(1250.0f, 1000.0f, 300.0f, tolerance), "and keeps giving it up to the local spread");

    TEST_CHECK(TerrainOcclusion::isBehind(5000.0f, 1000.0f, 300.0f, tolerance), "a label far behind is still occluded");

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

    TEST_CHECK(!TerrainOcclusion::isBehind(1400.0f, 1000.0f, 500.0f, tolerance), "a steeper grazing angle gives more room");
    TEST_CHECK(TerrainOcclusion::isBehind(1400.0f, 1000.0f, 50.0f, tolerance), "and a shallower one gives less");

    // Cannot happen (farthest >= nearest), but must not eat the slack.
    TEST_CHECK(!TerrainOcclusion::isBehind(1005.0f, 1000.0f, -100.0f, tolerance), "a negative spread is clamped away");

    TEST_CHECK(!TerrainOcclusion::isBehind(1400.0f, 1000.0f, 0.0f, 1.5f), "a generous tolerance still applies");

    testSegmentBlocked();
}
