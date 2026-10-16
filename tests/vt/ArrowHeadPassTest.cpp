// A maneuver arrow's heads (`line-arrow-only` styles) draw after every tile's shafts of the style layer
// and unmasked. Drawn with their tile, the tile mask cut a head at the tile edge (a flat map) and a
// neighbour's shaft stub, drawn later, painted over the part of the head lying in its tile.
// Covered: which slots each pass paints, how the builder marks the arrow-only ones, and that one never
// shares a slot with a plain line. Not covered: the draw itself (GL) - a device check.

#include "ArrowHeadPass.h"
#include "TileLayerBuilder.h"

#include "TestCheck.h"

#include <vector>

using namespace massif::vt;

namespace {
    // The four attachments of a maneuver arrow: the same colour and width for the shaft and the head
    // casing, the same for the fill and the head.
    LineStyle arrowStyle(float color, bool arrowOnly) {
        return LineStyle(CompOp::SRC_OVER, LineJoinMode::ROUND, LineCapMode::ROUND, ColorFunction(Color(color, color, color, 1)), FloatFunction(1.0f), FloatFunction(0.0f), -0.95f, -0.875f, std::shared_ptr<const BitmapPattern>(), std::optional<Transform>(), arrowOnly ? 2.4f : 0.0f, arrowOnly ? 1.9f : 0.0f, arrowOnly);
    }

    std::shared_ptr<TileLayer> build(const std::vector<LineStyle>& styles) {
        auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
        TileLayerBuilder builder("test", 0, TileId(18, 132775, 90182), transformer, 256.0f, 1.0f);
        long long id = 1;
        for (const LineStyle& style : styles) {
            TileLayerBuilder::LineProcessor processor = builder.createLineProcessor(style, std::shared_ptr<StrokeMap>());
            processor(id++, { { 0.3f, 0.5f }, { 0.5f, 0.3f }, { 0.7f, 0.5f } });
        }
        return builder.buildTileLayer();
    }
}

void testArrowHeadPass() {
    TEST_CHECK(arrowSlotDrawn(ArrowHeadPass::ALL, true) && arrowSlotDrawn(ArrowHeadPass::ALL, false), "a layer with no head draws every slot in its one pass");
    TEST_CHECK(arrowSlotDrawn(ArrowHeadPass::SHAFTS, false) && !arrowSlotDrawn(ArrowHeadPass::SHAFTS, true), "the first pass skips the head slots");
    TEST_CHECK(arrowSlotDrawn(ArrowHeadPass::HEADS, true) && !arrowSlotDrawn(ArrowHeadPass::HEADS, false), "the second draws only them");
    for (bool arrowOnly : { false, true }) {
        TEST_CHECK(arrowSlotDrawn(ArrowHeadPass::SHAFTS, arrowOnly) != arrowSlotDrawn(ArrowHeadPass::HEADS, arrowOnly), "every slot is drawn by exactly one of the two passes");
    }

    TEST_CHECK(arrowHeadPassCount(false, true) == 1, "no head slot: one pass");
    TEST_CHECK(arrowHeadPassCount(true, true) == 2, "a head slot in a plain layer: shafts, then heads");
    TEST_CHECK(arrowHeadPassCount(true, false) == 1, "an overlay or draw-once layer keeps its own order");

    // case, fill, headcase, head: four slots even though case and headcase (and fill and head) agree
    // on colour and width, and only the last two are arrow-only.
    std::shared_ptr<TileLayer> layer = build({ arrowStyle(0.2f, false), arrowStyle(1.0f, false), arrowStyle(0.2f, true), arrowStyle(1.0f, true) });
    TEST_CHECK(layer->getGeometries().size() == 1, "the four attachments are one batch");
    if (layer->getGeometries().size() == 1) {
        const TileGeometry::StyleParameters& params = layer->getGeometries()[0]->getStyleParameters();
        TEST_CHECK(params.parameterCount == 4, "a head slot is not merged into the shaft slot of the same colour and width");
        TEST_CHECK(params.parameterCount == 4 && !params.arrowOnly[0] && !params.arrowOnly[1] && params.arrowOnly[2] && params.arrowOnly[3], "the geometry says which slots are heads");
    }

    std::shared_ptr<TileLayer> plain = build({ arrowStyle(0.2f, false), arrowStyle(1.0f, false) });
    TEST_CHECK(plain->getGeometries().size() == 1 && !plain->getGeometries()[0]->getStyleParameters().arrowOnly[0] && !plain->getGeometries()[0]->getStyleParameters().arrowOnly[1], "a line with no arrow-only style marks none");
}
