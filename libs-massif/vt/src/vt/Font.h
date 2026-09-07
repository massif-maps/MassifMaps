/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_FONT_H_
#define _MASSIF_VT_FONT_H_

#include "Bitmap.h"
#include "GlyphMap.h"

#include <array>
#include <memory>
#include <string>
#include <cstdint>

#include <cglib/vec.h>

namespace massif::vt {
    inline constexpr int GLYPH_RENDER_SIZE = 27;
    // The SDF spread FreeType renders, in texels. It is also the padding around the glyph in its
    // bitmap, and therefore the largest halo that can be drawn: past it there is no field left.
    //
    // It must MATCH the range the field is ENCODED over (BITMAP_SDF_SCALE). Equal here but encoded
    // over half of it, the field is truncated mid-gradient at the bitmap's edge - the outermost
    // texel lands at 64/255 instead of 0 - and a halo wide enough to push the ramp below 64/255
    // lights the whole border of the quad: a hairline box around every letter, on any style with a
    // halo of about two pixels or more. Tangram ties the two together the same way
    // (core/src/text/fontContext.cpp: m_sdfRadius is the encode range, the atlas padding AND the
    // maximum stroke width).
    inline constexpr int GLYPH_RENDER_SPREAD = 8; // NOTE: keep it equal to BITMAP_SDF_SCALE

    // How much the sampled texture value (0..1) changes over one texel of signed distance.
    //
    // ONE convention for every SDF in the renderer, set by BitmapCanvas: 128 / BITMAP_SDF_SCALE per
    // texel, so the full 0..255 range spans +-BITMAP_SDF_SCALE texels. A glyph is encoded onto it in
    // addFreeTypeGlyph, and both have to agree - a glyph encoded over a NARROWER range never reaches
    // 0 at the edge of its bitmap, which is what put a hairline box around every letter (see
    // GLYPH_RENDER_SPREAD).
    inline constexpr float GLYPH_SDF_UNIT = (128.0f / BITMAP_SDF_SCALE) / 255.0f;

    // The em sizes a glyph may be rasterized at, as tangram has them (core/src/text/fontContext.cpp:
    // s_fontRasterSizes = { 16, 28, 40 }): a label takes the smallest one that still covers it, and
    // is only magnified past the last. One raster size for every label is what made large text soft
    // - the field itself was undersampled, no antialiasing could put the detail back.
    inline constexpr std::array<int, 3> GLYPH_RENDER_EM_SIZES = { { 16, 28, 40 } };

    // The render size (em plus the SDF spread, which is what FontManager and the renderer count in)
    // for a label drawn at emSizePixels screen pixels per em.
    inline int pickGlyphRenderSize(float emSizePixels) {
        for (int emSize : GLYPH_RENDER_EM_SIZES) {
            if (emSizePixels <= static_cast<float>(emSize)) {
                return emSize + GLYPH_RENDER_SPREAD;
            }
        }
        return GLYPH_RENDER_EM_SIZES.back() + GLYPH_RENDER_SPREAD;
    }

    class Font {
    public:
        using CodePoint = unsigned int;

        enum : CodePoint {
            NULL_CODEPOINT  = 0x00000000U,
            SPACE_CODEPOINT = 0xffff0000U,
            CR_CODEPOINT    = 0xffff0001U
        };

        struct Metrics {
            float ascent;
            float descent;
            float height;

            explicit Metrics(float ascent, float descent, float height) : ascent(ascent), descent(descent), height(height) { }
        };

        struct Glyph {
            std::uint32_t utf32Char;
            CodePoint codePoint;
            GlyphMap::Glyph baseGlyph;
            cglib::vec2<float> size;
            cglib::vec2<float> offset;
            cglib::vec2<float> advance;
            // The glyph belongs to the label's SECOND run of text (TextFormatter::Options), which
            // may have its own colour. Set by the formatter, read when the quads are built.
            bool secondary = false;
            // The glyph belongs to the label's ICON run - the glyphs drawn before the text and left
            // where they are when the text moves to another side (TextLabelStyle::iconGlyphs). Its
            // own colour again, so a font icon does not have to be the colour of the name.
            bool icon = false;

            explicit Glyph(std::uint32_t utf32Char, CodePoint codePoint, const GlyphMap::Glyph& baseGlyph, const cglib::vec2<float>& size, const cglib::vec2<float>& offset, const cglib::vec2<float>& advance) : utf32Char(utf32Char), codePoint(codePoint), baseGlyph(baseGlyph), size(size), offset(offset), advance(advance) { }
        };

        virtual ~Font() = default;

        virtual Metrics getMetrics(float size) const = 0;
        virtual std::vector<Glyph> shapeGlyphs(const std::uint32_t* utf32Text, std::size_t len, float size, bool rtl) const = 0;
        virtual std::shared_ptr<GlyphMap> getGlyphMap() const = 0;
        virtual int getGlyphRenderSize() const = 0;
        // The name it was resolved under, query parameters included - FontManager needs it to hand
        // back the same font rasterized at another size.
        virtual const std::string& getName() const = 0;
    };
}

#endif
