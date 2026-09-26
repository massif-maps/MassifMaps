// Whether a label anchor is behind the terrain (terrain/TerrainOcclusion.h). A small height error
// becomes dz / sin(angle) of depth at a grazing view, so the slack scales with the sampled depth spread.

#include "terrain/TerrainOcclusion.h"

#include "TestCheck.h"

using namespace massif;

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
}
