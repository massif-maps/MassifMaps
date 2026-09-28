/*
 * The shared terrain grid (TileSurfaceBuilder::buildRegularGridSurface) is emitted in bands of cells
 * rather than whole rows, so a vertex is shaded about once instead of twice. Checked here: the same
 * triangles with the same winding, and the cache miss ratio a small FIFO vertex cache sees. The frame
 * rate it buys is a device measurement (docs/internals/performance-log.md, section 32).
 */

#include "vt/TileSurface.h"
#include "vt/TileSurfaceBuilder.h"
#include "vt/TileTransformer.h"

#include <algorithm>
#include <array>
#include <deque>
#include <memory>
#include <set>
#include <vector>

using namespace massif::vt;

#include "TestCheck.h"

namespace {

    double missesPerTriangle(const VertexArray<std::uint16_t>& indices, std::size_t cacheSize) {
        std::deque<std::uint16_t> cache;
        std::size_t misses = 0;
        for (std::size_t i = 0; i < indices.size(); i++) {
            if (std::find(cache.begin(), cache.end(), indices[i]) != cache.end()) {
                continue;
            }
            misses++;
            cache.push_back(indices[i]);
            if (cache.size() > cacheSize) {
                cache.pop_front();
            }
        }
        return static_cast<double>(misses) / (indices.size() / 3);
    }

    void testBandedGrid() {
        const int res = 128;
        auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
        std::shared_ptr<TileSurface> surface = TileSurfaceBuilder(transformer).buildRegularGridSurface(res);
        TEST_CHECK(surface && surface->getIndices().size() == static_cast<std::size_t>(res) * res * 6, "one quad of two triangles per cell");
        if (!surface) {
            return;
        }
        const VertexArray<std::uint16_t>& indices = surface->getIndices();

        std::set<std::array<int, 3>> expected, built;
        int stride = res + 1;
        for (int j = 0; j < res; j++) {
            for (int i = 0; i < res; i++) {
                int a = j * stride + i;
                expected.insert({ a, a + 1, a + stride + 1 });
                expected.insert({ a, a + stride + 1, a + stride });
            }
        }
        for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
            built.insert({ indices[t], indices[t + 1], indices[t + 2] });
        }
        TEST_CHECK(built == expected, "the same triangles, in the same winding, as row order");
        TEST_CHECK(missesPerTriangle(indices, 16) < 0.65, "a 16-entry FIFO cache shades each vertex about once (row order: ~1.0 misses per triangle)");
    }

}

void testGridIndexOrder() {
    testBandedGrid();
}
