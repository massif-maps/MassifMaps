// Past a source's max zoom the decoder skips features by their bounds before any rule reads them
// (MBVTGeometryBounds.h); a bound that disagreed with getGeometry's clip would drop drawn features.

#include "TestCheck.h"

#include <mapnikvt/MBVTGeometryBounds.h>

#include <cstdint>
#include <vector>

namespace mvt = massif::mvt;

namespace {
    std::uint32_t command(int id, int count) { return static_cast<std::uint32_t>((count << 3) | id); }
    std::uint32_t zigzag(int v) { return static_cast<std::uint32_t>((v << 1) ^ (v >> 31)); }

    cglib::bbox2<float> bounds(const std::vector<std::uint32_t>& geometry, float scale = 1.0f / 4096) {
        return mvt::mbvtGeometryBounds(static_cast<int>(geometry.size()), [&geometry](int i) { return static_cast<int>(geometry[i]); }, scale);
    }

    // The overzoom transform for child (x, y) dz levels under the source, as VectorTileDecoder builds it.
    cglib::mat3x3<float> childTransform(int dz, int x, int y) {
        float s = static_cast<float>(1 << dz);
        return cglib::translate3_matrix(cglib::vec3<float>(-x, -y, 1)) * cglib::scale3_matrix(cglib::vec3<float>(s, s, 1));
    }

    const cglib::bbox2<float> clipBox(cglib::vec2<float>(-0.125f, -0.125f), cglib::vec2<float>(1.125f, 1.125f));
}

void testMBVTGeometryBounds() {
    cglib::bbox2<float> line = bounds({ command(1, 1), zigzag(100), zigzag(200), command(2, 2), zigzag(300), zigzag(-50), zigzag(-200), zigzag(400) });
    TEST_CHECK(line.min(0) == 100.0f / 4096 && line.max(0) == 400.0f / 4096, "a line's x runs over its deltas");
    TEST_CHECK(line.min(1) == 150.0f / 4096 && line.max(1) == 550.0f / 4096, "and its y too");

    TEST_CHECK(bounds({ command(1, 1), zigzag(10), zigzag(10), command(2, 2), zigzag(5), zigzag(0), zigzag(0), zigzag(5), command(7, 1) }).max(0) == 15.0f / 4096,
               "a ClosePath adds no vertex");
    TEST_CHECK(bounds({ command(1, 1), zigzag(10) }).min(0) > bounds({ command(1, 1), zigzag(10) }).max(0), "a truncated stream has no vertex");

    // A source at z14 seen from the z18 child (5, 9): it covers [5/16, 6/16] x [9/16, 10/16] of the source.
    cglib::mat3x3<float> transform = childTransform(4, 5, 9);
    cglib::bbox2<float> inside = bounds({ command(1, 1), zigzag(1400), zigzag(2400) });
    TEST_CHECK(mvt::mbvtBoundsMeetClip(inside, transform, clipBox), "a point inside the child is kept, though its box is degenerate");
    cglib::bbox2<float> far = bounds({ command(1, 1), zigzag(100), zigzag(100) });
    TEST_CHECK(!mvt::mbvtBoundsMeetClip(far, transform, clipBox), "a point across the source is skipped");
    cglib::bbox2<float> crossing = bounds({ command(1, 1), zigzag(0), zigzag(2400), command(2, 1), zigzag(4096), zigzag(0) });
    TEST_CHECK(mvt::mbvtBoundsMeetClip(crossing, transform, clipBox), "a line through the child is kept");
    cglib::bbox2<float> buffer = bounds({ command(1, 1), zigzag(1270), zigzag(2400) });
    TEST_CHECK(mvt::mbvtBoundsMeetClip(buffer, transform, clipBox), "a point in the clip buffer, just left of the child, is kept");
    TEST_CHECK(!mvt::mbvtBoundsMeetClip(bounds({}), transform, clipBox), "no vertex, nothing to keep");

    // A merged feature's parts, already in the child's frame.
    TEST_CHECK(mvt::mbvtPartMeetsClip({ cglib::vec2<float>(0.5f, 0.5f) }, clipBox), "a housenumber in the child is kept");
    TEST_CHECK(!mvt::mbvtPartMeetsClip({ cglib::vec2<float>(-4.0f, 3.0f) }, clipBox), "its sibling across the source is not");
    TEST_CHECK(mvt::mbvtPartMeetsClip({ cglib::vec2<float>(-2.0f, 0.5f), cglib::vec2<float>(3.0f, 0.5f) }, clipBox), "a path crossing the child is kept whole");
    TEST_CHECK(!mvt::mbvtPartMeetsClip({}, clipBox), "an empty part is not");
}
