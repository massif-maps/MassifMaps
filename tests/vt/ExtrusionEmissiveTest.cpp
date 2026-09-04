/*
 * Per-rule emissive on an EXTRUSION (Polygon3DStyle::emissiveFunc, building-emissive-strength).
 *
 * Why it exists: everything a converted style draws carries its own `*-emissive-strength`, so at a
 * low sun a 2D fill keeps most of its authored colour while an extrusion - which had no such
 * property - was lit at full strength. That is invisible on a building, which mapbox lights the
 * same way, and glaring on a bridge DECK, an extrusion this fork adds to stand in for the flat road
 * casing 2D styles draw: the casing stayed bright and the deck under it went near black.
 *
 * The case that matters is the UNSET one. An extrusion that states nothing must resolve to the Map
 * block's building-emissive exactly as before, or this property changes every building in every
 * existing style - so unset is a std::optional that stays empty, never a 0 or a 1 standing in for
 * "no opinion".
 *
 * The other half is BATCHING. One draw carries one u_emissive uniform, so two rules that differ in
 * it cannot share a geometry - the same split elevationMode already forces. Without it a deck and a
 * building in one tile would land in one batch and the second rule's emissive would be dropped.
 *
 * NOT covered here: what the value does to the pixels. The emissive reaches the shader as
 * u_emissive and mixes the lit colour back toward the authored one (LIGHTING_SHADER_3D), which is a
 * device check - the deck against the casing it replaces, at a low sun.
 */

#include "TileLayerBuilder.h"

#include "TestCheck.h"

#include <optional>
#include <vector>

using namespace massif::vt;

namespace {
    const TileId PARIS_Z16(16, 33193, 22545);

    using Ring = std::vector<cglib::vec2<float>>;

    Ring square(float x0, float y0, float side) {
        return { { x0, y0 }, { x0 + side, y0 }, { x0 + side, y0 + side }, { x0, y0 + side } };
    }

    Polygon3DStyle styleWith(std::optional<FloatFunction> emissive) {
        return Polygon3DStyle(ColorFunction(Color(1, 1, 1, 1)), std::optional<Transform>(),
                              RoofShape::FLAT, 0.0f, LineElevationMode::DRAPE, std::move(emissive));
    }

    /** The emissive each POLYGON3D geometry the builder produced carries, in build order. */
    std::vector<std::optional<FloatFunction>> emissivesOf(const std::vector<Polygon3DStyle>& styles) {
        auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
        TileLayerBuilder builder("test", 0, PARIS_Z16, transformer, 256.0f, 1.0f);
        long long id = 1;
        float x = 0.1f;
        for (const Polygon3DStyle& style : styles) {
            TileLayerBuilder::Polygon3DProcessor processor = builder.createPolygon3DProcessor(style);
            TileLayerBuilder::VerticesList verticesList;
            verticesList.push_back(square(x, 0.1f, 0.05f));
            processor(id++, verticesList, 0.0f, 10.0f);
            x += 0.1f;
        }

        std::vector<std::optional<FloatFunction>> emissives;
        std::shared_ptr<TileLayer> layer = builder.buildTileLayer();
        for (const std::shared_ptr<TileGeometry>& geometry : layer->getGeometries()) {
            if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
                emissives.push_back(geometry->getStyleParameters().polygon3DEmissiveFunc);
            }
        }
        return emissives;
    }

    void testAnExtrusionThatSaysNothingKeepsSayingNothing() {
        // The regression guard for every existing style: unset must survive the whole builder, so
        // the renderer falls back to the Map block. An empty optional, not 0 and not 1.
        std::vector<std::optional<FloatFunction>> emissives = emissivesOf({ styleWith(std::nullopt) });
        TEST_CHECK(emissives.size() == 1, "one extrusion, one geometry");
        TEST_CHECK(!emissives[0].has_value(), "an extrusion with no emissive of its own carries none");
    }

    void testARuleCanStateItsOwn() {
        std::vector<std::optional<FloatFunction>> emissives = emissivesOf({ styleWith(FloatFunction(0.6f)) });
        TEST_CHECK(emissives.size() == 1, "one extrusion, one geometry");
        TEST_CHECK(emissives[0].has_value(), "a rule that states an emissive carries it");
        TEST_CHECK(*emissives[0] == FloatFunction(0.6f), "... at the value it stated");
    }

    void testTwoEmissivesDoNotShareABatch() {
        // One draw, one u_emissive. Batched together, the second rule's value would be lost - which
        // is exactly the deck-beside-a-building case this property exists for.
        std::vector<std::optional<FloatFunction>> emissives =
            emissivesOf({ styleWith(std::nullopt), styleWith(FloatFunction(0.6f)) });
        TEST_CHECK(emissives.size() == 2, "two emissives are two geometries, not one batch");
        TEST_CHECK(!emissives[0].has_value() && emissives[1].has_value(), "and each keeps its own");
    }

    void testTheSameEmissiveStillBatches() {
        // The split is on the VALUE, not on the property existing: two rules that agree must still
        // share a batch, or a style that sets one emissive everywhere pays a draw call per rule.
        std::vector<std::optional<FloatFunction>> same =
            emissivesOf({ styleWith(FloatFunction(0.6f)), styleWith(FloatFunction(0.6f)) });
        TEST_CHECK(same.size() == 1, "two rules at the same emissive share one geometry");
        std::vector<std::optional<FloatFunction>> unset =
            emissivesOf({ styleWith(std::nullopt), styleWith(std::nullopt) });
        TEST_CHECK(unset.size() == 1, "... and so do two that both leave it to the map");
    }

}

void testExtrusionEmissive() {
    testAnExtrusionThatSaysNothingKeepsSayingNothing();
    testARuleCanStateItsOwn();
    testTwoEmissivesDoNotShareABatch();
    testTheSameEmissiveStillBatches();
}
