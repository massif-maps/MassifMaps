/*
 * A placement pass over a view that has not moved keeps what the last one decided. The renderer
 * runs one whenever the cull says the tile set may have changed, and the cull compares the view
 * matrix bit for bit, so a camera event that changes nothing still starts a pass; if a pass could
 * flip a label on its own, every one of them would blink it.
 *
 * Random POIs (two sides, with and without an icon-only layout), icons and names collide on a flat
 * view, start from any visible/opacity/variant state, and are passed over 40 times with the opacity
 * easing the draw does between passes. From the 12th pass on, nothing may change visibility or lose
 * its name. The view is also shifted by float-noise amounts each pass.
 *
 * NOT covered here: what changes between passes on a device - building occlusion verdicts, a label
 * rebuilt on a tile-set change, a line run re-laid on a new view matrix - and a view that really
 * moves, where a label at the edge of a collision flips as it should.
 */

#include "Label.h"
#include "LabelCuller.h"

#include "TestCheck.h"

#include <algorithm>
#include <mutex>
#include <random>

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

    /** A 2x2 icon on the anchor, then a four-glyph name starting at the pen the variant shifts. */
    std::vector<Font::Glyph> buildIconAndName() {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back('O', 'O', baseGlyph, cglib::vec2<float>(2, 2), cglib::vec2<float>(-1, -1), cglib::vec2<float>(0, 0));
        glyphs.emplace_back(0, Font::CR_CODEPOINT, baseGlyph, cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0));
        for (int i = 0; i < 4; i++) {
            glyphs.emplace_back('A', 'A', baseGlyph, cglib::vec2<float>(1, 1), cglib::vec2<float>(0, 0), cglib::vec2<float>(1, 0));
        }
        return glyphs;
    }

    /** An icon alone, the size of the POI's. */
    std::vector<Font::Glyph> buildIcon() {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back('O', 'O', baseGlyph, cglib::vec2<float>(2, 2), cglib::vec2<float>(-1, -1), cglib::vec2<float>(0, 0));
        return glyphs;
    }

    std::shared_ptr<const TileLabel::Style> buildStyle() {
        return std::make_shared<TileLabel::Style>(LabelOrientation::BILLBOARD_2D, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), ColorFunction(Color()), FloatFunction(0.0f), false, 1.0f, 1.0f, 0.0f, std::optional<Transform>(), std::shared_ptr<const GlyphMap>(), 27);
    }

    std::shared_ptr<Label> buildLabel(long long id, int priority, const cglib::vec2<float>& position, std::vector<Font::Glyph> glyphs, std::vector<TileLabel::Variant> variants) {
        TileLabel tileLabel(id, id, 0, std::move(glyphs), position, std::vector<cglib::vec2<float>>(), buildStyle(),
                            TileLabel::PlacementInfo(priority, 0, false, false), -1, std::move(variants));
        auto label = std::make_shared<Label>(tileLabel, TileId(0, 0, 0), 0, cglib::mat4x4<double>::identity(), std::make_shared<FlatTransformer>());
        label->setActive(true);
        return label;
    }

    /** Name right of the icon, name left of it, and - unless asked not to - the icon alone. */
    std::vector<TileLabel::Variant> poiSides(bool iconOnly) {
        std::vector<TileLabel::Variant> variants;
        variants.emplace_back(cglib::vec2<float>(1.5f, -0.5f), true);
        variants.emplace_back(cglib::vec2<float>(-5.5f, -0.5f), true);
        if (iconOnly) {
            variants.emplace_back(cglib::vec2<float>(0, 0), false);
        }
        return variants;
    }

    ViewState buildViewState(double dx, double dy) {
        cglib::vec3<double> eye(dx, dy, 100);
        cglib::mat4x4<double> cameraMatrix = cglib::lookat4_matrix(eye, cglib::vec3<double>(dx, dy, 0), cglib::vec3<double>(0, 1, 0));
        cglib::mat4x4<double> projectionMatrix = cglib::perspective4_matrix(1.0, 1.0, 1.0, 1.0, 1000.0);
        ViewState viewState(projectionMatrix, cameraMatrix, 0, 0, 0, 1, 256);
        viewState.planarProjection = true;
        viewState.focusDistance = 100.0f;
        return viewState;
    }

    // Passes from the first one that has to be settled; a flip from there on is a label blinking on its own.
    const int SETTLED_PASS = 12;
    const int PASSES = 40;

    /** Scenarios of this seed range that still flip a label from SETTLED_PASS on, with the view shifted by `noise` world units. */
    int countUnstable(int scenarios, double noise) {
        int unstable = 0;
        for (int seed = 0; seed < scenarios; seed++) {
            std::mt19937 rng(seed);
            auto rnd = [&rng](int n) { return static_cast<int>(rng() % n); };
            std::vector<std::shared_ptr<Label>> labels;
            for (int i = 0, count = 4 + rnd(10); i < count; i++) {
                cglib::vec2<float> position(rnd(1300007) / 100000.0f - 6, rnd(500009) / 100000.0f - 2);
                int priority = rnd(3);
                int kind = rnd(3);
                std::shared_ptr<Label> label = kind == 0 ? buildLabel(i + 1, priority, position, buildIconAndName(), poiSides(true))
                    : kind == 1 ? buildLabel(i + 1, priority, position, buildIcon(), {})
                    : buildLabel(i + 1, priority, position, buildIconAndName(), poiSides(false));
                label->setOcclusionStamp(1);
                label->setVisible(rnd(2) != 0);
                label->setOpacity(rnd(11) / 10.0f);
                label->setTextOpacity(rnd(11) / 10.0f);
                labels.push_back(label);
            }
            // The renderer hands the culler its labels most important first.
            std::stable_sort(labels.begin(), labels.end(), [](const std::shared_ptr<Label>& label1, const std::shared_ptr<Label>& label2) {
                if (label1->getPriority() != label2->getPriority()) {
                    return label1->getPriority() > label2->getPriority();
                }
                return label1->getGlobalId() < label2->getGlobalId();
            });

            std::vector<std::pair<bool, bool>> previous;
            bool flipped = false;
            for (int pass = 0; pass < PASSES && !flipped; pass++) {
                LabelCuller culler(1.0f);
                culler.setViewState(buildViewState(noise * (rnd(2001) / 1000.0 - 1.0), noise * (rnd(2001) / 1000.0 - 1.0)));
                culler.setLabelViewDistance(0);
                std::mutex mutex;
                std::size_t cursor = 0;
                culler.process(labels, mutex, cursor);

                std::vector<std::pair<bool, bool>> current;
                for (const std::shared_ptr<Label>& label : labels) {
                    current.emplace_back(label->isVisible(), label->drawsText());
                }
                flipped = pass >= SETTLED_PASS && current != previous;
                previous = std::move(current);

                // What the draw does between two passes.
                for (const std::shared_ptr<Label>& label : labels) {
                    float step = 0.2f;
                    bool shown = label->isVisible();
                    label->setOpacity(shown ? std::min(1.0f, label->getOpacity() + step) : std::max(0.0f, label->getOpacity() - step));
                    label->setTextOpacity(shown && label->drawsText() ? std::min(1.0f, label->getTextOpacity() + step) : std::max(0.0f, label->getTextOpacity() - step));
                }
            }
            unstable += flipped ? 1 : 0;
        }
        return unstable;
    }
}

void testLabelStable() {
    TEST_CHECK(countUnstable(1500, 0.0) == 0, "a placement pass over an unchanged view keeps every label as the last one left it");
    TEST_CHECK(countUnstable(1500, 1.0e-6) == 0, "and so does one over a view that moved by float noise");
}
