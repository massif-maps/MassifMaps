/*
 * "No elevation here" is not the same height as zero (Label::applyElevation).
 *
 * The bug this exists for, measured on the Crosscall in AlpiMaps: the SDK's label elevation provider
 * asked ElevationManager::getDisplayHeight, which returns 0 both for "sea level" and for "the grid
 * for this point is not cached" - and a grid is routinely evicted while its texture keeps rendering.
 * A label over 2000 m of alpine terrain was anchored at z=0, UNDER the ground, so the terrain
 * occlusion test correctly hid it; the caller then marked it clean and nothing ever asked again.
 * Symptoms: no labels at all with 3D terrain on, and with occlusion switched off the labels were
 * visibly under the ground. RenderStats said it in one line: elevUpdMs 1-180 ms with elevReanchor=0,
 * i.e. the sampler ran every frame and never moved a single label.
 *
 * So a non-finite sample means "no data": the vertex keeps the height it had, and applyElevation
 * returns false, which leaves the label un-anchored. Zero stays a legal height. The label is still
 * marked clean - only new elevation can change the answer, and that re-dirties it - and its RENDER
 * height comes from the GPU either way, so an un-anchored label is drawn in the right place.
 *
 * NOT covered here: that the provider actually returns NaN (TileRenderer, needs ElevationManager),
 * and that the anchoring sites mark the label clean and the occlusion test skips an un-anchored one -
 * GLTileRenderer is not in this link. Both are the device check named in the PR.
 */

#include "Label.h"
#include "LabelVariants.h"

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

    std::shared_ptr<Label> buildPointLabel() {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back(0, Font::CR_CODEPOINT, baseGlyph, cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0));
        glyphs.emplace_back('A', 'A', baseGlyph, cglib::vec2<float>(1, 1), cglib::vec2<float>(0, 0), cglib::vec2<float>(1, 0));
        auto style = std::make_shared<TileLabel::Style>(LabelOrientation::BILLBOARD_2D, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), ColorFunction(Color()), FloatFunction(0.0f), false, 1.0f, 1.0f, 0.0f, std::optional<Transform>(), std::shared_ptr<const GlyphMap>(), 27);
        TileLabel tileLabel(1, 1, 0, glyphs, cglib::vec2<float>(0, 0), std::vector<cglib::vec2<float>>(),
                            style, TileLabel::PlacementInfo(0, 0, false, false), -1, std::vector<TileLabel::Variant>());
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

    // The label's anchor height, which is what the occlusion test compares against the terrain.
    bool anchorHeight(const std::shared_ptr<Label>& label, double& z) {
        cglib::vec3<double> center(0, 0, 0);
        if (!label->calculateCenter(center)) {
            return false;
        }
        z = center(2);
        return true;
    }

    // The provider anchors a point, not a bare height: on a globe "up" is radial, so the whole
    // position comes back. A non-finite one means it has no data there.
    std::function<cglib::vec3<double>(const cglib::vec3<double>&)> constantHeight(double height) {
        return [height](const cglib::vec3<double>& pos) {
            return cglib::vec3<double>(pos(0), pos(1), height);
        };
    }
}

void testLabelElevationAnchor() {
    ViewState viewState = buildViewState();

    // A height that IS known moves the anchor, and the label is complete: nothing to ask again.
    {
        std::shared_ptr<Label> label = buildPointLabel();
        label->updatePlacement(viewState);
        TEST_CHECK(label->updateElevation(constantHeight(250.0)), "a known height anchors the label");
        double z = 0;
        TEST_CHECK(anchorHeight(label, z) && std::abs(z - 250.0) < 1e-6, "and puts it on the terrain");
    }

    // The bug, in one line: no data must not read as sea level. The anchor KEEPS 250 m rather than
    // being buried at 0, and the label reports incomplete.
    {
        std::shared_ptr<Label> label = buildPointLabel();
        label->updatePlacement(viewState);
        label->updateElevation(constantHeight(250.0));
        bool complete = label->updateElevation(constantHeight(std::numeric_limits<double>::quiet_NaN()));
        TEST_CHECK(!complete, "a missing height reports the label incomplete");
        double z = 0;
        TEST_CHECK(anchorHeight(label, z) && std::abs(z - 250.0) < 1e-6, "and leaves the anchor where it was, not at 0");
    }

    // Zero is still a legal height - a label at sea level must anchor at sea level, and count as done.
    {
        std::shared_ptr<Label> label = buildPointLabel();
        label->updatePlacement(viewState);
        label->updateElevation(constantHeight(250.0));
        TEST_CHECK(label->updateElevation(constantHeight(0.0)), "an explicit 0 is a height, not a miss");
        double z = 0;
        TEST_CHECK(anchorHeight(label, z) && std::abs(z) < 1e-6, "and anchors the label at sea level");
    }

    // An infinite height is a miss too: the provider has no way to mean "infinitely high".
    {
        std::shared_ptr<Label> label = buildPointLabel();
        label->updatePlacement(viewState);
        label->updateElevation(constantHeight(250.0));
        TEST_CHECK(!label->updateElevation(constantHeight(std::numeric_limits<double>::infinity())), "an infinite height is a miss");
        double z = 0;
        TEST_CHECK(anchorHeight(label, z) && std::abs(z - 250.0) < 1e-6, "and does not move the anchor");
    }

    // A label whose height was never resolved is NOT anchored, so the terrain occlusion test leaves
    // it alone (GLTileRenderer::updateLabel) rather than hiding it under ground it is not behind.
    // mapbox never gates a symbol on elevation availability - it elevates in the vertex shader.
    {
        std::shared_ptr<Label> label = buildPointLabel();
        label->updatePlacement(viewState);
        TEST_CHECK(!label->isElevationAnchored(), "a fresh label is not anchored");
        TEST_CHECK(!label->updateElevation(constantHeight(std::numeric_limits<double>::quiet_NaN())), "and a miss does not anchor it");
        TEST_CHECK(!label->isElevationAnchored(), "so the occlusion test must not judge it");
        TEST_CHECK(label->updateElevation(constantHeight(410.0)), "the height arriving anchors it");
        TEST_CHECK(label->isElevationAnchored(), "and from then on it may be judged");
        // Sticky: a later miss does not un-anchor a label that already has a real height.
        label->updateElevation(constantHeight(std::numeric_limits<double>::quiet_NaN()));
        TEST_CHECK(label->isElevationAnchored(), "a later miss does not un-anchor it");
    }

    // The deck bit rides in attribs[3] alongside the offset mode, and labelVsh reads them separately:
    // bit 0 is how to read the glyph offset, bit 1 is "this height is absolute, do not take the
    // terrain's". Sharing a bit would silently make a deck label take the ground - see ShaderFlagTest
    // for the same class of bug in the shader flags.
    {
        std::shared_ptr<Label> label = buildPointLabel();
        label->updatePlacement(viewState);
        label->updateElevation(constantHeight(300.0));

        auto attribMode = [&](std::int8_t& mode) {
            VertexArray<cglib::vec3<float>> vertices, offsets, normals;
            VertexArray<cglib::vec2<std::int16_t>> texCoords;
            VertexArray<cglib::vec4<std::int8_t>> attribs;
            VertexArray<std::uint16_t> indices;
            if (!label->calculateVertexData(1.0f, viewState, 0, -1, vertices, offsets, normals, texCoords, attribs, indices) || attribs.size() == 0) {
                return false;
            }
            mode = attribs[0](3);
            return true;
        };

        std::int8_t ground = 0, deck = 0;
        TEST_CHECK(!label->hasAbsoluteHeight(), "a label is terrain-relative by default");
        TEST_CHECK(attribMode(ground), "a ground label builds vertex data");
        TEST_CHECK((ground & 2) == 0, "and carries no absolute-height bit");

        label->setAbsoluteHeight(true);
        TEST_CHECK(label->hasAbsoluteHeight(), "a deck label says so");
        TEST_CHECK(attribMode(deck), "and still builds vertex data");
        TEST_CHECK((deck & 2) != 0, "with the absolute-height bit set");
        TEST_CHECK((deck & 1) == (ground & 1), "and the offset mode in bit 0 untouched");
    }

    // The floating POI: the first anchor of a 2D/3D switch is taken from a COARSE ancestor tile -
    // finite, and metres over the real ground - and the exact tile lands frames later. An already
    // anchored label must still take the better height, and must follow the exaggeration ramp step
    // by step, or it stays in the air. What feeds it those samples is TileRenderer, which has to
    // invalidate on the elevation TEXTURE landing and on the exaggeration, not only on the grid.
    {
        std::shared_ptr<Label> label = buildPointLabel();
        label->updatePlacement(viewState);
        TEST_CHECK(label->updateElevation(constantHeight(127.0)), "the coarse ancestor anchors the label");
        TEST_CHECK(label->isElevationAnchored(), "so it counts as anchored from there on");
        TEST_CHECK(label->updateElevation(constantHeight(34.0)), "the exact tile re-anchors it");
        double z = 0;
        TEST_CHECK(anchorHeight(label, z) && std::abs(z - 34.0) < 1e-6, "down onto the ground it stands on");
        for (int step = 1; step <= 4; step++) {
            double ramped = 34.0 * step * 0.25;
            TEST_CHECK(label->updateElevation(constantHeight(ramped)), "every ramp step re-anchors the label");
            TEST_CHECK(anchorHeight(label, z) && std::abs(z - ramped) < 1e-6, "at the exaggeration of that step");
        }
    }

    // Repeated misses never drift: the anchor is the last KNOWN height however often it is asked.
    {
        std::shared_ptr<Label> label = buildPointLabel();
        label->updatePlacement(viewState);
        label->updateElevation(constantHeight(1800.0));
        for (int i = 0; i < 20; i++) {
            if (label->updateElevation(constantHeight(std::numeric_limits<double>::quiet_NaN()))) {
                TEST_CHECK(false, "a miss stays a miss however often it is retried");
                return;
            }
        }
        double z = 0;
        TEST_CHECK(anchorHeight(label, z) && std::abs(z - 1800.0) < 1e-6, "twenty misses leave the anchor on the terrain");
    }
}
