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
    /**
     * Where a label's TEXT sits for each side its style offers. Its own header because this is the
     * placement model, tested on its own (tests/vt/LabelAnchorAlignTest.cpp) rather than through a
     * tile build - and it went wrong three ways at once before it was.
     */

    // The pen walk Label::buildPointVertexData does. 'textPart' selects which half of the run is
    // measured: the icon glyphs come first, and the first CR pseudo-glyph resets the pen onto the
    // text's own origin.
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

    // One text layout per side the style allows. The glyph run is the same every time, only its pen
    // origin moves.
    //
    // A RADIAL offset is mapbox's model (evaluateVariableOffset): the distance runs from the ANCHOR
    // to the near edge of the text, along the side's own axis and zero across it, and a corner takes
    // it on the diagonal. Without one the offset is dx/dy measured from the ICON's edge, which is
    // what a style that states no radial offset has always got.
    inline std::vector<TileLabel::Variant> buildLabelVariants(const std::vector<LabelAnchor>& anchors, LabelLineAlign lineAlign, bool textOptional, bool hasIcon, const std::vector<Font::Glyph>& glyphs, const cglib::vec2<float>& iconExtent, const cglib::vec2<float>& styleOffset, float radialOffset) {
        std::vector<TileLabel::Variant> variants;
        // 'text-optional' is a layout list on its own: no side to try, but still the icon alone as
        // a last resort. Most mapbox styles set it WITHOUT a variable anchor, and returning here on
        // an empty anchor list dropped their POI icons with the names the culler could not fit.
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
                    // Centred ACROSS the axis, and the style's own offset stays out of it: carried
                    // in, a name centred under its icon sat one dx to the right of it.
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
