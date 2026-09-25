/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CELESTIALLABEL_H_
#define _MASSIF_CELESTIALLABEL_H_

#include "celestial/CelestialObject.h"

#include <memory>
#include <string>

namespace massif {
    class Bitmap;

    /**
     * Text in the sky: a time on a path, the name of a figure, a rise over a ridge.
     *
     * The SDK draws the text itself, with the platform's text API (BitmapCanvas, as the Text
     * vector element), so an application gives a string and a style rather than painting a bitmap.
     * The label faces the camera and keeps its pixel size at any field of view. Its anchor point
     * says which point of the label sits on its direction: (0, -1), the default, puts the middle of
     * its bottom edge there, so the text stands above the point it names.
     */
    class CelestialLabel : public CelestialObject {
    public:
        CelestialLabel();
        virtual ~CelestialLabel();

        /**
         * Returns the text.
         * @return The text, lines separated by newlines.
         */
        std::string getText() const;
        /**
         * Sets the text.
         * @param text The text, lines separated by newlines.
         */
        void setText(const std::string& text);

        /**
         * Returns the font list.
         * @return The font list.
         */
        std::string getFontName() const;
        /**
         * Sets the font, as a CSS-like list of names, the preferred first ("Roboto Bold, sans-serif").
         * A trailing style ("Bold", "Medium") picks the weight where the platform names fonts by family.
         * @param fontName The font list.
         */
        void setFontName(const std::string& fontName);

        /**
         * Returns the font size.
         * @return The font size in density-independent pixels.
         */
        float getFontSize() const;
        /**
         * Sets the font size.
         * @param size The font size in density-independent pixels. The default is 14.
         */
        void setFontSize(float size);

        /**
         * Returns the text colour. The object's own colour tints the whole label, plate included.
         * @return The text colour.
         */
        Color getTextColor() const;
        /**
         * Sets the text colour.
         * @param color The text colour. The default is black.
         */
        void setTextColor(const Color& color);

        /**
         * Returns the halo colour.
         * @return The halo colour.
         */
        Color getHaloColor() const;
        /**
         * Sets the colour of the outline drawn around the letters.
         * @param color The halo colour.
         */
        void setHaloColor(const Color& color);

        /**
         * Returns the halo width.
         * @return The halo width in density-independent pixels.
         */
        float getHaloWidth() const;
        /**
         * Sets the width of the outline drawn around the letters. 0, the default, draws none.
         * @param width The halo width in density-independent pixels.
         */
        void setHaloWidth(float width);

        /**
         * Returns the background colour.
         * @return The background colour.
         */
        Color getBackgroundColor() const;
        /**
         * Sets the colour of the plate behind the text. Fully transparent, the default, draws none.
         * @param color The background colour.
         */
        void setBackgroundColor(const Color& color);

        /**
         * Returns the corner radius of the plate.
         * @return The radius in density-independent pixels.
         */
        float getBackgroundRadius() const;
        /**
         * Sets the corner radius of the plate.
         * @param radius The radius in density-independent pixels.
         */
        void setBackgroundRadius(float radius);

        /**
         * Returns the horizontal padding between the text and the plate's edge.
         * @return The padding in density-independent pixels.
         */
        float getPaddingX() const;
        /**
         * Sets the horizontal padding between the text and the plate's edge.
         * @param padding The padding in density-independent pixels.
         */
        void setPaddingX(float padding);

        /**
         * Returns the vertical padding between the text and the plate's edge.
         * @return The padding in density-independent pixels.
         */
        float getPaddingY() const;
        /**
         * Sets the vertical padding between the text and the plate's edge.
         * @param padding The padding in density-independent pixels.
         */
        void setPaddingY(float padding);

        /**
         * Returns the horizontal anchor point.
         * @return The anchor point, -1 the left edge, 1 the right edge.
         */
        float getAnchorPointX() const;
        /**
         * Returns the vertical anchor point.
         * @return The anchor point, -1 the bottom edge, 1 the top edge.
         */
        float getAnchorPointY() const;
        /**
         * Sets which point of the label sits on its direction, each coordinate from -1 to 1.
         * @param x -1 the left edge, 0 the middle, 1 the right edge.
         * @param y -1 the bottom edge, 0 the middle, 1 the top edge.
         */
        void setAnchorPoint(float x, float y);

        /**
         * Returns the horizontal offset.
         * @return The offset in density-independent pixels, to the right.
         */
        float getOffsetX() const;
        /**
         * Returns the vertical offset.
         * @return The offset in density-independent pixels, up.
         */
        float getOffsetY() const;
        /**
         * Moves the label on screen from its anchor: a label naming a point of the skyline stands a
         * few pixels clear of it.
         * @param x The offset in density-independent pixels, to the right.
         * @param y The offset in density-independent pixels, up.
         */
        void setOffset(float x, float y);

        /**
         * Returns whether a click on the label hits it.
         * @return True if the label is clickable.
         */
        bool isClickable() const;
        /**
         * Sets whether a click on the label hits it. The default is true.
         * @param clickable True if the label is clickable.
         */
        void setClickable(bool clickable);

        /**
         * The label drawn at the given density, rebuilt only when the text, the style or the density
         * changed. Called by the renderer; an application does not need it.
         * @param dpToPx Device pixels per density-independent pixel.
         * @return The bitmap, or null for an empty text.
         */
        std::shared_ptr<Bitmap> buildBitmap(float dpToPx) const;

    private:
        static const int MAX_CANVAS_SIZE;

        void changed();

        std::string _text;
        std::string _fontName;
        float _fontSize;
        Color _textColor;
        Color _haloColor;
        float _haloWidth;
        Color _backgroundColor;
        float _backgroundRadius;
        float _paddingX;
        float _paddingY;
        float _anchorPointX;
        float _anchorPointY;
        float _offsetX;
        float _offsetY;
        bool _clickable;

        mutable std::shared_ptr<Bitmap> _bitmap;
        mutable float _bitmapDPToPX;
        mutable bool _bitmapValid;
    };

}

#endif
