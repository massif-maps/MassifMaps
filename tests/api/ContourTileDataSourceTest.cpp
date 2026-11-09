// ContourTileDataSource over a synthetic DEM served as raw Terrarium tiles: the contour tile bytes a fixture
// produces (seamless edges on and off), and how often the DEM source is read while a block of tiles is
// traced out of order on several threads. A 64 px tile and a 100 m interval keep it fast.

#include "core/BinaryData.h"
#include "core/MapTile.h"
#include "core/Variant.h"
#include "components/TerrainOptions.h"
#include "datasources/ContourTileDataSource.h"
#include "datasources/TileDataSource.h"
#include "datasources/components/TileData.h"

#include "TestCheck.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

using namespace massif;

// Label stubs are off here, so the source never asks for the elevation manager; TerrainOptions.cpp is the whole
// terrain option set and is not linked for the one getter.
namespace massif {
    std::shared_ptr<ElevationManager> TerrainOptions::getElevationManager() const { return std::shared_ptr<ElevationManager>(); }
}

namespace {
    constexpr int TEXELS = 64;
    constexpr int ZOOM = 12, BASE_X = 2100, BASE_Y = 1450;
    constexpr std::uint64_t GOLDEN_SEAMLESS = 9169288706907277867ULL, GOLDEN_PLAIN = 9882241752032338642ULL;

    class HillSource : public TileDataSource {
    public:
        HillSource() : TileDataSource(0, 14), _loads(0) {
            setMetaDataElement("dem_encoding", Variant(std::string("terrarium")));
        }

        std::shared_ptr<TileData> loadTile(const MapTile& tile) override {
            _loads++;
            std::this_thread::sleep_for(std::chrono::microseconds(300));
            std::vector<unsigned char> pixels(TEXELS * TEXELS * 4);
            for (int j = 0; j < TEXELS; j++) {
                for (int i = 0; i < TEXELS; i++) {
                    double gx = tile.getX() * TEXELS + i, gy = -tile.getY() * TEXELS + j;
                    double v = 32768.0 + 1500.0 + 900.0 * std::sin(gx * 0.05) + 700.0 * std::cos(gy * 0.04) + 0.3 * gx;
                    unsigned char r = static_cast<unsigned char>(std::floor(v / 256.0));
                    unsigned char g = static_cast<unsigned char>(std::floor(v - r * 256.0));
                    unsigned char b = static_cast<unsigned char>(std::floor((v - std::floor(v)) * 256.0));
                    unsigned char* p = &pixels[(static_cast<std::size_t>(j) * TEXELS + i) * 4];
                    p[0] = r;
                    p[1] = g;
                    p[2] = b;
                    p[3] = 255;
                }
            }
            return std::make_shared<TileData>(std::make_shared<BinaryData>(pixels.data(), pixels.size()), TEXELS, TEXELS);
        }

        int loads() const { return _loads; }

    private:
        std::atomic<int> _loads;
    };

    std::shared_ptr<ContourTileDataSource> makeContour(const std::shared_ptr<HillSource>& source, bool seamless) {
        auto contour = std::make_shared<ContourTileDataSource>(source);
        contour->setBaseInterval(100.0f);
        contour->setResolution(0);
        contour->setMinVisibleZoom(0);
        contour->setSeamlessEdgesEnabled(seamless);
        return contour;
    }

    std::uint64_t hashOf(const std::shared_ptr<TileData>& tile) {
        std::uint64_t hash = 1469598103934665603ULL;
        if (!tile || !tile->getData()) {
            return 0;
        }
        const unsigned char* bytes = tile->getData()->data();
        for (std::size_t i = 0; i < tile->getData()->size(); i++) {
            hash = (hash ^ bytes[i]) * 1099511628211ULL;
        }
        return hash;
    }

    void testGeometry() {
        auto source = std::make_shared<HillSource>();
        std::uint64_t seamless = 0, plain = 0;
        std::size_t seamlessBytes = 0;
        for (int dy = 0; dy < 2; dy++) {
            for (int dx = 0; dx < 2; dx++) {
                MapTile tile(BASE_X + dx, BASE_Y + dy, ZOOM, 0);
                std::shared_ptr<TileData> a = makeContour(source, true)->loadTile(tile);
                std::shared_ptr<TileData> b = makeContour(source, false)->loadTile(tile);
                seamlessBytes += (a && a->getData() ? a->getData()->size() : 0);
                seamless = seamless * 31 + hashOf(a);
                plain = plain * 31 + hashOf(b);
            }
        }
        std::printf("contour golden seamless=%llu plain=%llu bytes=%zu\n", (unsigned long long)seamless, (unsigned long long)plain, seamlessBytes);
        TEST_CHECK(seamlessBytes > 1000, "the fixture hills carry contour lines");
        TEST_CHECK(seamless == GOLDEN_SEAMLESS, "the contour tiles with seamless edges are byte for byte what they were");
        TEST_CHECK(plain == GOLDEN_PLAIN, "and without seamless edges");
    }

    void testDemReads() {
        constexpr int N = 6; // N x N tiles; their own and east/north/north-east DEMs make (N + 1)^2 distinct ones
        auto source = std::make_shared<HillSource>();
        auto contour = makeContour(source, true);

        std::vector<int> order;
        for (int i = 0; i < N * N; i++) {
            order.push_back((i * 17 + 5) % (N * N)); // 17 is coprime with 36: a fixed shuffle
        }
        std::atomic<int> next(0), produced(0);
        std::vector<std::thread> workers;
        for (int t = 0; t < 4; t++) {
            workers.emplace_back([&]() {
                for (int i = next++; i < N * N; i = next++) {
                    MapTile tile(BASE_X + order[i] % N, BASE_Y + order[i] / N, ZOOM, 0);
                    if (contour->loadTile(tile)) {
                        produced++;
                    }
                }
            });
        }
        for (std::thread& worker : workers) {
            worker.join();
        }
        TEST_CHECK(produced == N * N, "every tile of the block was traced");
        std::printf("dem reads %d\n", source->loads());
        TEST_CHECK(source->loads() == N * N + (N + 1) * (N + 1), "each DEM tile is read once for the bitmap plus once per tile that traces it");
    }
}

void testContourTileDataSource() {
    testGeometry();
    testDemReads();
}
