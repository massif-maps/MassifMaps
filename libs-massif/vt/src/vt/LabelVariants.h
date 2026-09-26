/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_LABELVARIANTS_H_
#define _MASSIF_VT_LABELVARIANTS_H_

#include "TileLabel.h"

#include <cmath>
#include <vector>

namespace massif::vt {
    // Where a label's text sits for each side its style offers; standalone so tests/vt/LabelAnchorAlignTest.cpp can test it.

    // Label::buildPointVertexData's pen walk: icon glyphs first, then the first CR resets the pen to the
    // text's origin. 'textPart' selects which half is measured.
    inline cglib::bbox2<float> measureGlyphRun(const std::vector<Font::Glyph>& glyphs, bool textPart) {
        cglib::bbox2<float> bbox = cglib::bbox2<float>::smallest();
        cglib::vec2<float> pen(0, 0);
        bool text = false;
        for (const Font::Glyph& glyph : glyphs) {
            if (glyph.codePoint == Font::CR_CODEPOINT) {
                pen = cglib::vec2<float>(0, 0);
                text = true;
            }
            else if (text == textPart) {
                bbox.add(pen + glyph.offset);
                bbox.add(pen + glyph.offset + glyph.size);
            }
            pen += glyph.advance;
        }
        return bbox;
    }

    // Justification of the lines of a wrapped name on a given side. AUTO follows the side; an
    // explicit value is mirrored on the left, so "flush against the icon" means the same on both.
    inline float resolveLineAlign(LabelLineAlign align, const cglib::vec2<float>& dir) {
        if (align == LabelLineAlign::AUTO) {
            return (dir(0) > 0 ? -1.0f : dir(0) < 0 ? 1.0f : 0.0f);
        }
        float base = (align == LabelLineAlign::LEFT ? -1.0f : align == LabelLineAlign::RIGHT ? 1.0f : 0.0f);
        return (dir(0) < 0 ? -base : base);
    }

    // One pen origin per side the style allows. A radial offset is mapbox's evaluateVariableOffset (anchor to
    // the text's near edge, along the side's axis, diagonal on a corner); without one, dx/dy from the icon's edge.
    inline std::vector<TileLabel::Variant> buildLabelVariants(const std::vector<LabelAnchor>& anchors, LabelLineAlign lineAlign, bool textOptional, bool hasIcon, const std::vector<Font::Glyph>& glyphs, const cglib::vec2<float>& iconExtent, const cglib::vec2<float>& styleOffset, float radialOffset) {
        std::vector<TileLabel::Variant> variants;
        // 'text-optional' alone still needs the icon-only fallback: most mapbox styles set it without a
        // variable anchor, and returning here dropped their POI icons along with the names.
        bool iconAlone = textOptional && hasIcon;
        if (anchors.empty() && !iconAlone) {
            return variants;
        }

        cglib::bbox2<float> textBBox = measureGlyphRun(glyphs, true);
        if (textBBox.min(0) > textBBox.max(0)) {
            return variants; // no text to move, and none to make optional either
        }
        // The box as it would be with no dx/dy, so that the offset can be re-applied per side.
        cglib::vec2<float> boxMin = textBBox.min - styleOffset;
        cglib::vec2<float> boxMax = textBBox.max - styleOffset;

        bool radial = radialOffset > 0;
        variants.reserve(anchors.size() + 1);
        for (LabelAnchor anchor : anchors) {
            cglib::vec2<float> dir = labelAnchorDirection(anchor);
            // r^2 + r^2 = radialOffset^2 on a corner, as mapbox solves it.
            float diagonal = (dir(0) != 0 && dir(1) != 0 ? 1.0f / std::sqrt(2.0f) : 1.0f);
            cglib::vec2<float> desired(0, 0);
            for (int i = 0; i < 2; i++) {
                float gap = (radial ? radialOffset * diagonal : std::abs(styleOffset(i)));
                float edge = (radial ? 0.0f : iconExtent(i));
                if (dir(i) > 0) {
                    desired(i) = edge + gap - boxMin(i);
                }
                else if (dir(i) < 0) {
                    desired(i) = -edge - gap - boxMax(i);
                }
                else {
                    // Centred across the axis; the style offset must not be carried in or it shifts the name by one dx.
                    desired(i) = -(boxMin(i) + boxMax(i)) * 0.5f + (radial ? 0.0f : styleOffset(i));
                }
            }
            variants.emplace_back(desired - styleOffset, true, resolveLineAlign(lineAlign, dir));
        }
        if (anchors.empty()) {
            // The style's own layout, spelled as a variant so the icon-only one can follow it.
            variants.emplace_back(cglib::vec2<float>(0, 0), true, resolveLineAlign(lineAlign, cglib::vec2<float>(0, 0)));
        }
        if (iconAlone) {
            variants.emplace_back(cglib::vec2<float>(0, 0), false);
        }
        return variants;
    }
}

#endif
