#include "TextFormatter.h"

#include <utility>
#include <algorithm>
#include <numeric>
#include <vector>

#include <cglib/vec.h>
#include <cglib/mat.h>
#include <cglib/bbox.h>

#include <utf8.h>

#include <bidi.h>

namespace massif::vt {
    TextFormatter::TextFormatter(std::shared_ptr<const Font> font, float fontSize, const Options& options) :
        _font(font), _metrics(font->getMetrics(1.0f)), _fontSize(fontSize), _options(options)
    {
    }

    std::vector<Font::Glyph> TextFormatter::format(const std::string& text, float fontSize) const {
        std::vector<Font::Glyph> glyphs = layoutLines(text, _options.alignment, _options.offset);
        if (!_options.secondaryText.empty()) {
            appendSecondaryRun(glyphs);
        }
        if (fontSize != 1) {
            for (Font::Glyph& glyph : glyphs) {
                glyph.offset *= fontSize;
                glyph.size *= fontSize;
                glyph.advance *= fontSize;
            }
        }
        return glyphs;
    }

    std::vector<Font::Glyph> TextFormatter::layoutLines(const std::string& text, const cglib::vec2<float>& alignment, const cglib::vec2<float>& offset) const {
        // Split text into lines
        std::vector<Line> lines = splitLines(text);

        // Calculate full bounding box and per-line bounding boxes
        cglib::bbox2<float> textBBox = cglib::bbox2<float>::smallest();
        std::for_each(lines.begin(), lines.end(), [&textBBox](const Line& line) { textBBox.add(line.bbox); });

        // Merge line runs, add pseudo-glyphs for new-line offsets
        std::vector<Font::Glyph> glyphs;
        glyphs.reserve(text.size() + lines.size());
        for (const Line& line : lines) {
            float xoff = -textBBox.max(0) * (alignment(0) + 1.0f) * 0.5f + (textBBox.size()(0) - line.bbox.size()(0)) * 0.5f;
            float yoff = (textBBox.min(1) - _metrics.descent) * (-alignment(1) + 1.0f) * 0.5f + (line.bbox.min(1) - textBBox.min(1));
            glyphs.emplace_back(Font::Glyph(0, Font::CR_CODEPOINT, GlyphMap::Glyph(GlyphMap::GlyphMode::BACKGROUND, 0, 0, 0, 0, cglib::vec2<float>(0, 0)), cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), cglib::vec2<float>(xoff, yoff) + offset * (1.0f / _fontSize)));
            glyphs.insert(glyphs.end(), line.glyphs.begin(), line.glyphs.end());
        }
        return glyphs;
    }

    cglib::bbox2<float> TextFormatter::measureGlyphs(const std::vector<Font::Glyph>& glyphs) {
        // The pen walk the renderer does (see Label::buildPointVertexData): a CR pseudo-glyph
        // resets the pen, and its advance then places the line.
        cglib::bbox2<float> bbox = cglib::bbox2<float>::smallest();
        cglib::vec2<float> pen(0, 0);
        for (const Font::Glyph& glyph : glyphs) {
            if (glyph.codePoint == Font::CR_CODEPOINT) {
                pen = cglib::vec2<float>(0, 0);
            }
            else {
                bbox.add(pen + glyph.offset);
                bbox.add(pen + glyph.offset + glyph.size);
            }
            pen += glyph.advance;
        }
        return bbox;
    }

    void TextFormatter::appendSecondaryRun(std::vector<Font::Glyph>& glyphs) const {
        // The second run is laid out on its own, left aligned, then scaled and placed after the
        // first one. Its lines keep their relative positions: a CR pseudo-glyph carries an
        // ABSOLUTE pen position, so every one of them is rebased on the block's origin.
        std::vector<Font::Glyph> secondary = layoutLines(_options.secondaryText, cglib::vec2<float>(-1, _options.alignment(1)), cglib::vec2<float>(0, 0));
        if (secondary.empty()) {
            return;
        }
        float scale = std::max(0.0f, _options.secondaryScale);

        cglib::vec2<float> firstCR(0, 0);
        for (const Font::Glyph& glyph : secondary) {
            if (glyph.codePoint == Font::CR_CODEPOINT) {
                firstCR = glyph.advance;
                break;
            }
        }
        cglib::vec2<float> mainCR(0, 0);
        for (const Font::Glyph& glyph : glyphs) {
            if (glyph.codePoint == Font::CR_CODEPOINT) {
                mainCR = glyph.advance;
                break;
            }
        }

        cglib::bbox2<float> mainBBox = measureGlyphs(glyphs);
        float invFontSize = (_fontSize != 0 ? 1.0f / _fontSize : 0.0f); // gap and baseline shift are pixels, like dx/dy
        float xStart = (mainBBox.min(0) <= mainBBox.max(0) ? mainBBox.max(0) + _options.secondaryGap * invFontSize : 0.0f);
        float yBase = mainCR(1) + _options.secondaryOffset * invFontSize;
        for (Font::Glyph& glyph : secondary) {
            if (glyph.codePoint == Font::CR_CODEPOINT) {
                glyph.advance = cglib::vec2<float>(xStart + (glyph.advance(0) - firstCR(0)) * scale, yBase + (glyph.advance(1) - firstCR(1)) * scale);
            }
            else {
                glyph.offset *= scale;
                glyph.size *= scale;
                glyph.advance *= scale;
            }
        }
        for (Font::Glyph& glyph : secondary) {
            glyph.secondary = true;
        }
        glyphs.insert(glyphs.end(), secondary.begin(), secondary.end());

        // The first run was aligned on its own width; the pair has to be aligned on theirs, so
        // both blocks move by the share of the extra width the alignment asks for.
        cglib::bbox2<float> fullBBox = measureGlyphs(glyphs);
        float extra = (mainBBox.min(0) <= mainBBox.max(0) ? fullBBox.max(0) - mainBBox.max(0) : 0.0f);
        float dx = -extra * (_options.alignment(0) + 1.0f) * 0.5f;
        if (dx != 0) {
            for (Font::Glyph& glyph : glyphs) {
                if (glyph.codePoint == Font::CR_CODEPOINT) {
                    glyph.advance(0) += dx;
                }
            }
        }
    }

    std::vector<TextFormatter::Line> TextFormatter::splitLines(const std::string& text) const {
        std::vector<std::uint32_t> utf32Text;
        utf32Text.reserve(text.size());
        utf8::utf8to32(text.begin(), text.end(), std::back_inserter(utf32Text));
        if (utf32Text.empty()) {
            return std::vector<Line>();
        }
        
        // Classify characters for BIDI algorithm
        std::vector<int> types(utf32Text.size());
        std::vector<int> levels(utf32Text.size());
        std::vector<int> reorderLevels(utf32Text.size());
        bidi_classify(utf32Text.data(), types.data(), static_cast<int>(utf32Text.size()), 0);
        
        // Split original texts into lines, which are split into runs of the same script.
        // Also do line wrapping at this stage.
        std::vector<Line> lines;
        for (std::size_t ich = 0; ich < utf32Text.size(); ) {
            int baseLevel = -1;
            int cchText = static_cast<int>(utf32Text.size() - ich);
            int cchPara = bidi_paragraph(&baseLevel, &types[ich], &levels[ich], cchText);
            while (cchPara > 0) {
                int cchLine = bidi_line(baseLevel, &utf32Text[ich], &types[ich], &levels[ich], &reorderLevels[ich], cchPara, 1, nullptr);
                cchPara -= cchLine;

                std::size_t lineIndex = lines.size();
                lines.emplace_back(Line());
                int lineMask = 0; // 1 for left-to-right, 2 for right-to-left and 3 for bidirectional lines
                float lineWidth = 0.0f;

                std::vector<Font::Glyph> word;
                float wordWidth = 0.0f;
                while (cchLine > 0) {
                    int rtlRun = 0;
                    int cchRun = bidi_run(&utf32Text[ich], &reorderLevels[ich], cchLine, &rtlRun);
                    cchLine -= cchRun;
                    lineMask |= (rtlRun ? 2 : 1);

                    int cchStrippedRun = cchRun;
                    while (cchStrippedRun > 0) {
                        if (utf32Text[ich + cchStrippedRun - 1] != '\n') {
                            break;
                        }
                        cchStrippedRun--;
                    }

                    std::vector<Font::Glyph> glyphs = _font->shapeGlyphs(&utf32Text[ich], cchStrippedRun, 1.0f, false);
                    for (std::size_t i = 0; i < glyphs.size(); i++) {
                        Font::Glyph& glyph = glyphs[i];
                        if (glyph.advance(0) > 0 && glyph.advance(1) == 0) {
                            glyph.advance(0) += _options.characterSpacing / _fontSize;
                        }
                        if (glyph.baseGlyph.width == 0) {
                            glyph.codePoint = Font::SPACE_CODEPOINT;
                        }

                        word.push_back(glyph);
                        wordWidth += glyph.advance(0) * _fontSize;

                        bool isWrapChar = (glyph.codePoint == Font::SPACE_CODEPOINT);
                        for (auto wrapCharIt = _options.wrapChars.begin(); wrapCharIt != _options.wrapChars.end(); ) {
                            std::uint32_t utf32WrapChar = utf8::next(wrapCharIt, _options.wrapChars.end());
                            isWrapChar = isWrapChar || glyph.utf32Char == utf32WrapChar;
                        }

                        if (isWrapChar && i + 1 < glyphs.size() && _options.wrapWidth > 0) {
                            if (lineMask != 3 && lineWidth + wordWidth >= _options.wrapWidth) {
                                if (_options.wrapBefore && !lines[lineIndex].glyphs.empty()) {
                                    if (lineMask == 2) {
                                        lines.insert(lines.begin() + lineIndex, Line());
                                    } else {
                                        lineIndex = lines.size();
                                        lines.emplace_back(Line());
                                    }
                                    lines[lineIndex].glyphs.insert(lines[lineIndex].glyphs.end(), word.begin(), word.end());
                                    lineWidth = wordWidth;
                                }
                                else {
                                    lines[lineIndex].glyphs.insert(lines[lineIndex].glyphs.end(), word.begin(), word.end());
                                    if (lineMask == 2) {
                                        lines.insert(lines.begin() + lineIndex, Line());
                                    } else {
                                        lineIndex = lines.size();
                                        lines.emplace_back(Line());
                                    }
                                    lineWidth = 0;
                                }
                            }
                            else {
                                lines[lineIndex].glyphs.insert(lines[lineIndex].glyphs.end(), word.begin(), word.end());
                                lineWidth += wordWidth;
                            }
                            
                            word.clear();
                            wordWidth = 0;
                        }
                    }

                    ich += cchRun;
                }

                if (lineMask != 3 && _options.wrapWidth > 0 && _options.wrapBefore && lineWidth + wordWidth >= _options.wrapWidth && !lines[lineIndex].glyphs.empty()) {
                    if (lineMask == 2) {
                        lines.insert(lines.begin() + lineIndex, Line());
                    } else {
                        lineIndex = lines.size();
                        lines.emplace_back(Line());
                    }
                }
                lines[lineIndex].glyphs.insert(lines[lineIndex].glyphs.end(), word.begin(), word.end());
            }
        }

        // Calculate line bounding boxes
        cglib::vec2<float> pen(0, 0);
        for (Line& line : lines) {
            pen(0) = 0;
            for (const Font::Glyph& glyph : line.glyphs) {
                line.bbox.add(pen + cglib::vec2<float>(glyph.offset(0), -_metrics.ascent));
                line.bbox.add(pen + cglib::vec2<float>(glyph.offset(0) + glyph.size(0), -_metrics.descent));

                pen += glyph.advance;
            }
            pen(1) -= _metrics.height + _options.lineSpacing / _fontSize;
        }

        return lines;
    }
}
