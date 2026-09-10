/*
 * mapbox's text-justify 'auto': the LINES of a wrapped name follow the side the culler put the text
 * on - flush left when the name sits right of its icon, flush right when it sits left, centred above
 * or below. The formatter centres every line inside the block whatever the alignment, so the
 * justification is entirely Label::calculateLineShift's, driven by the chosen variant's lineAlign.
 *
 * NOT covered here: which side the culler picks (LabelCuller is not in this link), and how
 * TileLayerBuilder turns 'auto' into a per-side value - that is a static there. What this pins is
 * that a line ACTUALLY MOVES by the amount the justification asks for, which is what a screenshot
 * of a POI name three lines long shows.
 *
 * The variant SHIFTS are pinned the same way, against the numbers mapbox's evaluateVariableOffset
 * produces: a radial offset runs from the anchor to the near edge of the text on the side's own
 * axis, and is zero across it.
 */

#include "Label.h"
#include "LabelVariants.h"

#include "TestCheck.h"

#include <cmath>
#include <limits>

using namespace massif::vt;

namespace {
    struct FlatTransformer final : public TileTransformer::VertexTransformer {
        cglib::vec3<float> calculatePoint(const cglib::vec2<float>& pos) const override { return cglib::vec3<float>(pos(0), pos(1), 0); }
        cglib::vec3<float> calculateNormal(const cglib::vec2<float>&) const override { return cglib::vec3<float>(0, 0, 1); }
        cglib::vec3<float> calculateVector(const cglib::vec2<float>&, const cglib::vec2<float>& vec) const override { return cglib::vec3<float>(vec(0), vec(1), 0); }
        cglib::vec2<float> calculateTilePosition(const cglib::vec3<float>& pos) const override { return cglib::vec2<float>(pos(0), pos(1)); }
        float calculateHeight(const cglib::vec2<float>&, float height) const override { return height; }
        void tesselateLineString(const cglib::vec2<float>* points, std::size_t count, VertexArray<cglib::vec2<float>>& tesselatedPoints) const override {
            for (std::size_t i = 0; i < count; i++) {
                tesselatedPoints.append(points[i]);
            }
        }
        void tesselateTriangles(const std::size_t*, std::size_t, VertexArray<cglib::vec2<float>>&, VertexArray<cglib::vec2<float>>&, VertexArray<std::size_t>&) const override { }
    };

    /**
     * A two-line name as the formatter hands it over: one CR per line carrying that line's pen
     * origin, and the SHORT line already centred under the long one - four glyphs at x 0..4, then
     * two at x 1..3.
     */
    std::vector<Font::Glyph> buildWrappedGlyphs() {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        auto cr = [&](float x, float y) {
            glyphs.emplace_back(0, Font::CR_CODEPOINT, baseGlyph, cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), cglib::vec2<float>(x, y));
        };
        auto run = [&](int count) {
            for (int i = 0; i < count; i++) {
                glyphs.emplace_back('A', 'A', baseGlyph, cglib::vec2<float>(1, 1), cglib::vec2<float>(0, 0), cglib::vec2<float>(1, 0));
            }
        };
        cr(0, 0);
        run(4);
        cr(1, -2); // (4 - 2) / 2, the centring the formatter applies; a line below the first
        run(2);
        return glyphs;
    }

    /** The three sides a POI name is offered, in the order the style lists them. */
    std::shared_ptr<Label> buildLabel() {
        auto style = std::make_shared<TileLabel::Style>(LabelOrientation::BILLBOARD_2D, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), ColorFunction(Color()), FloatFunction(0.0f), false, 1.0f, 1.0f, 0.0f, std::optional<Transform>(), std::shared_ptr<const GlyphMap>(), 27);
        std::vector<TileLabel::Variant> variants;
        variants.emplace_back(cglib::vec2<float>(0, 0), true,  0.0f); // below the icon: centred
        variants.emplace_back(cglib::vec2<float>(0, 0), true, -1.0f); // right of it: flush left
        variants.emplace_back(cglib::vec2<float>(0, 0), true,  1.0f); // left of it: flush right
        TileLabel tileLabel(1, 1, 0, buildWrappedGlyphs(), cglib::vec2<float>(0, 0), std::vector<cglib::vec2<float>>(),
                            style, TileLabel::PlacementInfo(0, 0, false, false), -1, variants);
        return std::make_shared<Label>(tileLabel, TileId(0, 0, 0), 0, cglib::mat4x4<double>::identity(), std::make_shared<FlatTransformer>());
    }

    ViewState buildViewState() {
        cglib::vec3<double> eye(0, 0, 100);
        cglib::mat4x4<double> cameraMatrix = cglib::lookat4_matrix(eye, cglib::vec3<double>(0, 0, 0), cglib::vec3<double>(0, 1, 0));
        cglib::mat4x4<double> projectionMatrix = cglib::perspective4_matrix(1.0, 1.0, 1.0, 1.0, 1000.0);
        ViewState viewState(projectionMatrix, cameraMatrix, 0, 0, 0, 1, 1);
        viewState.planarProjection = true;
        viewState.focusDistance = 100.0f;
        return viewState;
    }

    /** Where the SECOND line's ink starts and ends, in glyph units, for one of the sides. */
    bool secondLineExtent(const std::shared_ptr<Label>& label, int variantIndex, const ViewState& viewState, float& minX, float& maxX) {
        label->setVariantIndex(variantIndex);
        label->updatePlacement(viewState);
        VertexArray<cglib::vec3<float>> vertices, offsets, normals;
        VertexArray<cglib::vec2<std::int16_t>> texCoords;
        VertexArray<cglib::vec4<std::int8_t>> attribs;
        VertexArray<std::uint16_t> indices;
        if (!label->calculateVertexData(1.0f, viewState, 0, -1, vertices, offsets, normals, texCoords, attribs, indices)) {
            return false;
        }
        minX = std::numeric_limits<float>::max();
        maxX = -std::numeric_limits<float>::max();
        // The second line is the one BELOW the first; the offsets are the glyph quads on the
        // placement's own axes, so a negative y is that line and nothing else.
        for (std::size_t i = 0; i < offsets.size(); i++) {
            if (offsets[i](1) > -0.5f) {
                continue;
            }
            minX = std::min(minX, offsets[i](0));
            maxX = std::max(maxX, offsets[i](0));
        }
        return minX <= maxX;
    }
}

namespace {
    /**
     * A one-line name beside an icon, as the tile builder hands it over: the icon run first, then a
     * CR carrying the line origin, then four square glyphs. The icon is 6 units WIDE and 6 tall
     * around the anchor, so a placement measuring from its edge and one measuring from the anchor
     * cannot be confused.
     */
    std::vector<Font::Glyph> buildIconAndText(const cglib::vec2<float>& styleOffset = cglib::vec2<float>(0, 0)) {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back('O', 'O', baseGlyph, cglib::vec2<float>(6, 6), cglib::vec2<float>(-3, -3), cglib::vec2<float>(0, 0));
        // The formatter bakes dx/dy into the line origin, which is what buildLabelVariants takes
        // back out - so a run built for this test has to carry it too.
        glyphs.emplace_back(0, Font::CR_CODEPOINT, baseGlyph, cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), styleOffset);
        for (int i = 0; i < 4; i++) {
            glyphs.emplace_back('A', 'A', baseGlyph, cglib::vec2<float>(1, 1), cglib::vec2<float>(0, 0), cglib::vec2<float>(1, 0));
        }
        return glyphs;
    }

    /** Where the text's ink lands for one side, once the variant's shift is applied. */
    cglib::bbox2<float> placedText(const TileLabel::Variant& variant, const std::vector<Font::Glyph>& glyphs) {
        cglib::bbox2<float> box = measureGlyphRun(glyphs, true);
        return cglib::bbox2<float>(box.min + variant.shift, box.max + variant.shift);
    }
}

/** mapbox's evaluateVariableOffset, in the SDK's own terms: anchor to the near edge, one axis only. */
void testLabelRadialOffset() {
    std::vector<Font::Glyph> glyphs = buildIconAndText();
    std::vector<LabelAnchor> anchors { LabelAnchor::BOTTOM, LabelAnchor::RIGHT, LabelAnchor::LEFT };
    cglib::vec2<float> iconExtent(3, 3);

    std::vector<TileLabel::Variant> variants = buildLabelVariants(
        anchors, LabelLineAlign::AUTO, true, true, glyphs, iconExtent, cglib::vec2<float>(0, 0), 5.0f);
    TEST_CHECK(variants.size() == 4, "one variant per side, and the icon alone behind them");

    cglib::bbox2<float> below = placedText(variants[0], glyphs);
    cglib::bbox2<float> right = placedText(variants[1], glyphs);
    cglib::bbox2<float> left = placedText(variants[2], glyphs);

    // Below the icon: CENTRED on the anchor across the axis. Carrying the style's dx in here is what
    // slid a name under its icon to the right.
    TEST_CHECK(std::abs(below.min(0) + below.max(0)) < 0.01f, "text below its icon is centred on the anchor");
    TEST_CHECK(std::abs(below.max(1) + 5.0f) < 0.01f, "and its top edge sits the radial offset below it");

    // Beside it: the offset runs from the ANCHOR, not from the icon's 3-unit half-width, and the
    // other axis is untouched.
    TEST_CHECK(std::abs(right.min(0) - 5.0f) < 0.01f, "text right of its icon starts one offset from the anchor");
    TEST_CHECK(std::abs(left.max(0) + 5.0f) < 0.01f, "text left of it ends one offset from the anchor");
    TEST_CHECK(std::abs(right.min(1) + right.max(1)) < 0.01f, "and nothing is added across the axis");
}

/** A style that states no radial offset keeps the icon-edge placement it has always had. */
void testLabelEdgeOffset() {
    std::vector<Font::Glyph> glyphs = buildIconAndText(cglib::vec2<float>(2, 0));
    std::vector<LabelAnchor> anchors { LabelAnchor::RIGHT };
    std::vector<TileLabel::Variant> variants = buildLabelVariants(
        anchors, LabelLineAlign::AUTO, false, true, glyphs, cglib::vec2<float>(3, 3), cglib::vec2<float>(2, 0), 0.0f);

    cglib::bbox2<float> right = placedText(variants[0], glyphs);
    TEST_CHECK(std::abs(right.min(0) - 5.0f) < 0.01f, "the gap is dx past the icon's edge, as before");
}

void testLabelAnchorAlign() {
    ViewState viewState = buildViewState();
    std::shared_ptr<Label> label = buildLabel();

    float centredMin = 0, centredMax = 0, leftMin = 0, leftMax = 0, rightMin = 0, rightMax = 0;
    TEST_CHECK(secondLineExtent(label, 0, viewState, centredMin, centredMax), "the centred side draws its second line");
    TEST_CHECK(secondLineExtent(label, 1, viewState, leftMin, leftMax), "the flush-left side draws its second line");
    TEST_CHECK(secondLineExtent(label, 2, viewState, rightMin, rightMax), "the flush-right side draws its second line");

    // The first line spans 0..4 and never moves: it is the widest, so it IS the block.
    TEST_CHECK(std::abs(centredMin - 1.0f) < 0.01f && std::abs(centredMax - 3.0f) < 0.01f,
               "above or below the icon the short line stays centred under the long one");
    TEST_CHECK(std::abs(leftMin - 0.0f) < 0.01f && std::abs(leftMax - 2.0f) < 0.01f,
               "right of the icon both lines start at the block's left edge");
    TEST_CHECK(std::abs(rightMin - 2.0f) < 0.01f && std::abs(rightMax - 4.0f) < 0.01f,
               "left of the icon both lines end at the block's right edge");
}
