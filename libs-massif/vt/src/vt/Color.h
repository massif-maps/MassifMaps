/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_COLOR_H_
#define _CARTO_VT_COLOR_H_

#include <cstdint>
#include <array>
#include <algorithm>
#include <functional>

namespace carto::vt {
    class Color final {
    public:
        constexpr Color() : _components{ { 0, 0, 0, 0 } } { }

        constexpr explicit Color(const std::array<float, 4>& rgba) : _components(rgba) { }
        
        constexpr explicit Color(float r, float g, float b, float a) : _components{ { r, g, b, a } } { }

        constexpr float& operator [] (std::size_t i) {
            return _components[i];
        }

        constexpr float operator [] (std::size_t i) const {
            return _components[i];
        }

        constexpr unsigned int value() const {
            std::array<std::uint8_t, 4> components = rgba8();
            unsigned int val = components[3];
            val = (val << 8) | components[0];
            val = (val << 8) | components[1];
            val = (val << 8) | components[2];
            return val;
        }

        constexpr std::array<float, 4> rgba() const {
            return _components;
        }

        constexpr std::array<std::uint8_t, 4> rgba8() const {
            std::array<std::uint8_t, 4> components8 {};
            for (std::size_t i = 0; i < 4; i++) {
                float c = std::max(0.0f, std::min(1.0f, _components[i]));
                components8[i] = static_cast<std::uint8_t>(c * 255.0f + 0.5f);
            }
            return components8;
        }

        static constexpr Color fromValue(unsigned int value) {
            return Color(
                ((value >> 16) & 255) * (1.0f / 255.0f),
                ((value >> 8)  & 255) * (1.0f / 255.0f),
                ((value >> 0)  & 255) * (1.0f / 255.0f),
                ((value >> 24) & 255) * (1.0f / 255.0f)
            );
        }

        static constexpr Color fromColorOpacity(const Color& baseColor, float opacity) {
            Color color = baseColor;
            if (opacity < 1.0f) {
                for (std::size_t i = 0; i < 4; i++) {
                    color._components[i] *= std::max(opacity, 0.0f);
                }
            }
            return color;
        }

    private:
        std::array<float, 4> _components; // rgba
    };

    constexpr Color operator + (const Color& color1, const Color& color2) {
        return Color(color1[0] + color2[0], color1[1] + color2[1], color1[2] + color2[2], color1[3] + color2[3]);
    }

    constexpr Color operator - (const Color& color1, const Color& color2) {
        return Color(color1[0] - color2[0], color1[1] - color2[1], color1[2] - color2[2], color1[3] - color2[3]);
    }

    constexpr Color operator * (const Color& color1, const Color& color2) {
        return Color(color1[0] * color2[0], color1[1] * color2[1], color1[2] * color2[2], color1[3] * color2[3]);
    }

    constexpr Color operator * (const Color& color1, float c2) {
        return Color(color1[0] * c2, color1[1] * c2, color1[2] * c2, color1[3] * c2);
    }

    constexpr Color operator * (float c1, const Color& color2) {
        return Color(c1 * color2[0], c1 * color2[1], c1 * color2[2], c1 * color2[3]);
    }

    constexpr bool operator == (const Color& color1, const Color& color2) {
        return color1[0] == color2[0] && color1[1] == color2[1] && color1[2] == color2[2] && color1[3] == color2[3];
    }
    
    constexpr bool operator != (const Color& color1, const Color& color2) {
        return !(color1 == color2);
    }
}

#endif
