/*
 * A name gives way to a label already on screen. A POI with several sides (text-anchor list) used to
 * take the first free one with its name even when that covered a lower-ranked label shown on the
 * last pass, which then vanished mid-screen; the culler now tries the POI's other sides and its
 * icon alone first, and evicts only when nothing else fits.
 *
 * NOT covered here: the rotation/pan churn this cuts (measured on the style preview, see
 * docs/internals/rendering/06-labels.mdx), and labels of different layers, which reserve nothing
 * across a layer boundary.
 */

#include "Label.h"
#include "LabelCuller.h"

#include "TestCheck.h"

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

    ViewState buildViewState() {
        cglib::vec3<double> eye(0, 0, 100);
        cglib::mat4x4<double> cameraMatrix = cglib::lookat4_matrix(eye, cglib::vec3<double>(0, 0, 0), cglib::vec3<double>(0, 1, 0));
        cglib::mat4x4<double> projectionMatrix = cglib::perspective4_matrix(1.0, 1.0, 1.0, 1.0, 1000.0);
        ViewState viewState(projectionMatrix, cameraMatrix, 0, 0, 0, 1, 256);
        viewState.planarProjection = true;
        viewState.focusDistance = 100.0f;
        return viewState;
    }

    /** One placement pass over the labels, highest priority first as the renderer orders them. */
    void cull(const std::vector<std::shared_ptr<Label>>& labels) {
        LabelCuller culler(1.0f);
        culler.setViewState(buildViewState());
        culler.setLabelViewDistance(0);
        std::mutex mutex;
        std::size_t cursor = 0;
        culler.process(labels, mutex, cursor);
    }
}

void testLabelYield() {
    const cglib::vec2<float> right(4.0f, 0.0f), left(-4.0f, 0.0f);

    {
        auto poi = buildLabel(1, 10, cglib::vec2<float>(0, 0), buildIconAndName(), poiSides(true));
        auto other = buildLabel(2, 0, right, buildIcon(), {});
        cull({ poi, other });
        TEST_CHECK(poi->isVisible() && poi->drawsText() && poi->getVariantIndex() == 0, "a new neighbour does not stop the higher-ranked POI naming itself on its first side");
        TEST_CHECK(!other->isVisible(), "the neighbour its name covers is not placed, as greedy insertion always did");
    }

    {
        auto poi = buildLabel(1, 10, cglib::vec2<float>(0, 0), buildIconAndName(), poiSides(true));
        auto other = buildLabel(2, 0, right, buildIcon(), {});
        other->setVisible(true);
        cull({ poi, other });
        TEST_CHECK(other->isVisible(), "a label shown on the last pass keeps its place against a higher-ranked name");
        TEST_CHECK(poi->isVisible() && poi->drawsText() && poi->getVariantIndex() == 1, "the POI names itself on its free side instead");
    }

    {
        auto poi = buildLabel(1, 10, cglib::vec2<float>(0, 0), buildIconAndName(), poiSides(true));
        auto east = buildLabel(2, 0, right, buildIcon(), {});
        auto west = buildLabel(3, 0, left, buildIcon(), {});
        east->setVisible(true);
        west->setVisible(true);
        cull({ poi, east, west });
        TEST_CHECK(east->isVisible() && west->isVisible(), "both neighbours shown on the last pass stay when every side of the name covers one");
        TEST_CHECK(poi->isVisible() && !poi->drawsText(), "the POI is drawn as its icon alone rather than evicting either");
    }

    {
        auto poi = buildLabel(1, 10, cglib::vec2<float>(0, 0), buildIconAndName(), poiSides(false));
        auto east = buildLabel(2, 0, right, buildIcon(), {});
        auto west = buildLabel(3, 0, left, buildIcon(), {});
        east->setVisible(true);
        west->setVisible(true);
        cull({ poi, east, west });
        TEST_CHECK(poi->isVisible() && poi->getVariantIndex() == 0, "a POI with no icon-only layout is not hidden for yielding: it takes its first side");
        TEST_CHECK(!east->isVisible() && west->isVisible(), "and evicts only the neighbour that side covers");
    }

}
