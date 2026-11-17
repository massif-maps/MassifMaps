/*
 * A line run may be drawn smaller (text-fit-steps / text-fit-ratio) where its full size does not fit the street
 * or collides: the culler takes the largest size that is free, and goes back up when room comes.
 *
 * NOT covered here: how the smaller run looks (a device check), a tilted view, and the SDF edge ramp,
 * which the style batch sizes for the full size.
 */

#include "Label.h"
#include "LabelCuller.h"

#include "TestCheck.h"

#include <cmath>
#include <mutex>

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

    // A run of n glyphs is n units of line, centred on its anchor.
    std::vector<Font::Glyph> buildGlyphs(int count) {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back(0, Font::CR_CODEPOINT, baseGlyph, cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), cglib::vec2<float>(-count * 0.5f, 0));
        for (int i = 0; i < count; i++) {
            glyphs.emplace_back('A', 'A', baseGlyph, cglib::vec2<float>(1, 1), cglib::vec2<float>(0, 0), cglib::vec2<float>(1, 0));
        }
        return glyphs;
    }

    // A 10-glyph run on the line from -half to +half along x, allowed `steps` shrinks of 0.8 each.
    std::shared_ptr<Label> buildRun(float half, int steps, float ratio = 0.8f) {
        auto style = std::make_shared<TileLabel::Style>(LabelOrientation::LINE, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), ColorFunction(Color()), FloatFunction(0.0f), false, 1.0f, 1.0f, 0.0f, std::optional<Transform>(), std::shared_ptr<const GlyphMap>(), 27);
        style->fitSteps = steps;
        style->fitRatio = ratio;
        TileLabel tileLabel(1, 1, 0, buildGlyphs(10), cglib::vec2<float>(0, 0),
                            std::vector<cglib::vec2<float>>{ cglib::vec2<float>(-half, 0), cglib::vec2<float>(half, 0) },
                            style, TileLabel::PlacementInfo(0, 0, false, false), -1);
        auto label = std::make_shared<Label>(tileLabel, TileId(0, 0, 0), 0, cglib::mat4x4<double>::identity(), std::make_shared<FlatTransformer>());
        label->setActive(true);
        return label;
    }

    // A 2 x 2 icon that outranks the run, so it is placed first.
    std::shared_ptr<Label> buildBlocker(float x) {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back('O', 'O', baseGlyph, cglib::vec2<float>(2, 2), cglib::vec2<float>(-1, -1), cglib::vec2<float>(0, 0));
        auto style = std::make_shared<TileLabel::Style>(LabelOrientation::BILLBOARD_2D, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), ColorFunction(Color()), FloatFunction(0.0f), false, 1.0f, 1.0f, 0.0f, std::optional<Transform>(), std::shared_ptr<const GlyphMap>(), 27);
        TileLabel tileLabel(2, 2, 0, std::move(glyphs), cglib::vec2<float>(x, 0), std::vector<cglib::vec2<float>>(), style, TileLabel::PlacementInfo(9, 0, false, false), -1);
        auto label = std::make_shared<Label>(tileLabel, TileId(0, 0, 0), 0, cglib::mat4x4<double>::identity(), std::make_shared<FlatTransformer>());
        label->setActive(true);
        return label;
    }

    // An icon with its name on the right, on the left, or alone: the sides a POI tries in turn.
    std::shared_ptr<Label> buildPoi(float x, float y) {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back('O', 'O', baseGlyph, cglib::vec2<float>(2, 2), cglib::vec2<float>(-1, -1), cglib::vec2<float>(0, 0));
        glyphs.emplace_back(0, Font::CR_CODEPOINT, baseGlyph, cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0));
        for (int i = 0; i < 4; i++) {
            glyphs.emplace_back('A', 'A', baseGlyph, cglib::vec2<float>(1, 1), cglib::vec2<float>(0, 0), cglib::vec2<float>(1, 0));
        }
        std::vector<TileLabel::Variant> variants;
        variants.emplace_back(cglib::vec2<float>(1.5f, -0.5f), true);
        variants.emplace_back(cglib::vec2<float>(-5.5f, -0.5f), true);
        variants.emplace_back(cglib::vec2<float>(0, 0), false);
        auto style = std::make_shared<TileLabel::Style>(LabelOrientation::BILLBOARD_2D, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), ColorFunction(Color()), FloatFunction(0.0f), false, 1.0f, 1.0f, 0.0f, std::optional<Transform>(), std::shared_ptr<const GlyphMap>(), 27);
        TileLabel tileLabel(3, 3, 0, std::move(glyphs), cglib::vec2<float>(x, y), std::vector<cglib::vec2<float>>(), style, TileLabel::PlacementInfo(9, 0, false, false), -1, std::move(variants));
        auto label = std::make_shared<Label>(tileLabel, TileId(0, 0, 0), 0, cglib::mat4x4<double>::identity(), std::make_shared<FlatTransformer>());
        label->setActive(true);
        return label;
    }

    void cull(const std::vector<std::shared_ptr<Label>>& labels) {
        cglib::vec3<double> eye(0, 0, 100);
        cglib::mat4x4<double> cameraMatrix = cglib::lookat4_matrix(eye, cglib::vec3<double>(0, 0, 0), cglib::vec3<double>(0, 1, 0));
        cglib::mat4x4<double> projectionMatrix = cglib::perspective4_matrix(1.0, 1.0, 1.0, 1.0, 1000.0);
        ViewState viewState(projectionMatrix, cameraMatrix, 0, 0, 0, 1, 256);
        viewState.planarProjection = true;
        viewState.focusDistance = 100.0f;

        LabelCuller culler(1.0f);
        culler.setViewState(viewState);
        culler.setLabelViewDistance(0);
        std::mutex mutex;
        std::size_t cursor = 0;
        culler.process(labels, mutex, cursor);
    }
}

void testLabelFitSize() {
    {
        auto run = buildRun(4.5f, 0);
        cull({ run });
        TEST_CHECK(!run->isVisible(), "a 10 unit run does not fit a 9 unit street at its one size");
    }

    {
        auto run = buildRun(4.5f, 2);
        cull({ run });
        TEST_CHECK(run->isVisible() && run->getSizeStep() == 1, "and takes one shrink, the largest that fits, when the style allows two");
        TEST_CHECK(std::abs(run->getSizeFactor() - 0.8f) < 1e-6f, "that is 0.8 of its size");
    }

    {
        auto run = buildRun(3.5f, 1);
        cull({ run });
        TEST_CHECK(!run->isVisible() && run->getSizeStep() == 0, "a street that holds it at no allowed size leaves it hidden at the full one");
    }

    {
        auto run = buildRun(15.0f, 2);
        auto blocker = buildBlocker(7.0f);
        cull({ run, blocker });
        TEST_CHECK(blocker->isVisible() && run->isVisible() && run->getSizeStep() >= 1, "a run whose ends meet a name outranking it is drawn smaller, clear of it");
    }

    {
        auto run = buildRun(15.0f, 2);
        auto blocker = buildBlocker(7.0f);
        cull({ run, blocker });
        int small = run->getSizeStep();
        cull({ run });
        TEST_CHECK(small >= 1 && run->isVisible() && run->getSizeStep() == 0, "and goes back to the full size once nothing is there");
    }

    {
        auto run = buildRun(15.0f, 0);
        auto blocker = buildBlocker(7.0f);
        cull({ run, blocker });
        TEST_CHECK(!run->isVisible(), "a style with no steps still loses the collision");
    }

    {
        auto run = buildRun(4.5f, 2);
        cull({ run });
        bool steady = run->isVisible() && run->getSizeStep() == 1;
        for (int pass = 0; pass < 8; pass++) {
            cull({ run });
            steady = steady && run->isVisible() && run->getSizeStep() == 1;
        }
        TEST_CHECK(steady, "where only the small size fits, it stays small pass after pass, not trying the big one as fitting");
    }

    {
        auto run = buildRun(4.5f, 8, 0.95f);
        cull({ run });
        TEST_CHECK(run->isVisible() && run->getSizeStep() == 3, "with many fine steps it takes the largest that fits (0.95^3 of 10 is 8.6 on 9)");
        cull({ run });
        TEST_CHECK(run->isVisible() && run->getSizeStep() == 3, "and keeps it");
    }

    {
        auto run = buildRun(4.5f, 8, 0.95f);
        run->setSizeStep(8);
        cull({ run });
        TEST_CHECK(run->isVisible() && run->getSizeStep() == 3, "a run drawn at the smallest goes back up to the largest that fits");
    }

    {
        auto run = buildRun(15.0f, 0);
        auto poi = buildPoi(0.0f, 0.0f);
        run->setVisible(true);
        cull({ poi, run });
        TEST_CHECK(poi->isVisible() && poi->drawsText() && poi->getVariantIndex() == 0, "a POI keeps its name over a street name shown on the last pass, not dropping to its icon");
        TEST_CHECK(!run->isVisible(), "and the street name gives way");
    }

    {
        // The right side of the first name would cover the second one's icon, the left side is clear.
        auto first = buildPoi(0.0f, 0.0f);
        auto second = buildPoi(4.0f, 0.0f);
        cull({ first, second });
        TEST_CHECK(first->isVisible() && first->drawsText() && first->getVariantIndex() == 1 && second->isVisible(), "a name takes the side that leaves a later POI its icon");
    }


    {
        // A street name and a POI on one side of it, the run left at its full size or its smallest, over passes at one view.
        bool steady = true;
        bool same = true;
        int cases = 0;
        for (float half : { 6.0f, 10.0f }) {
            for (float x : { -10.0f, -7.0f, -4.0f, 0.0f, 4.0f }) {
                std::string states[2];
                for (int start = 0; start < 2; start++) {
                    auto run = buildRun(half, 4);
                    auto poi = buildPoi(x, 0.0f);
                    run->setSizeStep(start == 0 ? 0 : 4);
                    std::string previous;
                    for (int pass = 0; pass < 9; pass++) {
                        cull({ poi, run });
                        std::string state = std::string(run->isVisible() ? "run@" + std::to_string(run->getSizeStep()) : "run-") + (poi->isVisible() ? " poi@" + std::to_string(poi->getVariantIndex()) : " poi-");
                        steady = steady && (pass < 7 || state == previous);
                        previous = state;
                    }
                    states[start] = previous;
                }
                same = same && states[0] == states[1];
                cases++;
            }
        }
        TEST_CHECK(cases == 10 && same, "at rest a street name and a POI land the same whichever size the run was left at");
        TEST_CHECK(steady, "and once a hidden run has slid to its spot (SLIDE_RETRIES passes at most), passes at one view change nothing");
    }
}
