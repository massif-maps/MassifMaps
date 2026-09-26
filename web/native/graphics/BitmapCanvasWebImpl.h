/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_BITMAPCANVASWEBIMPL_H_
#define _MASSIF_BITMAPCANVASWEBIMPL_H_

#include "graphics/BitmapCanvas.h"
#include "graphics/Color.h"

#include <string>

namespace massif {

    /**
     * The browser's 2D canvas: OffscreenCanvas where available (any thread), a DOM canvas otherwise.
     * Both are synchronous, so the synchronous interface holds; font lists name CSS families.
     */
    class BitmapCanvas::WebImpl : public BitmapCanvas::Impl {
    public:
        WebImpl(int width, int height);
        virtual ~WebImpl();

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
        void applyPaint() const;

        const int _width;
        const int _height;
        int _canvasId;
        DrawMode _drawMode;
        Color _color;
        float _strokeWidth;
        std::string _font;
        float _fontSize;
    };

}

#endif
