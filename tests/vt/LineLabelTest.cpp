/*
 * Line labels: the glyph run may not leave the line it names (see Label::buildLineVertexData).
 * A run given room past either end was laid out on a straight CONTINUATION of the geometry, so a
 * street name ran off the end of its road - and off the curve where the road bent away.
 *
 * Label.cpp is the one vt source in this link. It reaches no font, no tile builder and no
 * renderer, so a label can be built here out of glyph metrics alone.
 *
 * A run whose span turns more than maplibre's text-max-angle is dropped (Label::checkPlacementMaxAngle):
 * a road's jog used to be averaged into one slanted edge and the name laid across it.
 *
 * NOT covered here: what the run LOOKS like (the atlas and the shader are device checks), the
 * projected path of a 'line-billboard' run (same layout code, but its line is the one the camera
 * projects, which needs a tilted view to differ), and collision, which is LabelCuller's.
 */

#include "Label.h"

#include "TestCheck.h"

#include <cmath>
#include <limits>

using namespace massif::vt;

namespace {
    // Tile coordinates ARE world coordinates here, flat at z = 0: the layout is what is under
    // test, not the tile-to-world transform.
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

    // One square glyph per unit of line: with size, zoomScale and the style scale all 1, a glyph
    // unit IS a world unit, so a run of n glyphs needs n units of line and the numbers below can
    // be read off the geometry.
    // alignOffset is the CR's x advance, where TextFormatter puts the alignment: -count/2 when centred.
    std::vector<Font::Glyph> buildGlyphs(int count, float alignOffset = 0.0f) {
        GlyphMap::Glyph baseGlyph(GlyphMap::GlyphMode::SDF, 0, 0, 1, 1, cglib::vec2<float>(0, 0));
        std::vector<Font::Glyph> glyphs;
        glyphs.emplace_back(0, Font::CR_CODEPOINT, baseGlyph, cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), cglib::vec2<float>(alignOffset, 0));
        for (int i = 0; i < count; i++) {
            glyphs.emplace_back('A', 'A', baseGlyph, cglib::vec2<float>(1, 1), cglib::vec2<float>(0, 0), cglib::vec2<float>(1, 0));
        }
        return glyphs;
    }

    std::shared_ptr<TileLabel::Style> buildStyle() {
        return std::make_shared<TileLabel::Style>(LabelOrientation::LINE, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), ColorFunction(Color()), FloatFunction(0.0f), false, 1.0f, 1.0f, 0.0f, std::optional<Transform>(), std::shared_ptr<const GlyphMap>(), 27);
    }

    // Straight west-east line from (0, 0) to (lineLength, 0), with the label's own anchor at
    // anchorX - the anchor a point label snapped onto the line brings with it.
    std::shared_ptr<Label> buildLineLabel(int glyphCount, float lineLength, float anchorX) {
        TileLabel tileLabel(1, 1, 0, buildGlyphs(glyphCount), cglib::vec2<float>(anchorX, 0),
                            std::vector<cglib::vec2<float>>{ cglib::vec2<float>(0, 0), cglib::vec2<float>(lineLength, 0) },
                            buildStyle(), TileLabel::PlacementInfo(0, 0, false, false), -1);
        return std::make_shared<Label>(tileLabel, TileId(0, 0, 0), 0, cglib::mat4x4<double>::identity(), std::make_shared<FlatTransformer>());
    }

    // The same straight line, but carried by many vertices - a street the tileset stores as a
    // string of short segments rather than as two endpoints. The placement smooths it before
    // laying glyphs out, and the smoothing is what this exercises.
    std::shared_ptr<Label> buildDenseLineLabel(int glyphCount, float lineLength, int vertexCount, float anchorX) {
        std::vector<cglib::vec2<float>> vertices;
        for (int i = 0; i < vertexCount; i++) {
            vertices.emplace_back(lineLength * i / (vertexCount - 1), 0.0f);
        }
        TileLabel tileLabel(1, 1, 0, buildGlyphs(glyphCount), cglib::vec2<float>(anchorX, 0), vertices,
                            buildStyle(), TileLabel::PlacementInfo(0, 0, false, false), -1);
        return std::make_shared<Label>(tileLabel, TileId(0, 0, 0), 0, cglib::mat4x4<double>::identity(), std::make_shared<FlatTransformer>());
    }

    std::shared_ptr<Label> buildPolylineLabel(const std::vector<Font::Glyph>& glyphs, const std::vector<cglib::vec2<float>>& vertices, const cglib::vec2<float>& anchor, float maxAngle = 0.785398f) {
        std::shared_ptr<TileLabel::Style> style = buildStyle();
        style->maxAngle = maxAngle;
        TileLabel tileLabel(1, 1, 0, glyphs, anchor, vertices, style, TileLabel::PlacementInfo(0, 0, false, false), -1);
        return std::make_shared<Label>(tileLabel, TileId(0, 0, 0), 0, cglib::mat4x4<double>::identity(), std::make_shared<FlatTransformer>());
    }

    // A polyline through the corners, with a vertex every `step` as a tileset stores a street.
    std::vector<cglib::vec2<float>> densify(const std::vector<cglib::vec2<float>>& corners, float step) {
        std::vector<cglib::vec2<float>> vertices { corners.front() };
        for (std::size_t i = 1; i < corners.size(); i++) {
            int count = std::max(1, static_cast<int>(std::ceil(cglib::length(corners[i] - corners[i - 1]) / step)));
            for (int j = 1; j <= count; j++) {
                vertices.push_back(corners[i - 1] + (corners[i] - corners[i - 1]) * (static_cast<float>(j) / count));
            }
        }
        return vertices;
    }

    bool isPlaced(const std::shared_ptr<Label>& label, const ViewState& viewState) {
        label->updatePlacement(viewState);
        std::array<cglib::vec3<float>, 4> envelope;
        return label->calculateEnvelope(viewState, envelope);
    }

    // Top-down camera over the middle of the line, far enough that the whole line is in frustum.
    ViewState buildViewState(float lineLength, float centerY = 0.0f) {
        cglib::vec3<double> eye(lineLength * 0.5, centerY, 100);
        cglib::mat4x4<double> cameraMatrix = cglib::lookat4_matrix(eye, cglib::vec3<double>(lineLength * 0.5, centerY, 0), cglib::vec3<double>(0, 1, 0));
        cglib::mat4x4<double> projectionMatrix = cglib::perspective4_matrix(1.0, 1.0, 1.0, 1.0, 1000.0);
        return ViewState(projectionMatrix, cameraMatrix, 0, 0, 0, 1, 1);
    }
}

void testLineLabel() {
    const float lineLength = 10.0f;

    // A run round a BEND collides by its glyph boxes, as maplibre's by circles along it: the run's
    // bounds took in the corner beside the street, and a POI standing there was dropped (Place Grenette, tilt 60).
    {
        std::shared_ptr<Label> label = buildPolylineLabel(buildGlyphs(8), densify({ cglib::vec2<float>(0, 0), cglib::vec2<float>(10, 0), cglib::vec2<float>(18.66f, 5) }, 1.0f), cglib::vec2<float>(10, 0));
        ViewState viewState = buildViewState(20.0f, 2.0f);
        label->updatePlacement(viewState);
        std::array<cglib::vec3<float>, 4> envelope;
        std::vector<std::array<cglib::vec3<float>, 4>> glyphs;
        TEST_CHECK(label->calculateEnvelope(1.0f, 0.0f, viewState, envelope, &glyphs), "a run round a bend is laid out");
        TEST_CHECK(glyphs.size() >= 2 && glyphs.size() <= 8, "a few boxes along it, glyphs merged where the box stays tight");
        // Inside the corner, off the street.
        cglib::vec2<double> beside(13.0 - viewState.origin(0), 0.0 - viewState.origin(1));
        auto covers = [&beside](const std::array<cglib::vec3<float>, 4>& box) {
            // a convex quad: the point is on the same side of all four edges
            int sign = 0;
            for (int k = 0; k < 4; k++) {
                const cglib::vec3<float>& a = box[k];
                const cglib::vec3<float>& b = box[(k + 1) % 4];
                double cross = (b(0) - a(0)) * (beside(1) - a(1)) - (b(1) - a(1)) * (beside(0) - a(0));
                int s = (cross > 0) - (cross < 0);
                if (s != 0 && sign != 0 && s != sign) {
                    return false;
                }
                sign = (s != 0 ? s : sign);
            }
            return true;
        };
        TEST_CHECK(covers(envelope), "the run's box covers the corner beside the street");
        bool anyGlyph = false;
        for (const std::array<cglib::vec3<float>, 4>& glyph : glyphs) {
            anyGlyph = anyGlyph || covers(glyph);
        }
        TEST_CHECK(!anyGlyph, "no glyph box does");
    }

    // A run that fits, anchored one glyph from the end of the line: the anchor is slid back until
    // the run fits (clampPlacementAnchor), and the run then has to stay INSIDE the line. The
    // anchor is what used to push the last glyphs past the end.
    {
        std::shared_ptr<Label> label = buildLineLabel(8, lineLength, 9.0f);
        ViewState viewState = buildViewState(lineLength);
        label->updatePlacement(viewState);

        std::array<cglib::vec3<float>, 4> envelope;
        TEST_CHECK(label->calculateEnvelope(viewState, envelope), "a run shorter than its line is laid out on it");

        // The envelope comes back camera-relative, and it is the run's own bounds - so this is the
        // run itself being measured against the line it was laid out on.
        double minX = std::numeric_limits<double>::max(), maxX = -std::numeric_limits<double>::max();
        for (const cglib::vec3<float>& corner : envelope) {
            double x = viewState.origin(0) + corner(0);
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
        }
        TEST_CHECK(maxX <= lineLength + 1.0e-3, "the run does not reach past the end of the line");
        TEST_CHECK(minX >= -1.0e-3, "the run does not reach past the start of the line");
        TEST_CHECK(maxX - minX >= 7.9, "the whole run is laid out, not a clipped part of it");
    }

    // The run is centred on its anchor, as maplibre's: started there, Rue du 19 Mars 1962 (Grenoble
    // z15.97) was laid half a name past maplibre's, across the bend.
    {
        std::shared_ptr<Label> label = buildLineLabel(8, 40.0f, 20.0f);
        ViewState viewState = buildViewState(40.0f);
        label->updatePlacement(viewState);

        std::array<cglib::vec3<float>, 4> envelope;
        TEST_CHECK(label->calculateEnvelope(viewState, envelope), "a run on a long line is laid out");
        double minX = std::numeric_limits<double>::max(), maxX = -std::numeric_limits<double>::max();
        for (const cglib::vec3<float>& corner : envelope) {
            double x = viewState.origin(0) + corner(0);
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
        }
        TEST_CHECK(std::abs((minX + maxX) * 0.5 - 20.0) < 0.5, "the run is centred on its anchor");
    }

    // A run longer than its line is dropped rather than drawn on a straight continuation of it.
    // 12 glyphs on 10 units of line is what the old allowance (1.5x the line) still accepted.
    {
        std::shared_ptr<Label> label = buildLineLabel(12, lineLength, 5.0f);
        ViewState viewState = buildViewState(lineLength);
        label->updatePlacement(viewState);

        std::array<cglib::vec3<float>, 4> envelope;
        TEST_CHECK(!label->calculateEnvelope(viewState, envelope), "a run longer than its line is not laid out at all");
    }

    // SMOOTHING MAY NOT SHORTEN THE LINE. The same 10 units, this time carried by 11 vertices: the
    // placement averages the line over windows of a third of the text, and averaging the end
    // windows too used to pull both ends inward - here to 8.5 units, less than the run needs. The
    // window scales with the TEXT, so a wider face or a letter-spacing shortened the very line it
    // then had to fit on, and a street name MapBox places was dropped.
    {
        std::shared_ptr<Label> label = buildDenseLineLabel(9, lineLength, 11, 5.0f);
        ViewState viewState = buildViewState(lineLength);
        label->updatePlacement(viewState);

        std::array<cglib::vec3<float>, 4> envelope;
        TEST_CHECK(label->calculateEnvelope(viewState, envelope), "a run needing 9 of 10 units is laid out on a line of many vertices");

        double minX = std::numeric_limits<double>::max(), maxX = -std::numeric_limits<double>::max();
        for (const cglib::vec3<float>& corner : envelope) {
            double x = viewState.origin(0) + corner(0);
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
        }
        TEST_CHECK(maxX <= lineLength + 1.0e-3, "the run still does not reach past the end of the line");
        TEST_CHECK(minX >= -1.0e-3, "the run still does not reach past the start of the line");
    }

    // RUE DE LA VISCOSE, Grenoble z14.46: 203 m, a 78 m jog at 90 degrees, 218 m, under a 282 m name
    // - here 8 glyphs. Averaged, the jog became one slanted edge and the name was laid across it at
    // 66 degrees on a street running at 79. maplibre drops that anchor, and no stretch fits the name.
    {
        std::vector<cglib::vec2<float>> vertices = densify({ { 0, 0 }, { 5.76f, 0 }, { 5.76f, 2.2f }, { 11.94f, 2.2f } }, 0.25f);
        std::shared_ptr<Label> label = buildPolylineLabel(buildGlyphs(8), vertices, cglib::vec2<float>(5.76f, 1.1f));
        TEST_CHECK(!isPlaced(label, buildViewState(11.94f, 1.1f)), "a run across a 90-degree jog is dropped, not laid on a slanted average of it");
    }

    // The same jog on a longer street: an anchor on a straight stretch that fits the run still places.
    {
        std::vector<cglib::vec2<float>> vertices = densify({ { 0, 0 }, { 20, 0 }, { 20, 2.2f }, { 40, 2.2f } }, 0.25f);
        std::shared_ptr<Label> label = buildPolylineLabel(buildGlyphs(8), vertices, cglib::vec2<float>(5, 0));
        TEST_CHECK(isPlaced(label, buildViewState(40, 1.1f)), "a run on a straight stretch of a jogging street is placed");
    }

    // A DEM contour zigzags by a cell at every vertex, turning ~100 degrees each time: at half a glyph of
    // amplitude, that is noise the simplification drops, not corners the angle check sees.
    {
        std::vector<cglib::vec2<float>> vertices;
        for (int i = 0; i <= 32; i++) {
            vertices.emplace_back(i * 0.5f, (i % 2 ? 0.3f : -0.3f));
        }
        std::shared_ptr<Label> label = buildPolylineLabel(buildGlyphs(8), vertices, cglib::vec2<float>(8, 0));
        TEST_CHECK(isPlaced(label, buildViewState(16)), "a line zigzagging under a glyph of amplitude still carries its label");
    }

    // text-max-angle is 45 degrees: a 30-degree bend is followed, a 60-degree one is not. The corner is
    // in the second half of the run, which a centred label missed while its length counted the CR's
    // alignment offset (-width/2) and came out at half the run.
    for (float alignOffset : { 0.0f, -4.0f }) {
        for (float degrees : { 30.0f, 60.0f }) {
            float radians = degrees * 3.14159265f / 180.0f;
            std::vector<cglib::vec2<float>> vertices { { 0, 0 }, { 10, 0 }, { 10 + 3 * std::cos(radians), 3 * std::sin(radians) } };
            std::shared_ptr<Label> label = buildPolylineLabel(buildGlyphs(8, alignOffset), vertices, cglib::vec2<float>(8, 0));
            bool placed = isPlaced(label, buildViewState(12, 1));
            if (degrees < 45) {
                TEST_CHECK(placed, alignOffset < 0 ? "a centred run follows a 30-degree bend" : "a run follows a 30-degree bend");
            }
            else {
                TEST_CHECK(!placed, alignOffset < 0 ? "a centred run is measured whole, so a 60-degree bend in its second half drops it" : "a run across a 60-degree bend is dropped");
            }
        }
    }

    // Rue Émile Zola (Échirolles, z16.6), in glyph units: bends of 18 and 25 degrees half an em off
    // one chord. Simplified, they were one 34-degree corner and text-max-angle 30 dropped the name;
    // maplibre measures the line as it is and keeps it.
    {
        std::vector<cglib::vec2<float>> vertices { { 0, 0 }, { 2.235f, 2.294f }, { 5.176f, 3.824f }, { 17.706f, 4.471f }, { 26.294f, 4.647f } };
        std::shared_ptr<Label> label = buildPolylineLabel(buildGlyphs(10), vertices, cglib::vec2<float>(9.36f, 4.04f), 0.523599f);
        TEST_CHECK(isPlaced(label, buildViewState(26.3f, 2.3f)), "two gentle bends the simplification folds into one corner keep the name");
    }

    // The limit is the style's text-max-angle: a 30 drops the 35-degree bend the default follows.
    for (float maxAngle : { 0.785398f, 0.523599f }) {
        float radians = 35.0f * 3.14159265f / 180.0f;
        std::vector<cglib::vec2<float>> vertices { { 0, 0 }, { 10, 0 }, { 10 + 3 * std::cos(radians), 3 * std::sin(radians) } };
        bool placed = isPlaced(buildPolylineLabel(buildGlyphs(8), vertices, cglib::vec2<float>(8, 0), maxAngle), buildViewState(12, 1));
        TEST_CHECK(placed == (maxAngle > 0.6f), maxAngle > 0.6f ? "a 35-degree bend is followed under the default 45" : "a 35-degree bend drops the run under text-max-angle 30");
    }

    // A 2D/3D ramp re-anchors a placed run every frame: heights alone must not drop it as too short.
    // Sunk away from the top-down camera, the street projects shorter than its name.
    {
        std::shared_ptr<Label> label = buildPolylineLabel(buildGlyphs(8), densify({ cglib::vec2<float>(0, 0), cglib::vec2<float>(9, 0) }, 1.0f), cglib::vec2<float>(4.5f, 0));
        ViewState viewState = buildViewState(9.0f);
        TEST_CHECK(isPlaced(label, viewState), "a street just long enough for its name is named");
        bool kept = true;
        for (int step = 1; step <= 8; step++) {
            double depth = -40.0 - step;
            label->updateElevation([depth](const cglib::vec3<double>& pos) { return cglib::vec3<double>(pos(0), pos(1), depth); });
            std::array<cglib::vec3<float>, 4> envelope;
            kept = label->calculateEnvelope(viewState, envelope) && kept;
        }
        TEST_CHECK(kept, "and keeps it through every re-anchor of a ramp");
    }
}
