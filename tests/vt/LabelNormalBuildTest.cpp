/*
 * Whether a label builds vertex normals at all (Label::calculateVertexData).
 *
 * aVertexNormal exists in labelVsh only under LIGHTING_VSH/LIGHTING_FSH, and the SDK compiles those
 * in for a NON-planar projection alone (TileRenderer::initializeRenderer sets the 2D lighting shader
 * only when the projection surface is not planar). renderLabelBatch already knew that - it uploads
 * the normal buffer under `if (_lightingShader2D)`.
 *
 * The build did not: it filled one normal PER VERTEX, per label, per frame, on every planar map, and
 * threw all of it away. Measured on the Crosscall, 3589 labels an interval.
 *
 * So the caller says whether anything will read them. The rest of the vertex data is unaffected -
 * that is the half of this that could break a label.
 */

#include "Label.h"
#include "LabelVariants.h"

#include "TestCheck.h"

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

    struct VertexData {
        VertexArray<cglib::vec3<float>> vertices, offsets, normals;
        VertexArray<cglib::vec2<std::int16_t>> texCoords;
        VertexArray<cglib::vec4<std::int8_t>> attribs;
        VertexArray<std::uint16_t> indices;

        bool build(const std::shared_ptr<Label>& label, const ViewState& viewState, bool buildNormals) {
            return label->calculateVertexData(1.0f, viewState, 0, -1, vertices, offsets, normals, texCoords, attribs, indices,
                                              Label::DrawPass::ALL, LabelPlateIndices(), -1, -1, -1, buildNormals);
        }
    };
}

void testLabelNormalBuild() {
    ViewState viewState = buildViewState();

    // Lit (non-planar): the normals are there, one per vertex, as the shader's attribute needs.
    VertexData lit;
    {
        std::shared_ptr<Label> label = buildPointLabel();
        label->updatePlacement(viewState);
        TEST_CHECK(lit.build(label, viewState, true), "a label builds its vertex data");
        TEST_CHECK(lit.normals.size() == lit.vertices.size(), "and a normal for every vertex when something reads them");
        TEST_CHECK(lit.vertices.size() > 0, "which is not a vacuous check");
    }

    // Planar, the common case: no normals at all, and EVERYTHING ELSE identical. The second half is
    // the point - skipping the fill must not shift a glyph, a texture coordinate or an index.
    {
        std::shared_ptr<Label> label = buildPointLabel();
        label->updatePlacement(viewState);
        VertexData unlit;
        TEST_CHECK(unlit.build(label, viewState, false), "a label still builds its vertex data unlit");
        TEST_CHECK(unlit.normals.size() == 0, "and builds no normals nothing would read");

        TEST_CHECK(unlit.vertices.size() == lit.vertices.size(), "the same vertex count");
        TEST_CHECK(unlit.offsets.size() == lit.offsets.size(), "the same offsets");
        TEST_CHECK(unlit.texCoords.size() == lit.texCoords.size(), "the same texture coordinates");
        TEST_CHECK(unlit.attribs.size() == lit.attribs.size(), "the same attributes");
        TEST_CHECK(unlit.indices.size() == lit.indices.size(), "the same indices");

        for (std::size_t i = 0; i < unlit.vertices.size(); i++) {
            if (!(unlit.vertices[i] == lit.vertices[i]) || !(unlit.offsets[i] == lit.offsets[i])) {
                TEST_CHECK(false, "and every vertex in the same place");
                return;
            }
        }
        TEST_CHECK(true, "and every vertex in the same place");
    }
}
