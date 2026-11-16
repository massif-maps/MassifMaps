/*
 * A line label whose anchor is off the real screen, though the street is in view, slides along its
 * own line to the nearest stretch that is on screen and holds the whole run. It is the padded band
 * that keeps such an anchor placed (so it is ready when it scrolls in), and with nothing to move it
 * back the street showed no name. An anchor on screen never moves, and a slid label returns to its
 * anchor the moment that is on screen again.
 *
 * NOT covered here: the layout of the run on the slid line (LineLabelTest), a tilted camera
 * (the visible stretch is measured on the ground, the run on the projected line), and collision.
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

    // A run of n glyphs is n units of line.
    std::vector<Font::Glyph> buildGlyphs(int count) {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back(0, Font::CR_CODEPOINT, baseGlyph, cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), cglib::vec2<float>(-count * 0.5f, 0));
        for (int i = 0; i < count; i++) {
            glyphs.emplace_back('A', 'A', baseGlyph, cglib::vec2<float>(1, 1), cglib::vec2<float>(0, 0), cglib::vec2<float>(1, 0));
        }
        return glyphs;
    }

    std::shared_ptr<Label> buildLabel(float lineStart, float lineEnd, float anchorX) {
        auto style = std::make_shared<TileLabel::Style>(LabelOrientation::LINE, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), ColorFunction(Color()), FloatFunction(0.0f), false, 1.0f, 1.0f, 0.0f, std::optional<Transform>(), std::shared_ptr<const GlyphMap>(), 27);
        TileLabel tileLabel(1, 1, 0, buildGlyphs(10), cglib::vec2<float>(anchorX, 0),
                            std::vector<cglib::vec2<float>>{ cglib::vec2<float>(lineStart, 0), cglib::vec2<float>(lineEnd, 0) },
                            style, TileLabel::PlacementInfo(0, 0, false, false), -1);
        return std::make_shared<Label>(tileLabel, TileId(0, 0, 0), 0, cglib::mat4x4<double>::identity(), std::make_shared<FlatTransformer>());
    }

    // Top-down, centred on x = centerX: the screen is centerX +- ~54.6 at the ground, and the padded
    // band (20 of 100 pixels) reaches ~65.5.
    ViewState buildViewState(double centerX) {
        cglib::vec3<double> eye(centerX, 0, 100);
        cglib::mat4x4<double> cameraMatrix = cglib::lookat4_matrix(eye, cglib::vec3<double>(centerX, 0, 0), cglib::vec3<double>(0, 1, 0));
        cglib::mat4x4<double> projectionMatrix = cglib::perspective4_matrix(1.0, 1.0, 1.0, 1.0, 1000.0);
        return ViewState(projectionMatrix, cameraMatrix, 0, 0, 0, 1, 100);
    }

    double centerOf(const Label& label) {
        cglib::vec3<double> position(0, 0, 0);
        return (label.calculateCenter(position) ? position(0) : std::numeric_limits<double>::quiet_NaN());
    }
}

void testLabelSlide() {
    {
        std::shared_ptr<Label> label = buildLabel(0, 300, 60);
        ViewState viewState = buildViewState(0);
        bool changed = label->updatePlacement(viewState);
        double center = centerOf(*label);
        TEST_CHECK(!changed, "a slide is not a new placement, so a visible run is not faded again");
        TEST_CHECK(std::abs(center - 27.3) < 1.0, "an anchor off the screen puts the run in the middle of the stretch that is on it");
        TEST_CHECK(!label->updatePlacement(viewState) && std::abs(centerOf(*label) - center) < 1e-9, "and it stays there");

        label->updatePlacement(buildViewState(60));
        TEST_CHECK(std::abs(centerOf(*label) - center) < 1e-9, "also when its anchor comes on screen: a run on screen does not move");

        label->updatePlacement(buildViewState(150));
        TEST_CHECK(std::abs(centerOf(*label) - 150) < 1.0, "and moves to the middle of the street in view once it is left behind");

        label->updatePlacement(buildViewState(60));
        TEST_CHECK(std::abs(centerOf(*label) - 150) > 1.0 && std::abs(centerOf(*label) - 60) < 1e-6, "back on the anchor when that is on screen and the run is not");
    }

    {
        std::shared_ptr<Label> label = buildLabel(0, 300, 60);
        ViewState viewState = buildViewState(0);
        label->updatePlacement(viewState);
        label->retrySlide(viewState.viewProjMatrix);
        label->updatePlacement(viewState);
        TEST_CHECK(std::abs(centerOf(*label) - 13.65) < 1.0, "a run the culler hid takes another spot along the stretch");
        label->retrySlide(viewState.viewProjMatrix);
        label->updatePlacement(viewState);
        TEST_CHECK(std::abs(centerOf(*label) - 40.95) < 1.0, "and another on the next try");
        label->updatePlacement(viewState);
        TEST_CHECK(std::abs(centerOf(*label) - 40.95) < 1.0, "which it keeps once nothing hides it");

        std::shared_ptr<Label> anchored = buildLabel(0, 300, 20);
        anchored->updatePlacement(viewState);
        anchored->retrySlide(viewState.viewProjMatrix);
        anchored->updatePlacement(viewState);
        TEST_CHECK(std::abs(centerOf(*anchored) - 27.3) < 1.0, "an anchor on screen that is hidden tries the other spots too");
        for (int i = 0; i < 5; i++) {
            anchored->retrySlide(viewState.viewProjMatrix);
            anchored->updatePlacement(viewState);
        }
        TEST_CHECK(std::abs(centerOf(*anchored) - 20) < 1e-6, "and comes back to its anchor after the last");
        anchored->updatePlacement(viewState);
        TEST_CHECK(std::abs(centerOf(*anchored) - 20) < 1e-6, "which it keeps while nothing hides it");
    }

    {
        std::shared_ptr<Label> label = buildLabel(0, 300, 250);
        label->updatePlacement(buildViewState(0));
        TEST_CHECK(std::abs(centerOf(*label) - 27.3) < 1.0, "an anchor far beyond the padded band still names the street in view");
    }

    {
        std::shared_ptr<Label> label = buildLabel(0, 300, 20);
        ViewState viewState = buildViewState(0);
        label->updatePlacement(viewState);
        label->updatePlacement(viewState);
        TEST_CHECK(std::abs(centerOf(*label) - 20) < 1e-6, "an anchor on screen never moves");
    }

    {
        std::shared_ptr<Label> label = buildLabel(50, 300, 60);
        ViewState viewState = buildViewState(0);
        label->updatePlacement(viewState);
        label->updatePlacement(viewState);
        TEST_CHECK(std::abs(centerOf(*label) - 60) < 1e-6, "a street with less than a run in view leaves it on its anchor");
    }

    {
        std::shared_ptr<Label> label = buildLabel(100, 300, 110);
        label->updatePlacement(buildViewState(0));
        TEST_CHECK(std::isnan(centerOf(*label)), "a street out of view is not placed");
    }
}
