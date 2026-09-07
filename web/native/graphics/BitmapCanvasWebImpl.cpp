#include "graphics/BitmapCanvasWebImpl.h"
#include "graphics/Bitmap.h"
#include "utils/Log.h"

#include <atomic>

namespace massif {

    BitmapCanvas::WebImpl::WebImpl(int width, int height) :
        _width(width),
        _height(height)
    {
    }

    void BitmapCanvas::WebImpl::WarnUnsupported() {
        static std::atomic<bool> warned(false);
        if (!warned.exchange(true)) {
            Log::Warn("BitmapCanvas::WebImpl: Not implemented on the web build - Text and BalloonPopup elements do not draw");
        }
    }

    void BitmapCanvas::WebImpl::setDrawMode(DrawMode mode) {
    }

    void BitmapCanvas::WebImpl::setColor(const Color& color) {
    }

    void BitmapCanvas::WebImpl::setStrokeWidth(float width) {
    }

    void BitmapCanvas::WebImpl::setFont(const std::string& familyName, const std::string& fileName, float size) {
    }

    void BitmapCanvas::WebImpl::pushClipRect(const ScreenBounds& clipRect) {
    }

    void BitmapCanvas::WebImpl::popClipRect() {
    }

    void BitmapCanvas::WebImpl::drawText(std::string text, const ScreenPos& pos, int maxWidth, bool breakLines) {
        WarnUnsupported();
    }

    void BitmapCanvas::WebImpl::drawPolygon(const std::vector<ScreenPos>& poses) {
        WarnUnsupported();
    }

    void BitmapCanvas::WebImpl::drawRoundRect(const ScreenBounds& rect, float radius) {
        WarnUnsupported();
    }

    void BitmapCanvas::WebImpl::drawBitmap(const ScreenBounds& rect, const std::shared_ptr<Bitmap>& bitmap) {
        WarnUnsupported();
    }

    ScreenBounds BitmapCanvas::WebImpl::measureTextSize(std::string text, int maxWidth, bool breakLines) const {
        WarnUnsupported();
        return ScreenBounds(ScreenPos(0, 0), ScreenPos(0, 0));
    }

    std::shared_ptr<Bitmap> BitmapCanvas::WebImpl::buildBitmap() const {
        const unsigned char pixel[] = { 0, 0, 0, 0 };
        return std::make_shared<Bitmap>(pixel, 1, 1, ColorFormat::COLOR_FORMAT_RGBA, 4);
    }

}
