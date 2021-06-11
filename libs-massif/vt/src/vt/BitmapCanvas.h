/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_BITMAPCANVAS_H_
#define _CARTO_VT_BITMAPCANVAS_H_

#include "Color.h"
#include "Bitmap.h"

#include <cstdint>
#include <vector>
#include <memory>

#include <cglib/vec.h>
#include <cglib/mat.h>

namespace carto { namespace vt {
    class BitmapCanvas final {
    public:
        explicit BitmapCanvas(int width, int height, bool sdfMode);

        void setOpacity(float opacity);
        void setColor(const Color& color);
        void setTransform(const cglib::mat3x3<float>& transform);

        void drawRectangle(const cglib::vec2<float>& p0, const cglib::vec2<float>& p1);
        void drawTriangle(const cglib::vec2<float>& p0, const cglib::vec2<float>& p1, const cglib::vec2<float>& p2);
        void drawEllipse(const cglib::vec2<float>& p0, float rx, float ry);

        std::shared_ptr<BitmapImage> buildBitmapImage() const;

    private:
        void drawColorPixel(int x, int y, float alpha);
        void drawSDFPixel(int x, int y, float dist);

        const int _width;
        const int _height;
        const bool _sdfMode;
        float _opacity = 1.0f;
        std::array<std::uint8_t, 4> _color = { { 255, 255, 255, 255 } };
        cglib::mat3x3<float> _transform = cglib::mat3x3<float>::identity();
        cglib::mat3x3<float> _inverseTransform = cglib::mat3x3<float>::identity();
        std::vector<std::uint32_t> _data;
    };
} }

#endif
