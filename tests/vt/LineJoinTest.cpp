/*
 * How far out of its own silhouette a tesselated line reaches at a join (TileLayerBuilder::
 * tesselateLine). The vertex stage extrudes each vertex by binormal * half-width, so the test
 * rebuilds that here and measures every triangle it gets back against the polyline.
 *
 * The case that made this: at a near-reversing turn the INNER corner is the true miter point,
 * 1 / cos(turn / 2) half-widths out, and stroke-miterlimit never capped that vector - it only picks
 * WHICH branch runs, which is why setting it to 4, 3 or 2 all drew the same spike. At 161 degrees
 * (just short of the split branch's -0.95) it reached 6.06 half-widths and needled out past any
 * segment shorter than that: the spike roundabouts and slip roads grew at z11-12 and lost by z12.2,
 * where the same geometry is many half-widths long.
 *
 * NOT covered here: what the fragment stage then does with the antialias ramp, and the round fan's
 * own smoothness - both are device checks.
 */

#include "TileLayerBuilder.h"

#include "TestCheck.h"

#include <cmath>
#include <vector>

using namespace massif::vt;

namespace {
    // What LineSymbolizer passes for a line with no offset and no dash pattern.
    constexpr float SPLIT_DOT_LIMIT = -0.95f;

    // stroke-miterlimit's own default.
    constexpr float STROKE_MITER_LIMIT = 4.0f;
    // What a bevel/round join's inner corner is clamped to (vt's INNER_MITER_LIMIT), plus the
    // int16 packing of the coordinates.
    constexpr float INNER_MITER_LIMIT = 1.05f;

    // Its miterDotLimit too: 1 for a bevel join, so every turn takes the bevel branch, and
    // 2 / miterlimit^2 - 1 otherwise.
    float miterDotLimitFor(LineJoinMode joinMode) {
        return (joinMode == LineJoinMode::BEVEL ? 1.0f : 2.0f / (STROKE_MITER_LIMIT * STROKE_MITER_LIMIT) - 1.0f);
    }

    struct Triangle {
        cglib::vec2<float> a, b, c;
    };

    // The line as the vertex stage would extrude it: one point per vertex, at coord + binormal *
    // half-width, grouped into the triangles the indices name.
    std::vector<Triangle> tesselate(const std::vector<cglib::vec2<float>>& points, float halfWidth, LineJoinMode joinMode, float miterDotLimit) {
        auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
        TileLayerBuilder builder("test", 0, TileId(0, 0, 0), transformer, 256.0f, 1.0f);
        LineStyle style(CompOp::SRC_OVER, joinMode, LineCapMode::NONE, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), FloatFunction(0.0f), SPLIT_DOT_LIMIT, miterDotLimit, std::shared_ptr<const BitmapPattern>(), std::optional<Transform>());
        TileLayerBuilder::LineProcessor processor = builder.createLineProcessor(style, std::shared_ptr<StrokeMap>());
        processor(1, points);

        std::shared_ptr<TileLayer> layer = builder.buildTileLayer();
        std::vector<Triangle> triangles;
        for (const std::shared_ptr<TileGeometry>& geometry : layer->getGeometries()) {
            const TileGeometry::VertexGeometryLayoutParameters& layout = geometry->getVertexGeometryLayoutParameters();
            const std::uint8_t* vertexData = geometry->getVertexGeometry().data();
            std::vector<cglib::vec2<float>> vertices;
            for (std::size_t i = 0; i < geometry->getVertexGeometry().size() / layout.vertexSize; i++) {
                const std::uint8_t* vertex = vertexData + i * layout.vertexSize;
                const std::int16_t* coord = reinterpret_cast<const std::int16_t*>(vertex + layout.coordOffset);
                const std::int16_t* binormal = reinterpret_cast<const std::int16_t*>(vertex + layout.binormalOffset);
                vertices.emplace_back(coord[0] / layout.coordScale + binormal[0] / layout.binormalScale * halfWidth,
                                      coord[1] / layout.coordScale + binormal[1] / layout.binormalScale * halfWidth);
            }
            const VertexArray<std::uint16_t>& indices = geometry->getIndices();
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
                triangles.push_back({ vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]] });
            }
        }
        return triangles;
    }

    float distanceToSegment(const cglib::vec2<float>& p, const cglib::vec2<float>& p0, const cglib::vec2<float>& p1) {
        cglib::vec2<float> dp = p1 - p0;
        float length2 = cglib::dot_product(dp, dp);
        float t = (length2 > 0 ? cglib::dot_product(p - p0, dp) / length2 : 0.0f);
        t = std::max(0.0f, std::min(1.0f, t));
        return cglib::length(p - (p0 + dp * t));
    }

    // The farthest any point the triangles cover gets from the polyline, in half-widths. A correct
    // line reaches exactly one (plus the antialias ramp the packing rounds in).
    float reach(const std::vector<cglib::vec2<float>>& points, float halfWidth, LineJoinMode joinMode) {
        std::vector<Triangle> triangles = tesselate(points, halfWidth, joinMode, miterDotLimitFor(joinMode));
        std::vector<cglib::vec2<float>> line;
        for (const cglib::vec2<float>& p : points) {
            line.emplace_back(p(0), 1.0f - p(1)); // the tile transformer flips y
        }

        float worst = 0;
        for (const Triangle& triangle : triangles) {
            for (int a = 0; a <= 8; a++) {
                for (int b = 0; a + b <= 8; b++) {
                    cglib::vec2<float> p = (triangle.a * static_cast<float>(a) + triangle.b * static_cast<float>(b) + triangle.c * static_cast<float>(8 - a - b)) * 0.125f;
                    float distance = std::numeric_limits<float>::max();
                    for (std::size_t i = 0; i + 1 < line.size(); i++) {
                        distance = std::min(distance, distanceToSegment(p, line[i], line[i + 1]));
                    }
                    worst = std::max(worst, distance);
                }
            }
        }
        return worst / halfWidth;
    }

    bool covers(const Triangle& t, const cglib::vec2<float>& p) {
        auto side = [](const cglib::vec2<float>& a, const cglib::vec2<float>& b, const cglib::vec2<float>& q) {
            return (b(0) - a(0)) * (q(1) - a(1)) - (b(1) - a(1)) * (q(0) - a(0));
        };
        float s0 = side(t.a, t.b, p), s1 = side(t.b, t.c, p), s2 = side(t.c, t.a, p);
        return (s0 >= 0 && s1 >= 0 && s2 >= 0) || (s0 <= 0 && s1 <= 0 && s2 <= 0);
    }

    // Whether the whole polyline is painted: every centre-line sample has to fall inside some
    // triangle. A join that hands the wrong vertices to the next segment leaves a hole here.
    bool coversCentreLine(const std::vector<cglib::vec2<float>>& points, float halfWidth, LineJoinMode joinMode) {
        std::vector<Triangle> triangles = tesselate(points, halfWidth, joinMode, miterDotLimitFor(joinMode));
        std::vector<cglib::vec2<float>> line;
        for (const cglib::vec2<float>& p : points) {
            line.emplace_back(p(0), 1.0f - p(1)); // the tile transformer flips y
        }
        for (std::size_t i = 0; i + 1 < line.size(); i++) {
            for (int step = 0; step <= 20; step++) {
                cglib::vec2<float> p = line[i] + (line[i + 1] - line[i]) * (step / 20.0f);
                bool covered = false;
                for (const Triangle& triangle : triangles) {
                    covered = covered || covers(triangle, p);
                }
                if (!covered) {
                    return false;
                }
            }
        }
        return true;
    }

    // A turn of 'turnDegrees' between two segments 'segmentHalfWidths' long, around tile centre.
    std::vector<cglib::vec2<float>> hairpin(float turnDegrees, float segmentHalfWidths, float halfWidth) {
        float length = segmentHalfWidths * halfWidth;
        float turn = turnDegrees * boost::math::constants::pi<float>() / 180.0f;
        return { { 0.5f - length, 0.5f }, { 0.5f, 0.5f }, { 0.5f + length * std::cos(turn), 0.5f + length * std::sin(turn) } };
    }
}

void testLineJoinReach() {
    const float halfWidth = 0.004f;

    // A gentle turn is the baseline: the line reaches its own half-width and no further, whatever
    // the join. (1.01 rather than 1.0 is the int16 packing of the coordinates.)
    TEST_CHECK(reach(hairpin(60.0f, 40.0f, halfWidth), halfWidth, LineJoinMode::ROUND) < 1.05f, "a round join stays inside the line width");
    TEST_CHECK(reach(hairpin(60.0f, 40.0f, halfWidth), halfWidth, LineJoinMode::BEVEL) < 1.05f, "a bevel join stays inside the line width");

    // The reported spike: a near-reversal on a segment only a few half-widths long. 161 degrees is
    // the worst angle - one notch short of SPLIT_DOT_LIMIT, where the miter is longest and the
    // split branch has not taken over yet.
    for (float segmentHalfWidths : { 1.0f, 2.0f, 4.0f }) {
        TEST_CHECK(reach(hairpin(161.0f, segmentHalfWidths, halfWidth), halfWidth, LineJoinMode::ROUND) < INNER_MITER_LIMIT, "a round join on a short segment does not needle out at a near-reversal");
        TEST_CHECK(reach(hairpin(161.0f, segmentHalfWidths, halfWidth), halfWidth, LineJoinMode::BEVEL) < INNER_MITER_LIMIT, "a bevel join on a short segment does not needle out at a near-reversal");
    }

    // Every angle, on segments short enough for the miter to leave them.
    float worstRound = 0, worstBevel = 0;
    for (int turnDegrees = 1; turnDegrees <= 179; turnDegrees++) {
        worstRound = std::max(worstRound, reach(hairpin(static_cast<float>(turnDegrees), 2.0f, halfWidth), halfWidth, LineJoinMode::ROUND));
        worstBevel = std::max(worstBevel, reach(hairpin(static_cast<float>(turnDegrees), 2.0f, halfWidth), halfWidth, LineJoinMode::BEVEL));
    }
    TEST_CHECK(worstRound < INNER_MITER_LIMIT, "no turn angle makes a round join reach past the inner miter limit");
    TEST_CHECK(worstBevel < INNER_MITER_LIMIT, "no turn angle makes a bevel join reach past the inner miter limit");

    // A MITER join is the one that IS allowed to reach out - up to stroke-miterlimit and no
    // further, which is what the limit means. This is the check that the clamp above did not
    // quietly take that away.
    float worstMiter = 0;
    for (int turnDegrees = 1; turnDegrees <= 179; turnDegrees++) {
        worstMiter = std::max(worstMiter, reach(hairpin(static_cast<float>(turnDegrees), 40.0f, halfWidth), halfWidth, LineJoinMode::MITER));
    }
    TEST_CHECK(worstMiter > 3.5f && worstMiter < 4.1f, "a miter join still reaches its stroke-miterlimit");

    // Past 161 degrees the SPLIT branch takes over, and its round fan is what the next segment's
    // quad picks its start cross-section up from. Emitted after the cross-section instead of
    // before it, the quad is built off two fan rim vertices and the segment loses its inner half.
    for (int turnDegrees = 162; turnDegrees <= 179; turnDegrees++) {
        if (!coversCentreLine(hairpin(static_cast<float>(turnDegrees), 40.0f, halfWidth), halfWidth, LineJoinMode::ROUND)) {
            TEST_CHECK(false, "a round split join leaves the segment after it fully painted");
            return;
        }
    }
    TEST_CHECK(true, "a round split join leaves the segment after it fully painted");
}
