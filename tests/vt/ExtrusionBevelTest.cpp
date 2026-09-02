/*
 * The CHAMFER BAND of a 3D extrusion (TileLayerBuilder::appendPolygon3DRing): the quads that fill
 * the gap left when a wall stops short of a footprint corner so the vertical edge can be rounded.
 *
 * ExtrusionCornerTest already covers the cutback ARITHMETIC - how far each wall gives up. Nothing
 * covered the band the cutback creates, and that is where a wall-corner artifact lives: with the
 * building drawn translucent and the shading off, the wedge at a corner is visible as GEOMETRY, so
 * it is built wrong rather than shaded wrong.
 *
 * What a correct band satisfies, and what is asserted here:
 *   - no degenerate triangle - a zero-area one is a crack or a needle depending on rounding;
 *   - no vertex outside the footprint - the chamfer INSETS, so anything outside is a spike;
 *   - the two walls meeting at a corner never cross, at any angle or wall length.
 * The acute-corner and short-wall cases are the ones the cutback caps hardest, so they get their
 * own footprints rather than being hoped for inside a general one.
 *
 * NOT covered here: how the band is SHADED - its normal is normalize(mix(up, side)) and blends
 * across the band, which is a shader concern and a device check. Nor the roof surface itself; this
 * is only the ring the walls and the roof meet through.
 */

#include "TileLayerBuilder.h"

#include "TestCheck.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace massif::vt;

namespace {
    // A REAL tile. At TileId(0,0,0) one tile spans the world, so a 20 m building is ~5e-7 tile
    // units and a 0.4 m chamfer ~1e-8 - both far under the int16 coordinate quantum of 1/32768.
    // The chamfer then cannot be represented at all and every measurement is of that collapse.
    const TileId PARIS_Z16(16, 33193, 22545);

    struct Tri {
        cglib::vec3<float> a, b, c;
    };

    // A footprint, counter-clockwise. The builder orients rings itself (extrusionRingNeedsReverse),
    // so the winding here only has to be a closed ring.
    using Ring = std::vector<cglib::vec2<float>>;

    // Every triangle the extrusion emits, in tile coordinates, with the height applied so the
    // chamfer band is separable from the walls below it.
    std::vector<Tri> extrude(const Ring& ring, float edgeRadius, bool roundedRoof, float height) {
        auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
        TileLayerBuilder builder("test", 0, PARIS_Z16, transformer, 256.0f, 1.0f);
        builder.setPolygon3DEdgeRadius(edgeRadius);
        builder.setPolygon3DRoundedRoof(roundedRoof);

        Polygon3DStyle style(ColorFunction(Color(1, 1, 1, 1)), std::optional<Transform>());
        TileLayerBuilder::Polygon3DProcessor processor = builder.createPolygon3DProcessor(style);
        TileLayerBuilder::VerticesList verticesList;
        verticesList.push_back(ring);
        processor(1, verticesList, 0.0f, height);

        std::shared_ptr<TileLayer> layer = builder.buildTileLayer();
        std::vector<Tri> triangles;
        for (const std::shared_ptr<TileGeometry>& geometry : layer->getGeometries()) {
            if (geometry->getType() != TileGeometry::Type::POLYGON3D) {
                continue;
            }
            const TileGeometry::VertexGeometryLayoutParameters& layout = geometry->getVertexGeometryLayoutParameters();
            const std::uint8_t* vertexData = geometry->getVertexGeometry().data();
            std::vector<cglib::vec3<float>> vertices;
            std::size_t count = geometry->getVertexGeometry().size() / layout.vertexSize;
            for (std::size_t i = 0; i < count; i++) {
                const std::uint8_t* vertex = vertexData + i * layout.vertexSize;
                const std::int16_t* coord = reinterpret_cast<const std::int16_t*>(vertex + layout.coordOffset);
                // An extrusion is FLAT in coord space - the height rides its own attribute - so
                // measuring the packed coords alone makes every wall degenerate. There is NO normal
                // attribute here (normalOffset is -1; the renderer feeds a constant up vector), so
                // the height IS the z.
                const std::int16_t* height = reinterpret_cast<const std::int16_t*>(vertex + layout.heightOffset);
                float h = (layout.heightOffset >= 0 ? height[0] / layout.heightScale : 0.0f);
                vertices.emplace_back(coord[0] / layout.coordScale, coord[1] / layout.coordScale, h);
            }
            const VertexArray<std::uint16_t>& indices = geometry->getIndices();
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
                triangles.push_back({ vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]] });
            }
        }
        return triangles;
    }

    float area2(const Tri& t) {
        cglib::vec3<float> n = cglib::vector_product(t.b - t.a, t.c - t.a);
        return cglib::length(n);
    }

    // The ring as the builder sees it: the tile transformer flips y.
    Ring flipped(const Ring& ring) {
        Ring out;
        for (const cglib::vec2<float>& p : ring) {
            out.emplace_back(p(0), 1.0f - p(1));
        }
        return out;
    }

    // Signed distance OUTSIDE a convex ring, 0 when inside. Only used on convex footprints here,
    // which is what makes "outside the footprint" a sound test of a chamfer that only ever insets.
    float outside(const Ring& ring, const cglib::vec2<float>& p) {
        // Ring orientation is whatever the caller gave; take the sign from the ring's own area.
        float area = 0;
        for (std::size_t i = 0, n = ring.size(); i < n; i++) {
            const cglib::vec2<float>& a = ring[i];
            const cglib::vec2<float>& b = ring[(i + 1) % n];
            area += a(0) * b(1) - b(0) * a(1);
        }
        float sign = (area < 0 ? -1.0f : 1.0f);
        float worst = -std::numeric_limits<float>::max();
        for (std::size_t i = 0, n = ring.size(); i < n; i++) {
            const cglib::vec2<float>& a = ring[i];
            const cglib::vec2<float>& b = ring[(i + 1) % n];
            cglib::vec2<float> e = b - a;
            // Outward normal for this winding.
            cglib::vec2<float> nrm(e(1) * sign, -e(0) * sign);
            float len = cglib::length(nrm);
            if (!(len > 0)) {
                continue;
            }
            worst = std::max(worst, cglib::dot_product(nrm * (1.0f / len), p - a));
        }
        return std::max(0.0f, worst);
    }

    // How far the worst vertex escapes the footprint, and the smallest triangle emitted.
    struct Report {
        float escape = 0.0f;
        float smallestArea2 = std::numeric_limits<float>::max();
        std::size_t triangles = 0;
    };

    Report inspect(const Ring& ring, float edgeRadius, bool roundedRoof, float height) {
        std::vector<Tri> triangles = extrude(ring, edgeRadius, roundedRoof, height);
        Ring footprint = flipped(ring);
        Report report;
        report.triangles = triangles.size();
        for (const Tri& t : triangles) {
            report.smallestArea2 = std::min(report.smallestArea2, area2(t));
            for (const cglib::vec3<float>& v : { t.a, t.b, t.c }) {
                report.escape = std::max(report.escape, outside(footprint, cglib::vec2<float>(v(0), v(1))));
            }
        }
        return report;
    }

    // How close any emitted vertex gets to a footprint point. Used on a corner whose neighbouring
    // edge is CLIPPED AWAY: appendPolygon3DRing only backs a wall off from a corner this tile can
    // also fill, so at such a corner the wall must still reach it. If it is cut back anyway there
    // is no wedge to close the gap, and the building is open at the tile border.
    float nearest(const Ring& ring, float edgeRadius, bool roundedRoof, float height, const cglib::vec2<float>& probe) {
        float best = std::numeric_limits<float>::max();
        for (const Tri& t : extrude(ring, edgeRadius, roundedRoof, height)) {
            for (const cglib::vec3<float>& v : { t.a, t.b, t.c }) {
                best = std::min(best, cglib::length(cglib::vec2<float>(v(0), v(1)) - probe));
            }
        }
        return best;
    }

    Ring square(float x, float y, float size) {
        return { { x, y }, { x + size, y }, { x + size, y + size }, { x, y + size } };
    }

    // A sharp spur: the cutback caps hardest here, and it is the shape a wedge is reported on.
    Ring acuteWedge(float scale) {
        return { { 0.5f, 0.5f }, { 0.5f + scale, 0.5f }, { 0.5f + scale, 0.5f + scale * 0.08f } };
    }
}

void testExtrusionBevel() {
    // Metres, as tesselatePolygon3D reads them, and the style's own building-edge-radius.
    const float RADIUS = 0.4f;
    const float HEIGHT = 20.0f;
    // ~50 m across on a z16 tile (~611 m at this latitude), i.e. a building, not a continent.
    const float SIDE = 0.08f;

    // A plain right-angled footprint: the band exists and is well formed.
    {
        Report r = inspect(square(0.45f, 0.45f, SIDE), RADIUS, true, HEIGHT);
        TEST_CHECK(r.triangles > 0, "a chamfered extrusion emits geometry");
        TEST_CHECK(r.escape < 1.0e-4f, "no chamfer vertex escapes a right-angled footprint");
        TEST_CHECK(r.smallestArea2 > 0.0f, "a right-angled chamfer emits no degenerate triangle");
    }

    // The same footprint with the chamfer off - the comparison the bench knob makes, so it is the
    // one that says whether the band is what differs.
    {
        Report r = inspect(square(0.45f, 0.45f, SIDE), 0.0f, false, HEIGHT);
        TEST_CHECK(r.escape < 1.0e-4f, "no vertex escapes the footprint with the chamfer off");
        TEST_CHECK(r.smallestArea2 > 0.0f, "an unchamfered extrusion emits no degenerate triangle");
    }

    // ACUTE corners, where extrusionCornerCutback caps at a third of the shorter edge.
    {
        Report r = inspect(acuteWedge(SIDE), RADIUS, true, HEIGHT);
        TEST_CHECK(r.escape < 1.0e-4f, "no chamfer vertex escapes an acute corner");
        TEST_CHECK(r.smallestArea2 > 0.0f, "an acute corner emits no degenerate triangle");
    }

    // A radius far larger than the footprint can absorb: the cap must hold rather than letting the
    // two ends of a wall cross and fold the band inside out.
    {
        Report r = inspect(square(0.49f, 0.49f, SIDE * 0.1f), 20.0f, true, HEIGHT);
        TEST_CHECK(r.escape < 1.0e-4f, "an oversized radius does not push the chamfer outside the footprint");
        TEST_CHECK(r.smallestArea2 > 0.0f, "an oversized radius emits no degenerate triangle");
    }

    // A footprint STRADDLING THE TILE BORDER. The clip box is the tile plus a 1/512 margin, so the
    // right-hand edges here fall outside it entirely and are never emitted. That is the case
    // appendPolygon3DRing singles out: a corner is only backed off from when this tile can fill it,
    // because a lone cut-back wall leaves a gap straight through the building at the border.
    {
        const float X0 = 0.90f, X1 = 1.15f, Y0 = 0.45f, Y1 = 0.53f;
        Ring straddling = { { X0, Y0 }, { X1, Y0 }, { X1, Y1 }, { X0, Y1 } };
        Report r = inspect(straddling, RADIUS, true, HEIGHT);
        TEST_CHECK(r.triangles > 0, "a footprint crossing the tile border still emits its in-tile part");
        TEST_CHECK(r.smallestArea2 > 0.0f, "a clipped footprint emits no degenerate triangle");

        // The two corners on the IN-TILE side each have one neighbouring edge running out of the
        // box, so neither may be cut back - the wall has to arrive at the corner itself.
        Ring flippedRing = flipped(straddling);
        for (const cglib::vec2<float>& corner : { flippedRing[0], flippedRing[3] }) {
            float gap = nearest(straddling, RADIUS, true, HEIGHT, corner);
            TEST_CHECK(gap < 1.0e-3f, "a wall reaches a corner whose neighbouring edge is clipped away");
        }
    }

    // A FLAT facet instead of a rounded roof: capSide/roofSide differ (64 vs 127/0), so the band is
    // built through a different path.
    {
        Report r = inspect(square(0.45f, 0.45f, SIDE), RADIUS, false, HEIGHT);
        TEST_CHECK(r.escape < 1.0e-4f, "no chamfer vertex escapes with a flat roof facet");
        TEST_CHECK(r.smallestArea2 > 0.0f, "a flat roof facet emits no degenerate triangle");
    }
}
