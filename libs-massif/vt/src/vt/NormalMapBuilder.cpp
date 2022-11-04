#include "NormalMapBuilder.h"

#include <cmath>
#include <algorithm>

#include <boost/math/constants/constants.hpp>

namespace carto::vt {
    NormalMapBuilder::NormalMapBuilder(const std::array<float, 4>& rgbaHeightScale, std::uint8_t alpha) : _rgbaHeightScale(rgbaHeightScale), _alpha(alpha) {
    }

    std::shared_ptr<const Bitmap> NormalMapBuilder::buildNormalMapFromHeightMap(const carto::vt::TileId& tileId, const std::shared_ptr<const Bitmap>& bitmap) const {
        return buildNormalMapFromHeightMap(tileId, tileId, bitmap);
    }

    std::shared_ptr<const Bitmap> NormalMapBuilder::buildNormalMapFromHeightMap(const TileId& subTileId, const TileId& tileId, const std::shared_ptr<const Bitmap>& bitmap) const {
        if (!bitmap) {
            return bitmap;
        }

        int width = bitmap->width;
        int height = bitmap->height;

        auto getInterpolatedHeight = [&](int x, int y) {
            int x0 = x, x1 = x;
            int y0 = y, y1 = y;
            if (x < 0) {
                x0 = 0;
                x1 = 1;
            }
            if (x >= width) {
                x0 = width - 1;
                x1 = width - 2;
            }
            if (y < 0) {
                y0 = 0;
                y1 = 1;
            }
            if (y >= height) {
                y0 = height - 1;
                y1 = height - 2;
            }

            if (x0 == x1 && y0 == y1) {
                return unpackHeight(bitmap->data[(y0 * width) + x0]);
            }
            return 2 * unpackHeight(bitmap->data[(y0 * width) + x0]) - unpackHeight(bitmap->data[(y1 * width) + x1]);
        };

        std::vector<std::uint32_t> data(width * height, 0);
        if (width >= 2 && height >= 2) {
            int delta = subTileId.zoom - tileId.zoom;
            int x0 = (subTileId.x - (tileId.x << delta)) * (width >> delta);
            int y0 = ((1 << delta) - 1 - subTileId.y + (tileId.y << delta)) * (height >> delta);
            int subWidth = (width >> delta) + 1;
            int subHeight = (height >> delta) + 1;

            std::vector<cglib::vec3<float>> buffer(subWidth * subHeight, cglib::vec3<float>(0.0f, 0.0f, 0.0f));

            float heights[3][3];
            for (int y = 0; y < subHeight; y++) {
                double y1 = boost::math::constants::pi<double>() * ((tileId.y + (height - y0 - y - 0.5) / height) / (1 << tileId.zoom) - 0.5);
                double rz = std::tanh(y1);
                double ss = std::sqrt(std::max(0.0, 1.0 - rz * rz));

                for (int dy = 0; dy < 3; dy++) {
                    heights[dy][1] = getInterpolatedHeight(x0 - 1, y0 + y + dy - 1);
                    heights[dy][2] = getInterpolatedHeight(x0 - 0, y0 + y + dy - 1);
                }
                for (int x = 0; x < subWidth; x++) {
                    for (int dy = 0; dy < 3; dy++) {
                        heights[dy][0] = heights[dy][1];
                        heights[dy][1] = heights[dy][2];
                        heights[dy][2] = getInterpolatedHeight(x0 + x + 1, y0 + y + dy - 1);
                    }

                    float dx = (heights[0][2] + 2 * heights[1][2] + heights[2][2]) - (heights[0][0] + 2 * heights[1][0] + heights[2][0]);
                    float dy = (heights[2][0] + 2 * heights[2][1] + heights[2][2]) - (heights[0][0] + 2 * heights[0][1] + heights[0][2]);
                    float dz = 8.0f * static_cast<float>(ss);

                    buffer[y * subWidth + x] = cglib::vec3<float>(dx, dy, dz);
                }
            }

            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    cglib::vec3<float> normal(0.0f, 0.0f, 0.0f);
                    if (delta == 0) {
                        normal = buffer[y * subWidth + x];
                    } else {
                        int sx = x >> delta;
                        int sy = y >> delta;
                        float wx = (x - (sx << delta) + 0.5f) / (1 << delta);
                        float wy = (y - (sy << delta) + 0.5f) / (1 << delta);
                        for (int dy = 0; dy < 2; dy++) {
                            wy = 1.0f - wy;
                            for (int dx = 0; dx < 2; dx++) {
                                wx = 1.0f - wx;
                                if (sx + dx < subWidth && sy + dy < subHeight) {
                                    normal += buffer[(sy + dy) * subWidth + sx + dx] * wx * wy;
                                }
                            }
                        }
                    }

                    data[y * width + x] = packNormal(normal);
                }
            }
        }
        return std::make_shared<Bitmap>(width, height, std::move(data));
    }

    float NormalMapBuilder::unpackHeight(std::uint32_t color) const {
        union {
            std::uint32_t u32;
            std::uint8_t u8[sizeof(std::uint32_t)];
        } packedColor;
        packedColor.u32 = color;
        float height = packedColor.u8[0] * _rgbaHeightScale[0];
        height += packedColor.u8[1] * _rgbaHeightScale[1];
        height += packedColor.u8[2] * _rgbaHeightScale[2];
        height += packedColor.u8[3] * _rgbaHeightScale[3];
        return height;
    }

    std::uint32_t NormalMapBuilder::packNormal(cglib::vec3<float> normal) const {
        union {
            std::uint32_t u32;
            std::uint8_t u8[sizeof(std::uint32_t)];
        } packedNormal;
        normal = cglib::unit(normal);
        packedNormal.u8[0] = static_cast<std::uint8_t>((normal(0) + 1.0f) * 127.5f);
        packedNormal.u8[1] = static_cast<std::uint8_t>((normal(1) + 1.0f) * 127.5f);
        packedNormal.u8[2] = static_cast<std::uint8_t>((normal(2) + 1.0f) * 127.5f);
        packedNormal.u8[3] = _alpha;
        return packedNormal.u32;
    }
}
