/*
 * Labels of one group stay the group's minimum distance apart, and only that: two labels of a group
 * with no minimum distance are as free as two ungrouped ones. The culler used to hide the second
 * wherever it was on screen, because its group test skipped the bounds check the overlap test relies on.
 *
 * NOT covered here: tilted, non axis-aligned envelopes (the separating-axis path), and the CartoCSS
 * side - a symbolizer gives a label a group only with a minimum distance above 0.
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

    std::shared_ptr<Label> buildIcon(long long id, long long groupId, float minimumGroupDistance, const cglib::vec2<float>& position) {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back('O', 'O', baseGlyph, cglib::vec2<float>(2, 2), cglib::vec2<float>(-1, -1), cglib::vec2<float>(0, 0));
        auto style = std::make_shared<TileLabel::Style>(LabelOrientation::BILLBOARD_2D, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), ColorFunction(Color()), FloatFunction(0.0f), false, 1.0f, 1.0f, 0.0f, std::optional<Transform>(), std::shared_ptr<const GlyphMap>(), 27);
        TileLabel tileLabel(id, id, groupId, std::move(glyphs), position, std::vector<cglib::vec2<float>>(), style,
                            TileLabel::PlacementInfo(10 - static_cast<int>(id), minimumGroupDistance, false, false), -1);
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

void testLabelGroupDistance() {
    const cglib::vec2<float> apart(8.0f, 0.0f);

    {
        auto first = buildIcon(1, 0, 0, cglib::vec2<float>(0, 0));
        auto second = buildIcon(2, 0, 0, apart);
        cull({ first, second });
        TEST_CHECK(first->isVisible() && second->isVisible(), "two ungrouped labels 8 units apart are both placed");
    }

    {
        auto first = buildIcon(1, 1, 0, cglib::vec2<float>(0, 0));
        auto second = buildIcon(2, 1, 0, apart);
        cull({ first, second });
        TEST_CHECK(first->isVisible() && second->isVisible(), "so are two labels of one group with no minimum distance");
    }

    {
        auto first = buildIcon(1, 1, 1, cglib::vec2<float>(0, 0));
        auto second = buildIcon(2, 1, 1, apart);
        cull({ first, second });
        TEST_CHECK(first->isVisible() && second->isVisible(), "and of one group with a distance smaller than their gap");
    }

    {
        auto first = buildIcon(1, 1, 1000, cglib::vec2<float>(0, 0));
        auto second = buildIcon(2, 1, 1000, apart);
        cull({ first, second });
        TEST_CHECK(first->isVisible() && !second->isVisible(), "a group distance wider than the screen still keeps the second label out");
    }

    {
        auto first = buildIcon(1, 1, 1000, cglib::vec2<float>(0, 0));
        auto second = buildIcon(2, 2, 1000, apart);
        cull({ first, second });
        TEST_CHECK(first->isVisible() && second->isVisible(), "but only against its own group");
    }
}
