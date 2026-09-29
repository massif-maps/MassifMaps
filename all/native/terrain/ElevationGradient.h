/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ELEVATIONGRADIENT_H_
#define _MASSIF_ELEVATIONGRADIENT_H_

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace massif {

    /**
     * Per texel, r = h(x + 1, y) - h(x, y) and g = h(x, y + 1) - h(x, y) in metres as half floats: the lit surfaces'
     * normal in two fetches (docs/internals/rendering/08-lighting-sky-fog.md#the-terrain-normal-is-two-fetches).
     */
    namespace ElevationGradient {

        inline std::uint16_t toHalf(float value) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            std::uint32_t sign = (bits >> 16) & 0x8000u;
            std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xffu) - 127 + 15;
            std::uint32_t mantissa = bits & 0x7fffffu;
            if (exponent >= 31) {
                return static_cast<std::uint16_t>(sign | 0x7bffu); // clamped to the largest finite half
            }
            if (exponent <= 0) {
                if (exponent < -10) {
                    return static_cast<std::uint16_t>(sign);
                }
                mantissa |= 0x800000u;
                std::uint32_t shift = static_cast<std::uint32_t>(14 - exponent);
                std::uint32_t half = mantissa >> shift;
                half += (mantissa >> (shift - 1)) & 1u; // round to nearest
                return static_cast<std::uint16_t>(sign | half);
            }
            std::uint32_t half = (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13);
            half += (mantissa >> 12) & 1u; // round to nearest; a carry into the exponent is still correct
            return static_cast<std::uint16_t>(sign | std::min<std::uint32_t>(half, 0x7bffu));
        }

        inline float fromHalf(std::uint16_t half) {
            std::uint32_t sign = (static_cast<std::uint32_t>(half) & 0x8000u) << 16;
            std::uint32_t exponent = (half >> 10) & 0x1fu;
            std::uint32_t mantissa = half & 0x3ffu;
            std::uint32_t bits = 0;
            if (exponent == 0) {
                float value = static_cast<float>(mantissa) / 16777216.0f; // 2^-24
                return sign ? -value : value;
            }
            bits = sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
            float value = 0;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        /**
         * Writes the texels of the rect [x0, x0 + rectWidth) x [y0, y0 + rectHeight), rows packed, two halves
         * each. heightAt(x, y) is the height in metres of a texel of the width x height raster.
         */
        template <typename HeightAt>
        void encode(const HeightAt& heightAt, int width, int height, int x0, int y0, int rectWidth, int rectHeight, std::vector<std::uint16_t>& gradient) {
            gradient.resize(static_cast<std::size_t>(rectWidth) * rectHeight * 2);
            std::size_t index = 0;
            for (int y = y0; y < y0 + rectHeight; y++) {
                for (int x = x0; x < x0 + rectWidth; x++) {
                    float h = heightAt(x, y);
                    gradient[index++] = toHalf(x + 1 < width ? heightAt(x + 1, y) - h : 0.0f);
                    gradient[index++] = toHalf(y + 1 < height ? heightAt(x, y + 1) - h : 0.0f);
                }
            }
        }
    }

}

#endif
