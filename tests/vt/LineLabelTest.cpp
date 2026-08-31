/*
 * Line labels: the glyph run may not leave the line it names (see Label::buildLineVertexData).
 * A run given room past either end was laid out on a straight CONTINUATION of the geometry, so a
 * street name ran off the end of its road - and off the curve where the road bent away.
 *
 * Label.cpp is the one vt source in this link. It reaches no font, no tile builder and no
 * renderer, so a label can be built here out of glyph metrics alone.
 *
 * NOT covered here: what the run LOOKS like (the atlas and the shader are device checks), the
 * projected path of a 'line-billboard' run (same layout code, but its line is the one the camera
 * projects, which needs a tilted view to differ), and collision, which is LabelCuller's.
 */

#include "Label.h"

#include "TestCheck.h"

#include <cmath>
#include <limits>

using namespace massif::vt;

namespace {
    // Tile coordinates ARE world coordinates here, flat at z = 0: the layout is what is under
    // test, not the tile-to-world transform.
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

    // One square glyph per unit of line: with size, zoomScale and the style scale all 1, a glyph
    // unit IS a world unit, so a run of n glyphs needs n units of line and the numbers below can
    // be read off the geometry.
    std::vector<Font::Glyph> buildGlyphs(int count) {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back(0, Font::CR_CODEPOINT, baseGlyph, cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0));
        for (int i = 0; i < count; i++) {
            glyphs.emplace_back('A', 'A', baseGlyph, cglib::vec2<float>(1, 1), cglib::vec2<float>(0, 0), cglib::vec2<float>(1, 0));
        }
        return glyphs;
    }

    std::shared_ptr<TileLabel::Style> buildStyle() {
        return std::make_shared<TileLabel::Style>(LabelOrientation::LINE, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), ColorFunction(Color()), FloatFunction(0.0f), false, 1.0f, 1.0f, 0.0f, std::optional<Transform>(), std::shared_ptr<const GlyphMap>(), 27);
    }

    // Straight west-east line from (0, 0) to (lineLength, 0), with the label's own anchor at
    // anchorX - the anchor a point label snapped onto the line brings with it.
    std::shared_ptr<Label> buildLineLabel(int glyphCount, float lineLength, float anchorX) {
        TileLabel tileLabel(1, 1, 0, buildGlyphs(glyphCount), cglib::vec2<float>(anchorX, 0),
                            std::vector<cglib::vec2<float>>{ cglib::vec2<float>(0, 0), cglib::vec2<float>(lineLength, 0) },
                            buildStyle(), TileLabel::PlacementInfo(0, 0, false, false), -1);
        return std::make_shared<Label>(tileLabel, TileId(0, 0, 0), 0, cglib::mat4x4<double>::identity(), std::make_shared<FlatTransformer>());
    }

    // Top-down camera over the middle of the line, far enough that the whole line is in frustum.
    ViewState buildViewState(float lineLength) {
        cglib::vec3<double> eye(lineLength * 0.5, 0, 100);
        cglib::mat4x4<double> cameraMatrix = cglib::lookat4_matrix(eye, cglib::vec3<double>(lineLength * 0.5, 0, 0), cglib::vec3<double>(0, 1, 0));
        cglib::mat4x4<double> projectionMatrix = cglib::perspective4_matrix(1.0, 1.0, 1.0, 1.0, 1000.0);
        return ViewState(projectionMatrix, cameraMatrix, 0, 0, 0, 1, 1);
    }
}

void testLineLabel() {
    const float lineLength = 10.0f;

    // A run that fits, anchored one glyph from the end of the line: the anchor is slid back until
    // the run fits (clampPlacementAnchor), and the run then has to stay INSIDE the line. The
    // anchor is what used to push the last glyphs past the end.
    {
        std::shared_ptr<Label> label = buildLineLabel(8, lineLength, 9.0f);
        ViewState viewState = buildViewState(lineLength);
        label->updatePlacement(viewState);

        std::array<cglib::vec3<float>, 4> envelope;
        TEST_CHECK(label->calculateEnvelope(viewState, envelope), "a run shorter than its line is laid out on it");

        // The envelope comes back camera-relative, and it is the run's own bounds - so this is the
        // run itself being measured against the line it was laid out on.
        double minX = std::numeric_limits<double>::max(), maxX = -std::numeric_limits<double>::max();
        for (const cglib::vec3<float>& corner : envelope) {
            double x = viewState.origin(0) + corner(0);
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
        }
        TEST_CHECK(maxX <= lineLength + 1.0e-3, "the run does not reach past the end of the line");
        TEST_CHECK(minX >= -1.0e-3, "the run does not reach past the start of the line");
        TEST_CHECK(maxX - minX >= 7.9, "the whole run is laid out, not a clipped part of it");
    }

    // A run longer than its line is dropped rather than drawn on a straight continuation of it.
    // 12 glyphs on 10 units of line is what the old allowance (1.5x the line) still accepted.
    {
        std::shared_ptr<Label> label = buildLineLabel(12, lineLength, 5.0f);
        ViewState viewState = buildViewState(lineLength);
        label->updatePlacement(viewState);

        std::array<cglib::vec3<float>, 4> envelope;
        TEST_CHECK(!label->calculateEnvelope(viewState, envelope), "a run longer than its line is not laid out at all");
    }
}
