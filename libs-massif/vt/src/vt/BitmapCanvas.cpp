#include "BitmapCanvas.h"
#include "Color.h"

namespace {
    float calculateLineDistance(const cglib::vec2<float>& p, const cglib::vec2<float>& p1, const cglib::vec2<float>& p2) {
        cglib::vec2<float> center = (p1 + p2) * 0.5f;
        float len = cglib::length(p2 - p1);
        cglib::vec2<float> dir = (p2 - p1) * (1.0f / len);
        return cglib::dot_product(p - center, cglib::vec2<float>(dir(1), -dir(0)));
    }
}

namespace carto { namespace vt {
    BitmapCanvas::BitmapCanvas(int width, int height, bool sdfMode) : _width(width), _height(height), _sdfMode(sdfMode), _data(width * height) {
    }

    void BitmapCanvas::setOpacity(float opacity) {
        if (_sdfMode) {
            throw std::invalid_argument("Opacity not supported in SDF mode");
        }
        _opacity = std::max(0.0f, std::min(1.0f, opacity));
    }

    void BitmapCanvas::setColor(const Color& color) {
        if (_sdfMode) {
            throw std::invalid_argument("Color not supported in SDF mode");
        }
        _color = color.rgba8();
    }

    void BitmapCanvas::setTransform(const cglib::mat3x3<float>& transform) {
        _transform = transform;
        _inverseTransform = cglib::inverse(transform);
    }

    void BitmapCanvas::drawRectangle(const cglib::vec2<float>& p0, const cglib::vec2<float>& p1) {
        if (p0 == p1) {
            return;
        }
        for (int y = 0; y < _height; y++) {
            for (int x = 0; x < _width; x++) {
                cglib::vec2<float> p = cglib::transform_point_affine(cglib::vec2<float>(x, y), _inverseTransform) + cglib::vec2<float>(0.5f, 0.5f);
                float xd0 = 0.5f + p(0) - p0(0);
                float yd0 = 0.5f + p(1) - p0(1);
                float xd1 = 0.5f - p(0) + p1(0);
                float yd1 = 0.5f - p(1) + p1(1);

                if (_sdfMode) {
                    drawSDFPixel(x, y, std::min(std::min(xd0, xd1), std::min(yd0, yd1)));
                }
                else if (xd0 > 0 && xd1 > 0 && yd0 > 0 && yd1 > 0) {
                    drawColorPixel(x, y, std::min(1.0f, xd0) * std::min(1.0f, xd1) * std::min(1.0f, yd0) * std::min(1.0f, yd1));
                }
            }
        }
    }
    
    void BitmapCanvas::drawTriangle(const cglib::vec2<float>& p0, const cglib::vec2<float>& p1, const cglib::vec2<float>& p2) {
        if (p0 == p1 || p0 == p2 || p1 == p2) {
            return;
        }
        for (int y = 0; y < _height; y++) {
            for (int x = 0; x < _width; x++) {
                cglib::vec2<float> p = cglib::transform_point_affine(cglib::vec2<float>(x, y), _inverseTransform) + cglib::vec2<float>(0.5f, 0.5f);
                float d1 = 0.5f + calculateLineDistance(p, p0, p1);
                float d2 = 0.5f + calculateLineDistance(p, p1, p2);
                float d3 = 0.5f + calculateLineDistance(p, p2, p0);
                
                if (_sdfMode) {
                    drawSDFPixel(x, y, std::min(d1, std::min(d2, d3)));
                }
                else if (d1 > 0 && d2 > 0 && d3 > 0) {
                    drawColorPixel(x, y, std::min(1.0f, d1) * std::min(1.0f, d2) * std::min(1.0f, d3));
                }
            }
        }
    }
    
    void BitmapCanvas::drawEllipse(const cglib::vec2<float>& p0, float rx, float ry) {
        if (rx == 0 || ry == 0) {
            return;
        }
        float rscale = std::min(rx, ry);
        float xscale = 1.0f / rx;
        float yscale = 1.0f / ry;
        for (int y = 0; y < _height; y++) {
            for (int x = 0; x < _width; x++) {
                cglib::vec2<float> p = cglib::transform_point_affine(cglib::vec2<float>(x, y), _inverseTransform) + cglib::vec2<float>(0.5f, 0.5f);
                float xd = (p(0) - p0(0)) * xscale;
                float yd = (p(1) - p0(1)) * yscale;
                float rd = 0.5f + (1 - xd * xd - yd * yd) * rscale;

                if (_sdfMode) {
                    drawSDFPixel(x, y, rd);
                }
                else if (rd > 0) {
                    drawColorPixel(x, y, std::min(1.0f, rd));
                }
            }
        }
    }

    std::shared_ptr<BitmapImage> BitmapCanvas::buildBitmapImage() const {
        return std::make_shared<BitmapImage>(_sdfMode, 1.0f, std::make_shared<Bitmap>(_width, _height, _data));
    }

    void BitmapCanvas::drawColorPixel(int x, int y, float alpha) {
        std::uint8_t* frameColor = reinterpret_cast<std::uint8_t*>(&_data.at(y * _width + x));
        if (alpha >= 1.0f && _opacity >= 1.0f) {
            for (int c = 0; c < 4; c++) {
                frameColor[c] = _color[c];
            }
        }
        else if (alpha > 0.0f) {
            int factor = static_cast<int>(std::max(0.0f, std::min(1.0f, alpha)) * _opacity * 255.0f);
            int blend1 = 1 + factor;
            int blend2 = 255 - factor;
            for (int c = 0; c < 4; c++) {
                int comp1 = _color[c] * blend1;
                int comp2 = frameColor[c] * blend2;
                frameColor[c] = static_cast<std::uint8_t>((comp1 + comp2) >> 8);
            }
        }
    }

    void BitmapCanvas::drawSDFPixel(int x, int y, float dist) {
        std::uint8_t* frameColor = reinterpret_cast<std::uint8_t*>(&_data.at(y * _width + x));
        std::uint8_t val = static_cast<std::uint8_t>(std::max(0.0f, std::min(255.0f, dist * (128.0f / BITMAP_SDF_SCALE) + 127.5f)));
        for (int c = 0; c < 4; c++) {
            frameColor[c] = std::max(frameColor[c], val);
        }
    }
}}
