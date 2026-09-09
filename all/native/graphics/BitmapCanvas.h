/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_BITMAPCANVAS_H_
#define _MASSIF_BITMAPCANVAS_H_

#include "core/ScreenPos.h"
#include "core/ScreenBounds.h"
#include "graphics/Color.h"
#include "graphics/Bitmap.h"

#include <memory>
#include <string>
#include <vector>

namespace massif {

    class BitmapCanvas {
    public:
        enum DrawMode {
            FILL,
            STROKE
        };

        BitmapCanvas(int width, int height);
        virtual ~BitmapCanvas();

        void setDrawMode(DrawMode mode);
        void setColor(const Color& color);
        void setStrokeWidth(float width);
        /**
         * @param names A CSS-like font list, the most preferred name first, entries optionally
         *              tagged with the platform they are for ("android:Roboto, ios:Helvetica Neue").
         */
        void setFont(const std::string& names, float size);

        void pushClipRect(const ScreenBounds& clipRect);
        void popClipRect();

        void drawText(const std::string& text, const ScreenPos& pos, int maxWidth, bool breakLines);
        void drawPolygon(const std::vector<ScreenPos>& poses);
        void drawRoundRect(const ScreenBounds& rect, float radius);
        void drawBitmap(const ScreenBounds& rect, const std::shared_ptr<Bitmap>& bitmap);

        ScreenBounds measureTextSize(const std::string& text, int maxWidth, bool breakLines) const;

        std::shared_ptr<Bitmap> buildBitmap() const;

    protected:
        class Impl {
        public:
            virtual ~Impl();

            virtual void setDrawMode(DrawMode mode) = 0;
            virtual void setColor(const Color& color) = 0;
            virtual void setStrokeWidth(float width) = 0;
            // A font already matched to the device: the family name the platform accepts, or - when
            // the platform has no name for it - the font file. Both empty keeps the default font.
            virtual void setFont(const std::string& familyName, const std::string& fileName, float size) = 0;

            virtual void pushClipRect(const ScreenBounds& clipRect) = 0;
            virtual void popClipRect() = 0;

            virtual void drawText(std::string text, const ScreenPos& pos, int maxWidth, bool breakLines) = 0;
            virtual void drawPolygon(const std::vector<ScreenPos>& poses) = 0;
            virtual void drawRoundRect(const ScreenBounds& rect, float radius) = 0;
            virtual void drawBitmap(const ScreenBounds& rect, const std::shared_ptr<Bitmap>& bitmap) = 0;

            virtual ScreenBounds measureTextSize(std::string text, int maxWidth, bool breakLines) const = 0;

            virtual std::shared_ptr<Bitmap> buildBitmap() const = 0;
        };

        class AndroidImpl;
        class IOSImpl;
        class UWPImpl;
        class WebImpl;
        
        std::unique_ptr<Impl> _impl;
    };

}

#endif
