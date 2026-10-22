// Which bitmaps of a layer share one draw (vt/RasterGroups.h). Normal maps are bound together so a
// shader can mix them, whatever their size; anything else keeps one draw per bitmap, as before.

#include <memory>
#include <vector>

#include "RasterGroups.h"

using namespace massif::vt;

#include "TestCheck.h"

namespace {
    std::shared_ptr<TileBitmap> bitmap(TileBitmap::Type type, int size) {
        return std::make_shared<TileBitmap>(type, TileBitmap::Format::RGBA, size, size, std::vector<std::uint8_t>());
    }
}

void testRasterGroups() {
    const TileBitmap::Type N = TileBitmap::Type::NORMALMAP;
    const TileBitmap::Type C = TileBitmap::Type::COLORMAP;

    TEST_CHECK(groupRasters({}).empty(), "no bitmaps, no draw");

    std::vector<RasterGroup> one = groupRasters({ bitmap(N, 256) });
    TEST_CHECK(one.size() == 1 && one[0].first == 0 && one[0].count == 1, "a lone bitmap draws alone");

    std::vector<RasterGroup> two = groupRasters({ bitmap(N, 256), bitmap(N, 256) });
    TEST_CHECK(two.size() == 1 && two[0].count == 2, "two normal maps share a draw");

    std::vector<RasterGroup> capped = groupRasters({ bitmap(N, 256), bitmap(N, 256), bitmap(N, 256), bitmap(N, 256), bitmap(N, 256) });
    TEST_CHECK(capped.size() == 2 && capped[0].count == MAX_RASTER_SLOTS && capped[1].first == MAX_RASTER_SLOTS && capped[1].count == 1, "a fifth raster starts a new draw");

    std::vector<RasterGroup> colors = groupRasters({ bitmap(C, 256), bitmap(C, 256) });
    TEST_CHECK(colors.size() == 2, "colour maps never share a draw");

    std::vector<RasterGroup> mixed = groupRasters({ bitmap(N, 256), bitmap(C, 256), bitmap(N, 256) });
    TEST_CHECK(mixed.size() == 3, "a colour map between two normal maps splits them");

    std::vector<RasterGroup> sizes = groupRasters({ bitmap(N, 256), bitmap(N, 256), bitmap(N, 1) });
    TEST_CHECK(sizes.size() == 1 && sizes[0].count == 3, "a 1x1 texel rides along with full tiles");

    TEST_CHECK(RASTER_SLOT_UNITS.size() + 1 == MAX_RASTER_SLOTS, "one unit per slot after the first");
    for (int unit : RASTER_SLOT_UNITS) {
        TEST_CHECK(unit != 0 && unit != 1 && unit != 5, "slots avoid the bitmap and terrain units");
        TEST_CHECK(unit < 8, "all of it fits the 8 units GLES2 guarantees");
    }
}
