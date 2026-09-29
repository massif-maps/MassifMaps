/*
 * The shared terrain grid (TileSurfaceBuilder::buildRegularGridSurface) is emitted in bands of cells
 * rather than whole rows, so a vertex is shaded about once instead of twice, and in contiguous blocks
 * a draw can skip when off screen. Checked here: the same triangles with the same winding, the cache
 * miss ratio a small FIFO vertex cache sees, and the block layout. The frame rate it buys is a device
 * measurement (docs/internals/performance-log.md, section 32).
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

    void testCullBlocksAreContiguous() {
        // 30 does not divide by the block count: the blocks must still tile the grid, each a contiguous
        // run of indices touching only its own cells, as visibleGridIndexRuns assumes.
        const int res = 30;
        auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
        std::shared_ptr<TileSurface> surface = TileSurfaceBuilder(transformer).buildRegularGridSurface(res);
        if (!surface) {
            TEST_CHECK(false, "the grid builds");
            return;
        }
        const VertexArray<std::uint16_t>& indices = surface->getIndices();
        const int blocks = TileSurfaceBuilder::GRID_CULL_BLOCKS;
        int stride = res + 1;
        std::size_t first = 0;
        bool inside = true;
        for (int blockY = 0; blockY < blocks; blockY++) {
            int y0 = TileSurfaceBuilder::gridBlockStart(res, blockY), y1 = TileSurfaceBuilder::gridBlockStart(res, blockY + 1);
            for (int blockX = 0; blockX < blocks; blockX++) {
                int x0 = TileSurfaceBuilder::gridBlockStart(res, blockX), x1 = TileSurfaceBuilder::gridBlockStart(res, blockX + 1);
                std::size_t count = static_cast<std::size_t>((x1 - x0) * (y1 - y0) * 6);
                for (std::size_t i = first; i < first + count && i < indices.size(); i++) {
                    int x = indices[i] % stride, y = indices[i] / stride;
                    inside = inside && x >= x0 && x <= x1 && y >= y0 && y <= y1;
                }
                first += count;
            }
        }
        TEST_CHECK(TileSurfaceBuilder::gridBlockStart(res, 0) == 0 && TileSurfaceBuilder::gridBlockStart(res, blocks) == res, "the blocks span the grid edge to edge");
        TEST_CHECK(first == indices.size(), "the blocks together hold every index");
        TEST_CHECK(inside, "each block's run touches only the vertices of its own cells");
    }

}

void testGridIndexOrder() {
    testBandedGrid();
    testCullBlocksAreContiguous();
}
