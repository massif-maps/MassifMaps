/*
 * A terrain occlusion verdict stands while the label's height cannot be judged. The culler used to
 * skip the test for a label whose elevation was unknown or owed a re-anchor and treat it as in the
 * clear, so a label behind a ridge took a slot - evicting the label next to it - until its height
 * landed, then gave it back: both blinked while tiles and elevation streamed in.
 *
 * NOT covered here: the GL thread's per-frame fade, which keeps the verdict the same way
 * (GLTileRenderer::updateLabel), and the carry-over to a label rebuilt on a tile-set change.
 */

#include "Label.h"
#include "LabelCuller.h"

#include "TestCheck.h"

#include <cmath>
#include <limits>
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

    std::shared_ptr<Label> buildLabel(long long id, int priority, const cglib::vec2<float>& position) {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back('O', 'O', baseGlyph, cglib::vec2<float>(2, 2), cglib::vec2<float>(-1, -1), cglib::vec2<float>(0, 0));
        auto style = std::make_shared<TileLabel::Style>(LabelOrientation::BILLBOARD_2D, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), ColorFunction(Color()), FloatFunction(0.0f), false, 1.0f, 1.0f, 0.0f, std::optional<Transform>(), std::shared_ptr<const GlyphMap>(), 27);
        TileLabel tileLabel(id, id, 0, std::move(glyphs), position, std::vector<cglib::vec2<float>>(), style,
                            TileLabel::PlacementInfo(priority, 0, false, false), -1, std::vector<TileLabel::Variant>());
        auto label = std::make_shared<Label>(tileLabel, TileId(0, 0, 0), 0, cglib::mat4x4<double>::identity(), std::make_shared<FlatTransformer>());
        label->setActive(true);
        return label;
    }

    /** The renderer's anchoring: every height known (or none), then marked clean. */
    void anchor(Label& label, bool known) {
        label.updateElevation([known](const cglib::vec3<double>& pos, int) {
            return known ? pos : cglib::vec3<double>(pos(0), pos(1), std::numeric_limits<double>::quiet_NaN());
        });
        label.setElevationDirty(false);
    }

    /** One placement pass; the ridge hides whatever stands at x < 0.5. */
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
        culler.setOcclusionTest([](const cglib::vec3<double>& pos) { return pos(0) < 0.5; });
        std::mutex mutex;
        std::size_t cursor = 0;
        culler.process(labels, mutex, cursor);
    }
}

void testLabelOcclusionKeep() {
    for (bool stale : { false, true }) {
        auto ridge = buildLabel(1, 10, cglib::vec2<float>(0, 0));
        auto poi = buildLabel(2, 0, cglib::vec2<float>(1, 0));
        anchor(*ridge, true);
        anchor(*poi, true);
        cull({ ridge, poi });
        TEST_CHECK(!ridge->isVisible() && ridge->isTerrainOccluded(), "a label behind the ridge is hidden and its verdict kept");
        TEST_CHECK(poi->isVisible(), "the lower-ranked label beside it takes the room");

        if (stale) {
            ridge->setElevationDirty(true); // a re-anchor owed: the elevation under it changed
        } else {
            anchor(*ridge, false); // its height went missing: a coarser elevation tile is all there is
        }
        cull({ ridge, poi });
        TEST_CHECK(!ridge->isVisible(), stale ? "a label owed a re-anchor stays hidden behind the ridge" : "a label whose height is unknown stays hidden behind the ridge");
        TEST_CHECK(poi->isVisible(), "and the label beside it is not evicted by it");
    }

    {
        auto label = buildLabel(1, 0, cglib::vec2<float>(0, 0));
        anchor(*label, false);
        cull({ label });
        TEST_CHECK(label->isVisible() && !label->isTerrainOccluded(), "a label never judged is not hidden for its unknown height");
    }
}
