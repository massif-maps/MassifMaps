// The hillshade normal map at heightScale 1 carries MapLibre's slope (hillshade_prepare.fragment.glsl,
// current gl-js): what lets a style's hillshade-exaggeration become the layer's contrast one for one.

#include "NormalMapBuilder.h"
#include "Bitmap.h"

#include <cmath>

using namespace massif::vt;

#include "TestCheck.h"

namespace {
    const double EARTH_CIRCUMFERENCE = 40075016.6855785;

    // A Terrarium plane: metres = R * 256 + G + B / 256 - 32768.
    std::shared_ptr<Bitmap> terrariumPlane(int size, double metresPerPixel, double slopeX, double slopeY) {
        std::vector<std::uint32_t> pixels(size * size);
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                double value = 1000.0 + (slopeX * x + slopeY * y) * metresPerPixel + 32768.0;
                std::uint32_t r = static_cast<std::uint32_t>(std::floor(value / 256.0));
                std::uint32_t g = static_cast<std::uint32_t>(std::floor(value)) % 256;
                std::uint32_t b = static_cast<std::uint32_t>(std::floor((value - std::floor(value)) * 256.0));
                pixels[y * size + x] = r | (g << 8) | (b << 16) | (255u << 24);
            }
        }
        return std::make_shared<Bitmap>(size, size, std::move(pixels));
    }

    // (dh/dx, dh/dy) as the normal-map lighting shader reads it back.
    std::pair<double, double> decodedDeriv(std::uint32_t packed) {
        double nx = (packed & 255) / 127.5 - 1.0;
        double ny = ((packed >> 8) & 255) / 127.5 - 1.0;
        double nz = ((packed >> 16) & 255) / 127.5 - 1.0;
        return { nx / nz, ny / nz };
    }

    // HillshadeRasterTileLayer::createVectorTile's scale for a Terrarium tile at heightScale 1.
    std::array<float, 4> layerScales(int size, int zoom) {
        float scale = (1.0f / 256.0f) * static_cast<float>(size * std::pow(2.0, zoom) / EARTH_CIRCUMFERENCE);
        scale *= static_cast<float>(std::pow(2.0, (15.0 - zoom) * 0.3));
        return { { 65536.0f * scale, 256.0f * scale, scale, 0.0f } };
    }
}

void testNormalMapSlope() {
    const int size = 64;
    const int zoom = 13;
    const TileId tileId(zoom, 4270, 2914);
    const double metresPerPixel = EARTH_CIRCUMFERENCE / (size * std::pow(2.0, zoom));

    // MapLibre: sobel * tileSize / 2^(28.2562 - zoom + exaggeration), then / cos(lat) in the render
    // pass; for a plane that is the slope times the below-z15 boost over cos(lat).
    const double boost = std::pow(2.0, (15.0 - zoom) * 0.3);
    const int row = size / 2;
    double mercatorY = M_PI * (1.0 - 2.0 * (tileId.y + (row + 0.5) / size) / (1 << zoom));
    double cosLat = 1.0 / std::cosh(mercatorY);

    NormalMapBuilder builder(layerScales(size, zoom), 128);
    for (double slope : { 0.1, 0.3 }) {
        double expected = slope * boost / cosLat;

        auto alongX = builder.buildNormalMapFromHeightMap(tileId, terrariumPlane(size, metresPerPixel, slope, 0.0));
        auto derivX = decodedDeriv(alongX->data[row * size + size / 2]);
        TEST_CHECK(std::abs(std::abs(derivX.first) - expected) < 0.03 * expected, "an east-west plane has MapLibre's slope");
        TEST_CHECK(std::abs(derivX.second) < 0.01, "and none across it");

        auto alongY = builder.buildNormalMapFromHeightMap(tileId, terrariumPlane(size, metresPerPixel, 0.0, slope));
        auto derivY = decodedDeriv(alongY->data[row * size + size / 2]);
        TEST_CHECK(std::abs(std::abs(derivY.second) - expected) < 0.03 * expected, "a north-south plane has MapLibre's slope");
        TEST_CHECK(std::abs(derivY.first) < 0.01, "and none across it");
    }
}
