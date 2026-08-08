/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_TEXTFORMATTER_H_
#define _CARTO_VT_TEXTFORMATTER_H_

#include "Font.h"

#include <memory>
#include <string>
#include <vector>

#include <cglib/vec.h>
#include <cglib/bbox.h>

namespace carto::vt {
    class TextFormatter final {
    public:
        struct Options {
            cglib::vec2<float> alignment;
            cglib::vec2<float> offset;
            std::string wrapChars;
            bool wrapBefore;
            float wrapWidth;
            float characterSpacing;
            float lineSpacing;
            // A second run of text set after the first one, at its own size - an elevation after a
            // summit name. One label, one baseline, one plate: the pair is laid out as a block and
            // the alignment applies to the block. Empty = nothing to add, which is the default.
            std::string secondaryText;
            float secondaryScale;  // of the main font size
            float secondaryGap;    // between the runs, in pixels like dx/dy
            float secondaryOffset; // baseline shift of the second run, in pixels (up is positive)

            explicit Options(const cglib::vec2<float>& alignment, const cglib::vec2<float>& offset, std::string wrapChars, bool wrapBefore, float wrapWidth, float characterSpacing, float lineSpacing, std::string secondaryText = std::string(), float secondaryScale = 0.7f, float secondaryGap = 0.0f, float secondaryOffset = 0.0f) : alignment(alignment), offset(offset), wrapChars(std::move(wrapChars)), wrapBefore(wrapBefore), wrapWidth(wrapWidth), characterSpacing(characterSpacing), lineSpacing(lineSpacing), secondaryText(std::move(secondaryText)), secondaryScale(secondaryScale), secondaryGap(secondaryGap), secondaryOffset(secondaryOffset) { }
        };

        explicit TextFormatter(std::shared_ptr<const Font> font, float fontSize, const Options& options);

        const std::shared_ptr<const Font>& getFont() const { return _font; }
        float getFontSize() const { return _fontSize; }
        const Options& getOptions() const { return _options; }

        std::vector<Font::Glyph> format(const std::string& text, float fontSize) const;

    private:
        // Lays the secondary run (see Options) out after the glyphs of the main one, and shifts
        // the pair back so that the alignment holds for the two runs together.
        void appendSecondaryRun(std::vector<Font::Glyph>& glyphs) const;
        std::vector<Font::Glyph> layoutLines(const std::string& text, const cglib::vec2<float>& alignment, const cglib::vec2<float>& offset) const;
        static cglib::bbox2<float> measureGlyphs(const std::vector<Font::Glyph>& glyphs);

        struct Line {
            cglib::bbox2<float> bbox;
            std::vector<Font::Glyph> glyphs;

            Line() : bbox(cglib::bbox2<float>::smallest()), glyphs() { }
        };

        std::vector<Line> splitLines(const std::string& text) const;

        const std::shared_ptr<const Font> _font;
        const Font::Metrics _metrics;
        const float _fontSize;
        const Options _options;
    };
}

#endif
