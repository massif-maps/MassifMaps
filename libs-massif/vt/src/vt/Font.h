/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_FONT_H_
#define _CARTO_VT_FONT_H_

#include "Bitmap.h"
#include "GlyphMap.h"

#include <memory>
#include <string>
#include <cstdint>

#include <cglib/vec.h>

namespace carto::vt {
    inline constexpr int GLYPH_RENDER_SIZE = 27;
    inline constexpr int GLYPH_RENDER_SPREAD = 4; // NOTE: keep it equal or smaller than BITMAP_SDF_SCALE

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

            explicit Glyph(std::uint32_t utf32Char, CodePoint codePoint, const GlyphMap::Glyph& baseGlyph, const cglib::vec2<float>& size, const cglib::vec2<float>& offset, const cglib::vec2<float>& advance) : utf32Char(utf32Char), codePoint(codePoint), baseGlyph(baseGlyph), size(size), offset(offset), advance(advance) { }
        };

        virtual ~Font() = default;

        virtual Metrics getMetrics(float size) const = 0;
        virtual std::vector<Glyph> shapeGlyphs(const std::uint32_t* utf32Text, std::size_t len, float size, bool rtl) const = 0;
        virtual std::shared_ptr<GlyphMap> getGlyphMap() const = 0;
    };
}

#endif
