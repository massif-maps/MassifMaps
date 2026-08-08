/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_FONT_H_
#define _CARTO_VT_FONT_H_

#include "Bitmap.h"
#include "GlyphMap.h"

#include <array>
#include <memory>
#include <string>
#include <cstdint>

#include <cglib/vec.h>

namespace carto::vt {
    inline constexpr int GLYPH_RENDER_SIZE = 27;
    // The SDF spread FreeType renders, and it must MATCH the range the field is encoded over
    // (BITMAP_SDF_SCALE): with a smaller spread the distance field is truncated mid-gradient at the
    // glyph bitmap's edge - the outermost texel lands at ~63/255 instead of 0 - so the quad's border
    // renders as a grey fringe hanging off every letter. Tangram ties the two together the same way
    // (core/src/text/fontContext.cpp: m_sdfRadius is the encode range, the atlas padding AND the
    // maximum stroke width).
    inline constexpr int GLYPH_RENDER_SPREAD = 8; // NOTE: keep it equal to BITMAP_SDF_SCALE

    // How much the sampled texture value (0..1) changes over one texel of signed distance:
    // FreeType spreads +-GLYPH_RENDER_SPREAD texels over +-127, and FontManager rescales that by
    // 4 / BITMAP_SDF_SCALE when it encodes the glyph (see addFreeTypeGlyph). The renderer needs it
    // to size the antialias ramp - one screen pixel of distance is this times the number of glyph
    // texels a screen pixel covers.
    inline constexpr float GLYPH_SDF_UNIT = 127.0f * (4.0f / BITMAP_SDF_SCALE) / GLYPH_RENDER_SPREAD / 255.0f;

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
