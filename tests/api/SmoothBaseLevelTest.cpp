/*
 * Tests for the level a building's smoothed base reads its ground at (all/native/terrain/SmoothBaseLevel.h).
 *
 * On the first 2D/3D switch the only cached grid over Zermatt was a z5 ancestor, ~1.7 km per texel: the
 * valley averaged with its peaks, every base stood 550 m up and the walls stretched from the ground to
 * it. The base now waits for the level it wants unless the answer is at most maxAncestorLevels coarser.
 *
 * NOT covered here: ElevationTextureCache::getDisplayHeight taking an ALIAS (a level the source answered
 * with an ancestor) as final through LoadMode::CACHED_EXACT, and the request that loads the wanted tile.
 * Both need ElevationManager, which needs the data source and the grid cache (see ../README.md).
 */

#include "terrain/SmoothBaseLevel.h"

using namespace massif;

#include "TestCheck.h"

namespace {

    // A 512-texel grid's posting at Zermatt's latitude, in metres.
    double posting(int zoom) {
        return 40075016.686 * std::cos(46.02 * 3.14159265358979323846 / 180.0) / (1 << zoom) / 512;
    }

}

void testSmoothBaseLevel() {
    const double target = 50.0;
    TEST_CHECK(smoothBaseLevelOffset(posting(12), target) == 2, "a z12 grid (13 m) is two levels finer than a 50 m base wants");
    TEST_CHECK(smoothBaseLevelOffset(posting(10), target) == 0, "the z10 grid (53 m) is the level a 50 m base reads");
    TEST_CHECK(smoothBaseLevelOffset(posting(5), target) == -5, "a z5 grid (1.7 km) is five levels too coarse");
    TEST_CHECK(smoothBaseLevelOffset(0.0, target) == 0, "an empty grid moves no level");

    // Zermatt at z17, x 68356 y 46619: the measured case.
    MapTile wanted = smoothBaseWantedTile(68356, 46619, 17, 5, smoothBaseLevelOffset(posting(5), target), 1);
    TEST_CHECK(wanted.getZoom() == 10, "a z5 answer sends the base to wait for z10");
    TEST_CHECK(wanted.getX() == (68356 >> 7) && wanted.getY() == (46619 >> 7), "the wanted tile is the one holding the point");

    TEST_CHECK(smoothBaseWantedTile(68356, 46619, 17, 10, 0, 1).getZoom() == -1, "the wanted level stands");
    TEST_CHECK(smoothBaseWantedTile(68356, 46619, 17, 9, -1, 1).getZoom() == -1, "one level coarser stands, as the unsmoothed base allows");
    TEST_CHECK(smoothBaseWantedTile(68356, 46619, 17, 8, -2, 1).getZoom() == 10, "two levels coarser waits");
    TEST_CHECK(smoothBaseWantedTile(68356, 46619, 17, 12, 2, 1).getZoom() == -1, "a finer grid is never refused");

    // A coarse source tile asks for no level finer than itself.
    MapTile clamped = smoothBaseWantedTile(5, 3, 8, 2, -9, 1);
    TEST_CHECK(clamped.getZoom() == 8 && clamped.getX() == 5 && clamped.getY() == 3, "the wanted level is clamped to the source tile's zoom");
}
