// The glue under terrain/SunExposure.h: which tiles a route needs, loading them, and the result array
// (terrain/SunExposureJob.h), over a synthetic DEM served as raw Terrarium tiles. A 1500 m wall runs
// north-south 4 km east of the route, so an eastern sun at 10 degrees is hidden and at 30 is not.

#include "terrain/SunExposure.h"
#include "terrain/SunExposureJob.h"

#include "components/SunExposureOptions.h"
#include "core/BinaryData.h"
#include "core/Variant.h"
#include "datasources/TileDataSource.h"
#include "datasources/components/TileData.h"
#include "graphics/Bitmap.h"
#include "projections/EPSG3857.h"
#include "terrain/ElevationManager.h"
#include "rastertiles/ElevationDecoder.h"

#include "TestCheck.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

using namespace massif;

// Bitmap.cpp needs libpng, libjpeg and libwebp; a raw RGBA tile is all this test hands the manager, so the
// members it reaches are stood in here instead of linking three image libraries.
namespace massif {
    Bitmap::Bitmap(const std::shared_ptr<BinaryData>& pixelData, unsigned int width, unsigned int height, ColorFormat::ColorFormat colorFormat, int bytesPerRow) :
        _width(0), _height(0), _bytesPerPixel(0), _colorFormat(ColorFormat::COLOR_FORMAT_UNSUPPORTED), _pixelData() {
        loadFromUncompressedBytes(pixelData->data(), width, height, colorFormat, bytesPerRow);
    }
    Bitmap::~Bitmap() { }
    bool Bitmap::loadFromUncompressedBytes(const unsigned char* pixelData, unsigned int width, unsigned int height, ColorFormat::ColorFormat colorFormat, int) {
        _colorFormat = colorFormat;
        _bytesPerPixel = 4;
        _width = width;
        _height = height;
        _pixelData.assign(pixelData, pixelData + width * height * 4);
        return true;
    }
    unsigned int Bitmap::getWidth() const { return _width; }
    unsigned int Bitmap::getHeight() const { return _height; }
    ColorFormat::ColorFormat Bitmap::getColorFormat() const { return _colorFormat; }
    unsigned int Bitmap::getBytesPerPixel() const { return _bytesPerPixel; }
    const std::vector<unsigned char>& Bitmap::getPixelData() const { return _pixelData; }
    std::shared_ptr<Bitmap> Bitmap::CreateFromCompressed(const std::shared_ptr<BinaryData>&) { return std::shared_ptr<Bitmap>(); }
}

namespace {
    constexpr double ROUTE_LON = 7.0, ROUTE_LAT = 46.0;
    constexpr double WALL_FROM = 4000, WALL_TO = 4600, WALL_HEIGHT = 1500;
    constexpr int TEXELS = 512;

    class WallSource : public TileDataSource {
    public:
        WallSource(int delayMs = 0) : TileDataSource(0, 14), _delayMs(delayMs), _loads(0) {
            setMetaDataElement("dem_encoding", Variant(std::string("terrarium")));
        }

        std::shared_ptr<TileData> loadTile(const MapTile& tile) override {
            _loads++;
            if (_delayMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(_delayMs));
            }
            double mercatorWidth = 40075016.68558;
            double eastOrigin = (ROUTE_LON / 360.0) * mercatorWidth;
            double cosLat = std::cos(ROUTE_LAT * 3.14159265358979323846 / 180.0);
            double n = static_cast<double>(1 << tile.getZoom());
            auto pixels = std::make_shared<std::vector<unsigned char> >(TEXELS * TEXELS * 4);
            for (int i = 0; i < TEXELS; i++) {
                double u = (tile.getX() + (i + 0.5) / TEXELS) / n;
                double groundEast = ((u - 0.5) * mercatorWidth - eastOrigin) * cosLat;
                double h = (groundEast >= WALL_FROM && groundEast < WALL_TO) ? WALL_HEIGHT : 0.0;
                double v = h + 32768.0;
                unsigned char r = static_cast<unsigned char>(std::floor(v / 256.0));
                unsigned char g = static_cast<unsigned char>(std::floor(v - r * 256.0));
                unsigned char b = static_cast<unsigned char>(std::floor((v - std::floor(v)) * 256.0));
                for (int j = 0; j < TEXELS; j++) {
                    unsigned char* p = &(*pixels)[(static_cast<std::size_t>(j) * TEXELS + i) * 4];
                    p[0] = r;
                    p[1] = g;
                    p[2] = b;
                    p[3] = 255;
                }
            }
            return std::make_shared<TileData>(std::make_shared<BinaryData>(pixels->data(), pixels->size()), TEXELS, TEXELS);
        }

        int loads() const { return _loads; }

    private:
        int _delayMs;
        std::atomic<int> _loads;
    };

    std::vector<MapPos> route(int count) {
        std::vector<MapPos> poses;
        for (int i = 0; i < count; i++) {
            poses.push_back(MapPos(ROUTE_LON, ROUTE_LAT + i * 100.0 / 111320.0)); // north, 100 m apart
        }
        return poses;
    }

    std::vector<double> sun(int count, double azimuth, double altitude) {
        std::vector<double> directions;
        for (int i = 0; i < count; i++) {
            directions.push_back(azimuth);
            directions.push_back(altitude);
        }
        return directions;
    }

    double fractionAt(const std::vector<double>& result, int i) { return result[SunExposureJob::HEADER_SIZE + i * SunExposureJob::POINT_STRIDE]; }
    int statusAt(const std::vector<double>& result, int i) { return static_cast<int>(result[SunExposureJob::HEADER_SIZE + i * SunExposureJob::POINT_STRIDE + 3]); }

    void testRoute() {
        auto source = std::make_shared<WallSource>();
        ElevationManager manager(source, std::shared_ptr<ElevationDecoder>());
        auto options = std::make_shared<SunExposureOptions>();
        options->setEyeHeight(0);

        std::vector<double> low = SunExposureJob::compute(manager, route(5), sun(5, 90, 10), options);
        TEST_CHECK(low.size() == SunExposureJob::HEADER_SIZE + 5 * SunExposureJob::POINT_STRIDE, "a header and four numbers per position");
        TEST_CHECK(low[0] == 1 && low[1] == 5 && low[2] == 4, "version 1, the count and the stride");
        TEST_CHECK(low[3] > 0 && low[4] == 0, "tiles were loaded and none is missing");
        TEST_CHECK(low[5] >= 0 && low[6] >= low[5], "the zoom range of what answered");
        bool allHidden = true, allClean = true;
        for (int i = 0; i < 5; i++) {
            allHidden = allHidden && fractionAt(low, i) == 0.0;
            allClean = allClean && statusAt(low, i) == 0;
        }
        TEST_CHECK(allHidden, "a 10 degree sun behind a 1500 m wall 4 km away is hidden along the route");
        TEST_CHECK(allClean, "with nothing flagged");
        double shadowDistance = low[SunExposureJob::HEADER_SIZE + 2];
        TEST_CHECK(shadowDistance > 3900 && shadowDistance < 4700, "and the shadow starts at the wall");

        std::vector<double> high = SunExposureJob::compute(manager, route(5), sun(5, 90, 30), options);
        TEST_CHECK(fractionAt(high, 0) == 1.0 && fractionAt(high, 4) == 1.0, "a 30 degree sun clears it");
        std::vector<double> away = SunExposureJob::compute(manager, route(5), sun(5, 270, 10), options);
        TEST_CHECK(fractionAt(away, 0) == 1.0, "and a western one never meets it");
        std::vector<double> night = SunExposureJob::compute(manager, route(2), sun(2, 90, -10), options);
        TEST_CHECK((statusAt(night, 0) & SunExposure::DARK) != 0 && fractionAt(night, 0) == 0.0, "a night sun is DARK");
        int loadsBefore = source->loads();
        SunExposureJob::compute(manager, route(5), sun(5, 90, 10), options);
        TEST_CHECK(source->loads() == loadsBefore, "a second call over the same ground loads nothing");

        bool threw = false;
        try {
            SunExposureJob::compute(manager, route(3), sun(2, 90, 10), options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST_CHECK(threw, "a sun direction short of a position is refused");
    }

    void testMissingData() {
        auto slow = std::make_shared<WallSource>(300);
        ElevationManager manager(slow, std::shared_ptr<ElevationDecoder>());
        auto options = std::make_shared<SunExposureOptions>();
        options->setLoadTimeoutMs(50);
        std::vector<double> result = SunExposureJob::compute(manager, route(3), sun(3, 90, 10), options);
        TEST_CHECK((static_cast<int>(result[7]) & SunExposureJob::FLAG_DEADLINE_HIT) != 0, "past the load deadline the call says so");
        TEST_CHECK(result[4] > 0, "and counts the tiles it lacked");
        TEST_CHECK(result.size() == SunExposureJob::HEADER_SIZE + 3 * SunExposureJob::POINT_STRIDE, "but still answers for every position");

        auto source = std::make_shared<WallSource>();
        ElevationManager cold(source, std::shared_ptr<ElevationDecoder>());
        auto cached = std::make_shared<SunExposureOptions>();
        cached->setLoadMissingTiles(false);
        std::vector<double> first = SunExposureJob::compute(cold, route(3), sun(3, 90, 10), cached);
        TEST_CHECK(fractionAt(first, 0) == -1.0 && (statusAt(first, 0) & SunExposure::NO_DEM) != 0, "with nothing decoded and no waiting, a point is unknown, not guessed");
        bool answered = false;
        for (int attempt = 0; attempt < 100 && !answered; attempt++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::vector<double> later = SunExposureJob::compute(cold, route(3), sun(3, 90, 10), cached);
            answered = fractionAt(later, 0) == 0.0;
        }
        TEST_CHECK(answered, "the tiles it asked for arrive in the background and the next call answers");

        auto tight = std::make_shared<SunExposureOptions>();
        tight->setCacheMB(1);
        auto big = std::make_shared<WallSource>();
        ElevationManager small(big, std::shared_ptr<ElevationDecoder>());
        std::vector<double> budget = SunExposureJob::compute(small, route(3), sun(3, 90, 10), tight);
        TEST_CHECK(budget[4] > 0, "a memory budget under the working set leaves tiles unloaded and counted");
    }
}

void testSunExposureJob() {
    testRoute();
    testMissingData();
}
