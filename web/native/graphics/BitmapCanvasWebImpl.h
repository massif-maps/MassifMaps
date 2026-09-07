/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_BITMAPCANVASWEBIMPL_H_
#define _MASSIF_BITMAPCANVASWEBIMPL_H_

#include "graphics/BitmapCanvas.h"

namespace massif {

    /**
     * NOT IMPLEMENTED on the web build.
     *
     * The other platforms draw this through a system text and 2D API (Canvas/CoreGraphics/D2D);
     * the browser's are on the main thread and asynchronous, which this synchronous interface
     * cannot reach from a worker. Only Text and BalloonPopup vector elements use it, so the web
     * build simply does not carry them - every call warns once and produces an empty bitmap
     * rather than silently drawing nothing.
     */
    class BitmapCanvas::WebImpl : public BitmapCanvas::Impl {
    public:
        WebImpl(int width, int height);

        virtual void setDrawMode(DrawMode mode);
        virtual void setColor(const Color& color);
        virtual void setStrokeWidth(float width);
        virtual void setFont(const std::string& familyName, const std::string& fileName, float size);

        virtual void pushClipRect(const ScreenBounds& clipRect);
        virtual void popClipRect();

        virtual void drawText(std::string text, const ScreenPos& pos, int maxWidth, bool breakLines);
        virtual void drawPolygon(const std::vector<ScreenPos>& poses);
        virtual void drawRoundRect(const ScreenBounds& rect, float radius);
        virtual void drawBitmap(const ScreenBounds& rect, const std::shared_ptr<Bitmap>& bitmap);

        virtual ScreenBounds measureTextSize(std::string text, int maxWidth, bool breakLines) const;

        virtual std::shared_ptr<Bitmap> buildBitmap() const;

    private:
        static void WarnUnsupported();

        const int _width;
        const int _height;
    };

}

#endif
