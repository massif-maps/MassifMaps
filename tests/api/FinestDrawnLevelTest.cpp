/*
 * Tests for the level a label reads its ground at (all/native/renderers/utils/FinestDrawnLevel.h).
 *
 * At the terrain-3d example's camera (Matterhorn, zoom 11.6) labels read the z10 grid of the camera
 * zoom while the summit was drawn from z12: the POI stood at 4162 m under a summit drawn at 4422 m.
 * A label now reads the finest texture drawn over it in the last frame.
 *
 * NOT covered here: ElevationTextureCache stamping an entry as it resolves a drawn tile, and labels
 * re-anchoring when a finer texture lands. Both need ElevationManager and GL (see ../README.md).
 */

#include "renderers/utils/FinestDrawnLevel.h"

#include <map>

using namespace massif;

#include "TestCheck.h"

namespace {

    // Grid zoom -> the LRU stamp of the texture holding the point.
    int pick(const std::map<int, std::uint64_t>& held, int finest, int coarsest, std::uint64_t since) {
        return finestDrawnLevel(finest, coarsest, since, [&held](int zoom, std::uint64_t& stamp) {
            auto it = held.find(zoom);
            if (it == held.end()) {
                return false;
            }
            stamp = it->second;
            return true;
        });
    }

}

void testFinestDrawnLevel() {
    // The measured case: z10, z11 and z12 all drawn last frame, the camera zoom asking for z10.
    TEST_CHECK(pick({ { 10, 50 }, { 11, 51 }, { 12, 52 } }, 16, 6, 40) == 12, "the finest drawn level wins over the camera zoom's");
    TEST_CHECK(pick({ { 10, 50 }, { 14, 30 } }, 16, 6, 40) == 10, "a finer texture from a previous camera is not what is drawn");
    TEST_CHECK(pick({ { 5, 50 } }, 16, 6, 40) == -1, "no coarser than the bound, as the camera-zoom lookup");
    TEST_CHECK(pick({ { 6, 50 } }, 16, 6, 40) == 6, "the bound itself is allowed");
    TEST_CHECK(pick({ { 12, 40 } }, 16, 6, 40) == -1, "a stamp at the frame start was drawn in an earlier frame");
    TEST_CHECK(pick({}, 16, 6, 40) == -1, "nothing held");
    TEST_CHECK(pick({ { 0, 50 } }, 3, -2, 40) == 0, "a negative bound stops at level 0");
}
