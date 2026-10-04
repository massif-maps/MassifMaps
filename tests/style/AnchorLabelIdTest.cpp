// Id-less features all get id 0, and a MultiPoint's points share one, so point labels fold their
// anchor into the id: nearby points must differ, one point read from any tile must agree.

#include "TestCheck.h"

#include <mapnikvt/Symbolizer.h>

using massif::vt::TileId;

namespace {
    using Vertex = cglib::vec2<float>;

    struct AnchorIdProbe final : massif::mvt::Symbolizer {
        AnchorIdProbe() : Symbolizer(std::shared_ptr<massif::mvt::Logger>()) { }

        FeatureProcessor createFeatureProcessor(const massif::mvt::ExpressionContext&, const massif::mvt::SymbolizerContext&) const override {
            return FeatureProcessor();
        }

        using Symbolizer::combineAnchorId;
    };

    // One anchor cell is 2^-28 of the world, 2^-14 of a zoom 14 tile.
    constexpr float CELL_Z14 = 1.0f / 16384.0f;
}

void testAnchorLabelId() {
    const TileId tile14(14, 8460, 5865);
    const Vertex anchor(0.3759765625f, 0.7509765625f); // exact in binary, so overzoom is exact too

    long long id = AnchorIdProbe::combineAnchorId(7, tile14, anchor);

    Vertex neighbour(anchor(0) + 40 * CELL_Z14, anchor(1));
    TEST_CHECK(AnchorIdProbe::combineAnchorId(7, tile14, neighbour) != id, "two points a few metres apart get different ids");

    // Same world point read for a zoom 16 target tile, or one POI becomes two while a zoom streams in.
    const TileId tile16(16, 33841, 23463);
    const Vertex anchor16(0.50390625f, 0.00390625f);
    TEST_CHECK(AnchorIdProbe::combineAnchorId(7, tile16, anchor16) == id, "overzoom levels of one point agree");

    // A MultiPoint's point inside the buffer of the next tile: one label, not one per tile.
    const TileId east14(14, 8461, 5865);
    TEST_CHECK(AnchorIdProbe::combineAnchorId(7, east14, Vertex(anchor(0) - 1.0f, anchor(1))) == id, "a point read from a sibling tile's buffer agrees");

    // Deliberate: two labels within one cell are one label.
    Vertex sameCell(anchor(0) + CELL_Z14 / 4, anchor(1));
    TEST_CHECK(AnchorIdProbe::combineAnchorId(7, tile14, sameCell) == id, "a sub-cell offset stays one id");

    TEST_CHECK(AnchorIdProbe::combineAnchorId(8, tile14, anchor) != id, "different symbolizers keep different ids");

    // A hash that dropped y would pass every check above.
    Vertex below(anchor(0), anchor(1) + 40 * CELL_Z14);
    TEST_CHECK(AnchorIdProbe::combineAnchorId(7, tile14, below) != id, "the anchor's y counts");
}
