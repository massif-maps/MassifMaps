/*
 * A name gives way to a label already on screen. A POI with several sides (text-anchor list) used to
 * take the first free one with its name even when that covered a lower-ranked label shown on the
 * last pass, which then vanished mid-screen; the culler now tries the POI's other sides and its
 * icon alone first, and evicts only when nothing else fits.
 *
 * And a label a building partly hides gives way to any label in the clear, whatever the priorities:
 * a half-transparent street name no longer keeps a POI off the screen until it is fully hidden.
 *
 * And a label only an ancestor tile carries - drawn in place of a tile still loading - gives way to
 * every other label: on a zoom out it evicted labels that came back once the tile landed.
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
        TEST_CHECK(poi->isVisible() && poi->drawsText() && poi->getVariantIndex() == 1, "a new neighbour makes the higher-ranked POI name itself on the side that leaves it its icon");
        TEST_CHECK(other->isVisible(), "and is placed");
    }

    {
        auto poi = buildLabel(1, 10, cglib::vec2<float>(0, 0), buildIconAndName(), poiSides(true));
        auto rightOther = buildLabel(2, 0, right, buildIcon(), {});
        auto leftOther = buildLabel(3, 0, left, buildIcon(), {});
        cull({ poi, rightOther, leftOther });
        TEST_CHECK(poi->isVisible() && poi->drawsText() && poi->getVariantIndex() == 0, "with an icon on both sides the higher-ranked POI still names itself on its first, as greedy insertion always did");
        TEST_CHECK(!rightOther->isVisible() && leftOther->isVisible(), "and only the icon its name covers is not placed");
    }

    for (bool shown : { true, false }) {
        auto ancestor = buildLabel(1, 10, cglib::vec2<float>(0, 0), buildIcon(), {});
        auto poi = buildLabel(2, 0, cglib::vec2<float>(1, 0), buildIcon(), {});
        ancestor->setStandIn(true);
        poi->setVisible(shown);
        cull({ ancestor, poi });
        TEST_CHECK(poi->isVisible() && !ancestor->isVisible(), shown ? "a stand-in tile's label does not evict a label on screen, whatever its rank" : "nor does it take the room a real tile's label wants");
    }

    {
        auto ancestor = buildLabel(1, 0, cglib::vec2<float>(0, 0), buildIcon(), {});
        ancestor->setStandIn(true);
        cull({ ancestor });
        TEST_CHECK(ancestor->isVisible(), "alone, a stand-in tile's label is placed");
    }

    {
        // Building occlusion eases toward each frame's verdict: one frame's dip, as building tiles load, must not re-rank.
        auto street = buildLabel(1, 10, cglib::vec2<float>(0, 0), buildIconAndName(), {});
        auto poi = buildLabel(2, 0, cglib::vec2<float>(1, 0), buildIcon(), {});
        street->setVisible(true);
        street->setOcclusion(0.9f, 0.25f);
        cull({ street, poi });
        TEST_CHECK(street->isVisible() && !poi->isVisible(), "a name one easing step under clear keeps its rank over the POI");
        street->setOcclusion(0.8125f, 0.25f);
        cull({ street, poi });
        TEST_CHECK(poi->isVisible() && !street->isVisible(), "eased to one blocked tap of four, it gives way");
        street->setOcclusion(0.9f, 0.25f);
        TEST_CHECK(street->isPartlyOccluded(), "it stays ranked behind until nearly clear, not flipping at the threshold");
        street->setOcclusion(0.95f, 0.25f);
        TEST_CHECK(!street->isPartlyOccluded(), "and ranks again once nearly clear");
    }

    {
        // A label never judged against the buildings (just rebuilt) must not evict a settled label in the clear.
        auto settled = buildLabel(1, 0, cglib::vec2<float>(0, 0), buildIcon(), {});
        auto fresh = buildLabel(2, 10, cglib::vec2<float>(1, 0), buildIcon(), {});
        settled->setVisible(true);
        settled->setOcclusionStamp(1);
        cull({ settled, fresh });
        TEST_CHECK(settled->isVisible() && !fresh->isVisible(), "an unjudged label ranks after a judged one in the clear, whatever its priority");
        fresh->setOcclusionStamp(1);
        cull({ settled, fresh });
        TEST_CHECK(fresh->isVisible() && !settled->isVisible(), "once judged clear it ranks by priority again");
    }

    for (float occlusion : { 1.0f, 0.5f }) {
        auto street = buildLabel(1, 10, cglib::vec2<float>(0, 0), buildIconAndName(), {});
        auto poi = buildLabel(2, 0, cglib::vec2<float>(1, 0), buildIcon(), {});
        street->setVisible(true);
        street->setOcclusion(occlusion);
        cull({ street, poi });
        if (occlusion == 1.0f) {
            TEST_CHECK(street->isVisible() && !poi->isVisible(), "a street name in the clear outranks the POI under it");
        } else {
            TEST_CHECK(poi->isVisible() && !street->isVisible(), "a street name half hidden by a building gives way to the POI in the clear");
        }
    }

    // MapLibre's order among equal priorities: the anchor lower on the screen first, then the lower feature id; what was shown
    // before decides nothing.
    for (bool firstShown : { true, false }) {
        auto first = buildLabel(1, 5, cglib::vec2<float>(0, 0), buildIcon(), {});
        auto second = buildLabel(2, 5, cglib::vec2<float>(0, 0), buildIcon(), {});
        first->setVisible(firstShown);
        second->setVisible(!firstShown);
        cull({ first, second });
        TEST_CHECK(first->isVisible() && !second->isVisible(), firstShown ? "a tie at one height goes to the lower id" : "even when the other one was shown");
    }

    {
        auto high = buildLabel(1, 5, cglib::vec2<float>(0, 1), buildIcon(), {});
        auto low = buildLabel(2, 5, cglib::vec2<float>(0, 0), buildIcon(), {});
        cull({ high, low });
        TEST_CHECK(low->isVisible() && !high->isVisible(), "a tie between two heights goes to the one lower on the screen, whatever the ids");
    }

    {
        auto poi = buildLabel(1, 10, cglib::vec2<float>(0, 0), buildIconAndName(), poiSides(true));
        poi->setVariantIndex(1);
        poi->setVisible(true);
        cull({ poi });
        TEST_CHECK(poi->drawsText() && poi->getVariantIndex() == 1, "a POI keeps the side it held while it is free");
    }
}
