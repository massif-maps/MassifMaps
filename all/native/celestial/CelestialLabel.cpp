#include "CelestialLabel.h"
#include "core/ScreenBounds.h"
#include "core/ScreenPos.h"
#include "graphics/Bitmap.h"
#include "graphics/BitmapCanvas.h"
#include "utils/Log.h"

#include <algorithm>
#include <cmath>

namespace massif {

    const int CelestialLabel::MAX_CANVAS_SIZE = 2048;

    CelestialLabel::CelestialLabel() :
        CelestialObject(),
        _text(),
        _fontName("sans-serif"),
        _fontSize(14.0f),
        _textColor(0xFF000000),
        _haloColor(0xFFFFFFFF),
        _haloWidth(0.0f),
        _backgroundColor(0x00000000),
        _backgroundRadius(0.0f),
        _paddingX(0.0f),
        _paddingY(0.0f),
        _anchorPointX(0.0f),
        _anchorPointY(-1.0f),
        _offsetX(0.0f),
        _offsetY(0.0f),
        _clickable(true),
        _bitmap(),
        _bitmapDPToPX(0.0f),
        _bitmapValid(false)
    {
    }

    CelestialLabel::~CelestialLabel() {
    }

    void CelestialLabel::changed() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _bitmapValid = false;
        }
        notifyChanged();
    }

    std::string CelestialLabel::getText() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _text;
    }

    void CelestialLabel::setText(const std::string& text) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (text == _text) {
                return;
            }
            _text = text;
            _bitmapValid = false;
        }
        notifyChanged();
    }

    std::string CelestialLabel::getFontName() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _fontName;
    }

    void CelestialLabel::setFontName(const std::string& fontName) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _fontName = fontName;
        }
        changed();
    }

    float CelestialLabel::getFontSize() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _fontSize;
    }

    void CelestialLabel::setFontSize(float size) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _fontSize = std::max(1.0f, size);
        }
        changed();
    }

    Color CelestialLabel::getTextColor() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _textColor;
    }

    void CelestialLabel::setTextColor(const Color& color) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _textColor = color;
        }
        changed();
    }

    Color CelestialLabel::getHaloColor() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _haloColor;
    }

    void CelestialLabel::setHaloColor(const Color& color) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _haloColor = color;
        }
        changed();
    }

    float CelestialLabel::getHaloWidth() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _haloWidth;
    }

    void CelestialLabel::setHaloWidth(float width) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _haloWidth = std::max(0.0f, width);
        }
        changed();
    }

    Color CelestialLabel::getBackgroundColor() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _backgroundColor;
    }

    void CelestialLabel::setBackgroundColor(const Color& color) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _backgroundColor = color;
        }
        changed();
    }

    float CelestialLabel::getBackgroundRadius() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _backgroundRadius;
    }

    void CelestialLabel::setBackgroundRadius(float radius) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _backgroundRadius = std::max(0.0f, radius);
        }
        changed();
    }

    float CelestialLabel::getPaddingX() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _paddingX;
    }

    void CelestialLabel::setPaddingX(float padding) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _paddingX = std::max(0.0f, padding);
        }
        changed();
    }

    float CelestialLabel::getPaddingY() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _paddingY;
    }

    void CelestialLabel::setPaddingY(float padding) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _paddingY = std::max(0.0f, padding);
        }
        changed();
    }

    float CelestialLabel::getAnchorPointX() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _anchorPointX;
    }

    float CelestialLabel::getAnchorPointY() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _anchorPointY;
    }

    void CelestialLabel::setAnchorPoint(float x, float y) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _anchorPointX = std::max(-1.0f, std::min(1.0f, x));
            _anchorPointY = std::max(-1.0f, std::min(1.0f, y));
        }
        notifyChanged();
    }

    float CelestialLabel::getOffsetX() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _offsetX;
    }

    float CelestialLabel::getOffsetY() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _offsetY;
    }

    void CelestialLabel::setOffset(float x, float y) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _offsetX = x;
            _offsetY = y;
        }
        notifyChanged();
    }

    bool CelestialLabel::isClickable() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _clickable;
    }

    void CelestialLabel::setClickable(bool clickable) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _clickable = clickable;
        }
        notifyChanged();
    }

    std::shared_ptr<Bitmap> CelestialLabel::buildBitmap(float dpToPx) const {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_bitmapValid && _bitmapDPToPX == dpToPx) {
            return _bitmap;
        }
        _bitmapValid = true;
        _bitmapDPToPX = dpToPx;
        _bitmap.reset();
        if (_text.empty()) {
            return _bitmap;
        }

        // The same layout as the Text vector element: measure, then plate, halo and letters.
        float fontSize = _fontSize * dpToPx;
        float haloWidth = _haloWidth * dpToPx;
        float paddingX = _paddingX * dpToPx;
        float paddingY = _paddingY * dpToPx;
        BitmapCanvas measureCanvas(0, 0);
        measureCanvas.setFont(_fontName, fontSize);
        ScreenBounds textBounds = measureCanvas.measureTextSize(_text, -1, false);
        int width = static_cast<int>(std::ceil(textBounds.getWidth() + haloWidth + 2 * paddingX));
        int height = static_cast<int>(std::ceil(textBounds.getHeight() + haloWidth + 2 * paddingY));
        if (width <= 0 || height <= 0 || width > MAX_CANVAS_SIZE || height > MAX_CANVAS_SIZE) {
            Log::Errorf("CelestialLabel::buildBitmap: Text size out of range: %d x %d", width, height);
            return _bitmap;
        }

        BitmapCanvas canvas(width, height);
        canvas.setFont(_fontName, fontSize);
        if (_backgroundColor.getA() > 0) {
            canvas.setColor(_backgroundColor);
            canvas.setDrawMode(BitmapCanvas::FILL);
            canvas.drawRoundRect(ScreenBounds(ScreenPos(0, 0), ScreenPos(static_cast<float>(width), static_cast<float>(height))), _backgroundRadius * dpToPx);
        }
        ScreenPos textPos(paddingX + haloWidth * 0.5f, paddingY + haloWidth * 0.5f);
        if (haloWidth > 0 && _haloColor.getA() > 0) {
            canvas.setColor(_haloColor);
            canvas.setDrawMode(BitmapCanvas::STROKE);
            canvas.setStrokeWidth(haloWidth);
            canvas.drawText(_text, textPos, static_cast<int>(std::ceil(textBounds.getWidth())), false);
        }
        canvas.setColor(_textColor);
        canvas.setDrawMode(BitmapCanvas::FILL);
        canvas.drawText(_text, textPos, static_cast<int>(std::ceil(textBounds.getWidth())), false);
        _bitmap = canvas.buildBitmap();
        return _bitmap;
    }

}
