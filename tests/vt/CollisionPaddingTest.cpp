/*
 * mapbox's text-padding / icon-padding: the box the COLLISION test uses is grown on every side,
 * while the glyphs are drawn at the same size. The label folds its style's padding into whatever
 * buffer the caller asks for (Label::calculateEnvelope), so every caller gets it consistently.
 *
 * Why it exists at all: shield-min-distance only separates labels of the same GROUP - the text hash
 * (ShieldSymbolizer) - so two DIFFERENT road shields were held apart by nothing but the one-unit
 * floor, and a tilted view packs a lot of far-field map into a thin band.
 *
 * NOT covered here: the culler's own pass (LabelCuller.cpp is not in this link), so what this pins
 * is that the padding reaches the envelope and grows it by the pixels asked for.
 */

#include "Label.h"

#include "TestCheck.h"

#include <cmath>

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

    std::vector<Font::Glyph> buildGlyphs(int count) {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back(0, Font::CR_CODEPOINT, baseGlyph, cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0));
        for (int i = 0; i < count; i++) {
            glyphs.emplace_back('A', 'A', baseGlyph, cglib::vec2<float>(1, 1), cglib::vec2<float>(0, 0), cglib::vec2<float>(1, 0));
        }
        return glyphs;
    }

    /** One square glyph per unit of line, size and scale 1: a glyph unit IS a world unit. */
    std::shared_ptr<Label> buildLabel(float collisionPadding) {
        auto style = std::make_shared<TileLabel::Style>(LabelOrientation::BILLBOARD_2D, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), ColorFunction(Color()), FloatFunction(0.0f), false, 1.0f, 1.0f, 0.0f, std::optional<Transform>(), std::shared_ptr<const GlyphMap>(), 27);
        style->collisionPadding = collisionPadding;
        TileLabel tileLabel(1, 1, 0, buildGlyphs(4), cglib::vec2<float>(0, 0), std::vector<cglib::vec2<float>>(),
                            style, TileLabel::PlacementInfo(0, 0, false, false), -1);
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

    /** Width of the envelope the culler would test, in world units - which here are glyph units. */
    double envelopeWidth(const std::shared_ptr<Label>& label, float buffer, const ViewState& viewState) {
        label->updatePlacement(viewState);
        std::array<cglib::vec3<float>, 4> envelope;
        if (!label->calculateEnvelope((label->getStyle()->sizeFunc)(viewState) , buffer, viewState, envelope)) {
            return 0;
        }
        double minX = std::numeric_limits<double>::max(), maxX = -std::numeric_limits<double>::max();
        for (const cglib::vec3<float>& corner : envelope) {
            minX = std::min(minX, static_cast<double>(corner(0)));
            maxX = std::max(maxX, static_cast<double>(corner(0)));
        }
        return maxX - minX;
    }
}

void testCollisionPadding() {
    ViewState viewState = buildViewState();

    // The SAME caller buffer for both - the culler's floor. The padding rides with it out of the
    // STYLE, so a label that asks for 6 px gets a box 6 px wider on each side and the same glyphs.
    double bare = envelopeWidth(buildLabel(0.0f), 1.0f, viewState);
    double padded = envelopeWidth(buildLabel(6.0f), 1.0f, viewState);

    TEST_CHECK(bare > 0, "a label with no padding still has a collision box");
    TEST_CHECK(padded > bare, "the padded box is wider than the bare one");
    // The buffer is added on BOTH sides and rotated into the label's frame, so the growth is the
    // padding times 2/sqrt(2) - the same conversion calculateEnvelope makes.
    double grown = padded - bare;
    TEST_CHECK(std::abs(grown - 6.0 * 2.0 / std::sqrt(2.0)) < 0.01,
               "the box grows by the padding on each side, and by nothing else");
}
