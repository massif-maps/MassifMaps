/*
 * The span resolver driven without a renderer: a span line built through the real
 * TileLayerBuilder, a ground that answers by position, and the bases read back.
 *
 * What it pins: off by default and free (no read, no chord, no unresolved end); on, a piece with
 * both portals inside its tile resolves to the chord between the two PORTAL heights and nothing
 * in between - the whole point of a span, a DSM spike off the deck never lifts the middle; a
 * piece the tile cut reports its cut end for the owner to fetch and resolves nothing; turning it
 * off again forgets everything.
 *
 * NOT covered: the merge of several pieces into one structure across tiles (SpanGeometryTest
 * pins the rules it uses), the chord cache's borrow after a pan, and every GL side of a deck.
 */

#include "SpanResolver.h"
#include "TileLayerBuilder.h"

#include "TestCheck.h"

#include <cmath>
#include <cstring>
#include <map>
#include <vector>

using namespace massif::vt;

namespace {

    const TileId TILE(16, 33000, 22000);

    /** One span line through the builder, as LineSymbolizer would emit it, in tile units. */
    std::shared_ptr<TileLayer> buildSpanLine(const std::vector<cglib::vec2<float>>& points) {
        auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
        TileLayerBuilder builder("test", 0, TILE, transformer, 256.0f, 1.0f);
        LineStyle style(CompOp::SRC_OVER, LineJoinMode::MITER, LineCapMode::NONE, ColorFunction(Color(1, 1, 1, 1)), FloatFunction(1.0f), FloatFunction(0.0f), -0.95f, 1.0f, std::shared_ptr<const BitmapPattern>(), std::optional<Transform>());
        style.elevationMode = LineElevationMode::SPAN;
        TileLayerBuilder::LineProcessor processor = builder.createLineProcessor(style, std::shared_ptr<StrokeMap>());
        processor(1, points);
        return builder.buildTileLayer();
    }

    std::map<TileId, std::shared_ptr<const Tile>> tilesOf(const std::shared_ptr<TileLayer>& layer) {
        std::map<TileId, std::shared_ptr<const Tile>> tiles;
        tiles[TILE] = std::make_shared<Tile>(TILE, 256.0f, std::vector<std::shared_ptr<TileLayer>> { layer });
        return tiles;
    }

    /** The base written for the vertex nearest tile-local x, as the shader reads it. */
    float baseNearX(const std::shared_ptr<TileGeometry>& geometry, float x) {
        const TileGeometry::VertexGeometryLayoutParameters& layout = geometry->getVertexGeometryLayoutParameters();
        const std::uint8_t* data = geometry->getVertexGeometry().data();
        std::size_t best = 0;
        float bestDistance = 1.0e9f;
        for (std::size_t i = 0; i < geometry->getVertexGeometry().size() / layout.vertexSize; i++) {
            const std::int16_t* coord = reinterpret_cast<const std::int16_t*>(data + i * layout.vertexSize + layout.coordOffset);
            float distance = std::abs(coord[0] / layout.coordScale - x);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        float base = 0;
        std::memcpy(&base, data + best * layout.vertexSize + layout.baseOffset, sizeof(float));
        return base;
    }

    /** Tile-local (u, v) in normalized world, the way the resolver reads a record. */
    cglib::vec2<double> world(double u, double v) {
        return cglib::transform_point(cglib::vec2<double>(u, 1.0 - v), SpanResolver::tileMatrix2D(TILE));
    }
}

void testSpanResolver() {
    // The ground: 100 units at the west portal rising to 200 at the east, with a 500-unit spike
    // in the middle third of the tile - the DSM catching the deck itself.
    int reads = 0;
    double westX = world(0.2, 0.5)(0), eastX = world(0.8, 0.5)(0);
    SpanResolver::ElevationProvider ground = [&](const cglib::vec3<double>& pos, int, bool, double& height) {
        reads++;
        double t = (pos(0) - westX) / (eastX - westX);
        height = 100.0 + 100.0 * t + (t > 0.3 && t < 0.7 ? 500.0 : 0.0);
        return true;
    };

    std::shared_ptr<TileLayer> layer = buildSpanLine({ cglib::vec2<float>(0.2f, 0.5f), cglib::vec2<float>(0.5f, 0.5f), cglib::vec2<float>(0.8f, 0.5f) });
    std::shared_ptr<TileGeometry> geometry = layer->getGeometries().front();
    TEST_CHECK(!geometry->getSpanRecords().empty() && geometry->getSpanRecords().front().portal0 && geometry->getSpanRecords().front().portal1,
               "a span line with both ends inside the tile carries a record with two portals");

    SpanResolver resolver;
    resolver.setElevationProvider(ground);
    resolver.setMetersToInternal(1.0);

    // Off by default, and free.
    resolver.build(tilesOf(layer), {}, { TILE }, 1);
    TEST_CHECK(reads == 0, "off, a build reads no ground");
    TEST_CHECK(!resolver.resolve(TILE, geometry, 1), "off, a span is not resolved (it drapes)");
    TEST_CHECK(resolver.chords(1).empty() && resolver.unresolvedEnds().empty(), "off, there is no chord and no end to fetch");

    // On: the chord between the two portals, read once each.
    resolver.setEnabled(true);
    resolver.build(tilesOf(layer), {}, { TILE }, 1);
    TEST_CHECK(reads == 2, "on, a build reads the two portals and nothing in between");
    TEST_CHECK(resolver.takeLabelsDirty(), "a chord that just got its heights asks for the labels");
    TEST_CHECK(!resolver.takeLabelsDirty(), "...once");
    const std::vector<SpanResolver::SpanChord>& chords = resolver.chords(1);
    TEST_CHECK(chords.size() == 1, "one structure, one chord");
    double h0 = chords.empty() ? 0 : chords.front().height0, h1 = chords.empty() ? 0 : chords.front().height1;
    TEST_CHECK(std::abs(std::min(h0, h1) - 100.0) < 0.5 && std::abs(std::max(h0, h1) - 200.0) < 0.5, "the chord's heights are the portals' ground (int16-packed ends)");

    TEST_CHECK(resolver.resolve(TILE, geometry, 1), "on, the piece resolves");
    TEST_CHECK(geometry->isBaseResolved(), "...and says so");
    TEST_CHECK(std::abs(baseNearX(geometry, 0.5f) - 150.0f) < 0.5f, "the middle vertex sits on the chord, not on the spike under it");
    TEST_CHECK(std::abs(baseNearX(geometry, 0.2f) - 100.0f) < 0.5f && std::abs(baseNearX(geometry, 0.8f) - 200.0f) < 0.5f, "the ends sit on their portals");
    double deck = 0;
    TEST_CHECK(resolver.heightAt(world(0.5, 0.5), deck) && std::abs(deck - 150.0) < 1.0e-6, "a label at mid-span is lifted onto the chord");
    TEST_CHECK(!resolver.heightAt(world(0.5, 0.9), deck), "...and one beside the deck is not");

    // A piece the tile cut: one portal, nothing to chord it with, and the cut end named for the owner.
    std::shared_ptr<TileLayer> cutLayer = buildSpanLine({ cglib::vec2<float>(0.2f, 0.3f), cglib::vec2<float>(1.05f, 0.3f) });
    std::shared_ptr<TileGeometry> cutGeometry = cutLayer->getGeometries().front();
    SpanResolver fresh;
    fresh.setElevationProvider(ground);
    fresh.setEnabled(true);
    fresh.build(tilesOf(cutLayer), {}, { TILE }, 1);
    TEST_CHECK(fresh.unresolvedEnds().size() == 1 && fresh.unresolvedEnds().front().first == TILE.zoom, "a cut end is reported once, at the piece's zoom");
    TEST_CHECK(!fresh.resolve(TILE, cutGeometry, 1), "and the piece stays unresolved");

    // Off again forgets everything.
    resolver.setEnabled(false);
    TEST_CHECK(resolver.chords(2).empty() && resolver.unresolvedEnds().empty(), "off again, the chords are gone");
    TEST_CHECK(!resolver.resolve(TILE, geometry, 2), "...and nothing resolves");
}
