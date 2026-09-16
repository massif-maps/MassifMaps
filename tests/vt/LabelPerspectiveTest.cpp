/*
 * How much smaller a label gets as it recedes from the camera (Label::calculateTerrainScaleFactor).
 *
 * In a planar projection a label is rescaled by view depth over camera-to-focus distance, which
 * cancels the perspective divide exactly and holds a CONSTANT ON-SCREEN SIZE. mapbox and maplibre
 * damp that divide instead of cancelling it - symbol_sdf.vertex.glsl multiplies the size by
 * `clamp(0.5 + 0.5 * distance_ratio, 0, 4)` - so a distant label does shrink, at half the rate.
 * Which of the two we do is `ViewState::labelPerspectiveScaling`, and this pins its endpoints.
 *
 * The visible reason it matters: a LINE label must find that much WORLD line to sit on, so under
 * tilt a road at 3x the focus depth was asked for 3x the road, and its name was dropped.
 *
 * NOT covered here: the shrink itself is a size, so what it looks like is a device check; and the
 * culler's own distance cutoff is LabelDistance's, a different use of the same maplibre ratio.
 */

#include "Label.h"

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

    std::vector<Font::Glyph> buildGlyphs(int count) {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back(0, Font::CR_CODEPOINT, baseGlyph, cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0));
        for (int i = 0; i < count; i++) {
            glyphs.emplace_back('A', 'A', baseGlyph, cglib::vec2<float>(1, 1), cglib::vec2<float>(0, 0), cglib::vec2<float>(1, 0));
        }
        return glyphs;
    }

    /** One square glyph per unit, size and scale 1: a glyph unit IS a world unit. */
    std::shared_ptr<Label> buildLabel(LabelOrientation orientation) {
        auto style = std::make_shared<TileLabel::Style>(orientation, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), ColorFunction(Color()), FloatFunction(0.0f), false, 1.0f, 1.0f, 0.0f, std::optional<Transform>(), std::shared_ptr<const GlyphMap>(), 27);
        TileLabel tileLabel(1, 1, 0, buildGlyphs(4), cglib::vec2<float>(0, 0), std::vector<cglib::vec2<float>>(),
                            style, TileLabel::PlacementInfo(0, 0, false, false), -1);
        return std::make_shared<Label>(tileLabel, TileId(0, 0, 0), 0, cglib::mat4x4<double>::identity(), std::make_shared<FlatTransformer>());
    }

    // The label sits at the origin, 100 below a top-down camera. `focusDistance` is what the zoom
    // sized it for, so setting it to 100 / depthRatio puts the label at depthRatio times the focus
    // depth - the same thing a tilt does to a label past the point the view is centred on, without
    // a tilted frustum to keep it inside.
    ViewState buildViewState(float depthRatio, float perspectiveScaling) {
        cglib::vec3<double> eye(0, 0, 100);
        cglib::mat4x4<double> cameraMatrix = cglib::lookat4_matrix(eye, cglib::vec3<double>(0, 0, 0), cglib::vec3<double>(0, 1, 0));
        cglib::mat4x4<double> projectionMatrix = cglib::perspective4_matrix(1.0, 1.0, 1.0, 1.0, 1000.0);
        ViewState viewState(projectionMatrix, cameraMatrix, 0, 0, 0, 1, 1);
        viewState.planarProjection = true;
        viewState.focusDistance = 100.0f / depthRatio;
        viewState.labelPerspectiveScaling = perspectiveScaling;
        return viewState;
    }

    double envelopeWidth(const std::shared_ptr<Label>& label, const ViewState& viewState) {
        label->updatePlacement(viewState);
        std::array<cglib::vec3<float>, 4> envelope;
        if (!label->calculateEnvelope((label->getStyle()->sizeFunc)(viewState), 0.0f, viewState, envelope)) {
            return 0;
        }
        double minX = std::numeric_limits<double>::max(), maxX = -std::numeric_limits<double>::max();
        for (const cglib::vec3<float>& corner : envelope) {
            minX = std::min(minX, static_cast<double>(corner(0)));
            maxX = std::max(maxX, static_cast<double>(corner(0)));
        }
        return maxX - minX;
    }

    double widthAt(LabelOrientation orientation, float depthRatio, float perspectiveScaling) {
        return envelopeWidth(buildLabel(orientation), buildViewState(depthRatio, perspectiveScaling));
    }
}

void testLabelPerspective() {
    // At the focus point every setting agrees: the ratio is 1, so there is no divide to damp and
    // no setting can change the size. This is also why the effect is invisible looking straight
    // down - a flat map has one depth, and testing the scaling there proves nothing.
    double atFocus = widthAt(LabelOrientation::BILLBOARD_2D, 1.0f, 0.0f);
    TEST_CHECK(atFocus > 0, "a label at the focus point has a collision box");
    TEST_CHECK(std::abs(widthAt(LabelOrientation::BILLBOARD_2D, 1.0f, 0.5f) - atFocus) < 1.0e-3,
               "and the perspective scaling does not change it there");
    TEST_CHECK(std::abs(widthAt(LabelOrientation::BILLBOARD_2D, 1.0f, 1.0f) - atFocus) < 1.0e-3,
               "at any setting");

    // Three times the focus depth. The world size is what the envelope measures, so:
    //   0   - 3x the world size, which is a CONSTANT on-screen size (what this SDK used to do)
    //   0.5 - 2x, maplibre's half-strength damping
    //   1   - 1x, a size fixed to the map, shrinking on screen as fast as the geometry does
    // Tolerant to 1%: the factor is quantized to 1% steps so the line label vertex cache is not
    // rebuilt every frame. Being off by 1% is meaningless here; being off by a FACTOR is the bug.
    double off = widthAt(LabelOrientation::BILLBOARD_2D, 3.0f, 0.0f);
    double half = widthAt(LabelOrientation::BILLBOARD_2D, 3.0f, 0.5f);
    double full = widthAt(LabelOrientation::BILLBOARD_2D, 3.0f, 1.0f);

    TEST_CHECK(std::abs(off / atFocus - 3.0) < 0.03, "with no scaling a label keeps its screen size at 3x the depth");
    TEST_CHECK(std::abs(half / atFocus - 2.0) < 0.03, "half scaling gives up half of that, as maplibre does");
    TEST_CHECK(std::abs(full / atFocus - 1.0) < 0.03, "and full scaling leaves the size fixed to the map");
    TEST_CHECK(off > half && half > full, "so the label shrinks monotonically as the scaling rises");

    // NEARER than the focus point nothing is damped, at any setting. The same blend applied there
    // would scale a label UP - it is 1/depthRatio on screen - and a name lunging at the camera as
    // it crosses the near half of a tilted map is worse than the distant shrink is good.
    double nearOff = widthAt(LabelOrientation::BILLBOARD_2D, 0.5f, 0.0f);
    TEST_CHECK(nearOff < atFocus, "a label nearer than the focus has a smaller world size");
    TEST_CHECK(std::abs(widthAt(LabelOrientation::BILLBOARD_2D, 0.5f, 0.5f) - nearOff) < 1.0e-3,
               "and the perspective scaling leaves the near field alone");
    TEST_CHECK(std::abs(widthAt(LabelOrientation::BILLBOARD_2D, 0.5f, 1.0f) - nearOff) < 1.0e-3,
               "so it never grows a label on approach");

    // A CALLOUT is a screen object - a summit name in a panorama, whose whole point is to stay
    // readable at any range. It is exempt, so a style that turns the scaling up does not shrink it.
    double calloutOff = widthAt(LabelOrientation::CALLOUT, 3.0f, 0.0f);
    double calloutFull = widthAt(LabelOrientation::CALLOUT, 3.0f, 1.0f);
    TEST_CHECK(calloutOff > 0, "a callout has a collision box");
    TEST_CHECK(std::abs(calloutFull - calloutOff) < 1.0e-3, "and the perspective scaling does not shrink it");
}
