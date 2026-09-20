/*
 * A tile whose features carry no id gives every one of them id 0, so a layer's point labels used
 * to share one global id and the renderer merged them into a single label. Symbolizer folds the
 * anchor in instead - these are the two properties that has to hold: two points a few metres apart
 * get different ids, and one point read at two overzoom levels gets the same id.
 */

#include "TestCheck.h"

#include <mapnikvt/Symbolizer.h>

using massif::vt::TileId;

namespace {
    using Vertex = cglib::vec2<float>;

    // combineAnchorId is protected, as combineId is: the symbolizers are its only callers.
    struct AnchorIdProbe final : massif::mvt::Symbolizer {
        AnchorIdProbe() : Symbolizer(std::shared_ptr<massif::mvt::Logger>()) { }

        FeatureProcessor createFeatureProcessor(const massif::mvt::ExpressionContext&, const massif::mvt::SymbolizerContext&) const override {
            return FeatureProcessor();
        }

        using Symbolizer::combineAnchorId;
    };

    // One anchor cell is 2^-28 of the world, which is 2^-14 of a zoom 14 tile.
    constexpr float CELL_Z14 = 1.0f / 16384.0f;
}

void testAnchorLabelId() {
    const TileId tile14(14, 8460, 5865);
    const Vertex anchor(0.3759765625f, 0.7509765625f); // exact in binary, so overzoom is exact too

    long long id = AnchorIdProbe::combineAnchorId(7, tile14, anchor);

    // The two POIs of the report are 3.4 m apart, which is ~30 cells; 40 is the same order.
    Vertex neighbour(anchor(0) + 40 * CELL_Z14, anchor(1));
    TEST_CHECK(AnchorIdProbe::combineAnchorId(7, tile14, neighbour) != id, "two points a few metres apart get different ids");

    // Same point, same source tile, read for a zoom 16 target tile: the world position is
    // unchanged, so the id has to be too - otherwise one POI becomes two while a zoom streams in.
    const TileId tile16(16, 33841, 23463);
    const Vertex anchor16(0.50390625f, 0.00390625f);
    TEST_CHECK(AnchorIdProbe::combineAnchorId(7, tile16, anchor16) == id, "overzoom levels of one point agree");

    // Below the cell there is deliberately no separation - two labels that close are one label.
    Vertex sameCell(anchor(0) + CELL_Z14 / 4, anchor(1));
    TEST_CHECK(AnchorIdProbe::combineAnchorId(7, tile14, sameCell) == id, "a sub-cell offset stays one id");

    // The fold narrows an id, it never widens it: two symbolizers sharing an anchor stay distinct.
    TEST_CHECK(AnchorIdProbe::combineAnchorId(8, tile14, anchor) != id, "different symbolizers keep different ids");

    // A vertical offset is not a horizontal one - a hash that dropped y would pass everything above.
    Vertex below(anchor(0), anchor(1) + 40 * CELL_Z14);
    TEST_CHECK(AnchorIdProbe::combineAnchorId(7, tile14, below) != id, "the anchor's y counts");
}
