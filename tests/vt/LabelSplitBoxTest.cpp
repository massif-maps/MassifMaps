/*
 * A POI with an icon and a name collides as TWO boxes, as maplibre's placement tests its text and
 * icon boxes: one box over both claimed the empty corners beside the icon, and the culler hid
 * ~80% of the POIs MapLibre shows at a Grenoble camera. Label::calculateVariantEnvelopes hands the
 * culler the text box and the icon box as parts of the whole; a text-less variant (text-optional) stays one box.
 *
 * NOT covered here: the culler's own overlap test of split records (LabelCuller.cpp is not in this link).
 */

#include "Label.h"

#include "TestCheck.h"

#include <algorithm>
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

    /** A 2x2 icon, then a line break and a 6x1 name: the run a shield hands over. */
    std::vector<Font::Glyph> buildGlyphs() {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back('I', 'I', baseGlyph, cglib::vec2<float>(2, 2), cglib::vec2<float>(-1, -1), cglib::vec2<float>(0, 0));
        glyphs.emplace_back(0, Font::CR_CODEPOINT, baseGlyph, cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0));
        for (int i = 0; i < 6; i++) {
            glyphs.emplace_back('A', 'A', baseGlyph, cglib::vec2<float>(1, 1), cglib::vec2<float>(0, 0), cglib::vec2<float>(1, 0));
        }
        return glyphs;
    }

    std::shared_ptr<Label> buildLabel() {
        auto style = std::make_shared<TileLabel::Style>(LabelOrientation::BILLBOARD_2D, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), ColorFunction(Color()), FloatFunction(0.0f), false, 1.0f, 1.0f, 0.0f, std::optional<Transform>(), std::shared_ptr<const GlyphMap>(), 27);
        std::vector<TileLabel::Variant> variants;
        variants.emplace_back(cglib::vec2<float>(-3, -3), true, 0.0f); // the name under the icon
        variants.emplace_back(cglib::vec2<float>(0, 0), false, 0.0f);  // text-optional: the icon alone
        TileLabel tileLabel(1, 1, 0, buildGlyphs(), cglib::vec2<float>(0, 0), std::vector<cglib::vec2<float>>(),
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

    cglib::bbox2<float> boundsOf(const std::array<cglib::vec3<float>, 4>& envelope) {
        cglib::bbox2<float> bounds = cglib::bbox2<float>::smallest();
        for (const cglib::vec3<float>& corner : envelope) {
            bounds.add(cglib::vec2<float>(corner(0), corner(1)));
        }
        return bounds;
    }
}

void testLabelSplitBox() {
    std::printf("  LabelSplitBox\n");
    ViewState viewState = buildViewState();
    std::shared_ptr<Label> label = buildLabel();
    label->updatePlacement(viewState);

    std::vector<std::array<cglib::vec3<float>, 4>> envelopes;
    std::vector<std::vector<std::array<cglib::vec3<float>, 4>>> partEnvelopes;
    TEST_CHECK(label->calculateVariantEnvelopes(1.0f, 0.0f, viewState, envelopes, partEnvelopes), "the label faces the camera");
    TEST_CHECK(envelopes.size() == 2 && partEnvelopes.size() == 2, "one entry per variant");
    if (envelopes.size() != 2 || partEnvelopes.size() != 2) {
        return;
    }

    TEST_CHECK(partEnvelopes[0].size() == 2, "a name beside an icon collides as the name's box and the icon's");
    if (partEnvelopes[0].size() == 2) {
        cglib::bbox2<float> whole = boundsOf(envelopes[0]), text = boundsOf(partEnvelopes[0][0]), icon = boundsOf(partEnvelopes[0][1]);
        TEST_CHECK(text.size()(0) > 5.5f && text.size()(1) < 1.5f, "the text box is the name's, 6 wide and 1 high");
        TEST_CHECK(icon.size()(0) < 2.5f && icon.size()(1) < 2.5f, "the icon box is the icon's");
        TEST_CHECK(!text.inside(icon), "and the two do not overlap: the corners beside the icon are free");
        TEST_CHECK(whole.size()(0) >= text.size()(0) && whole.size()(1) > text.size()(1) + icon.size()(1) - 0.01f, "the whole box still covers both, for the grid");
    }
    TEST_CHECK(partEnvelopes[1].empty(), "the icon alone stays one box");
}
