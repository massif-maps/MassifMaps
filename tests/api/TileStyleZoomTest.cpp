/*
 * Tests for the style zoom of a tile (all/native/layers/TileStyleZoom.h), the zoom CartoCSS rules
 * are matched at once the LOD is free to hand back a tile coarser than the camera asked for.
 *
 * The rule gates on the TILE (TileReader::readTile sets the adjusted zoom from it), so with no lift
 * a single level of coarsening deletes every rule written for the camera's zoom - a converted
 * MapBox style's `#building[zoom >= 16]` is a VIEW-zoom gate in the original, and the far half of
 * a tilted view lost its buildings in one step, along a tile edge.
 *
 * The lift is BOUNDED, and that bound is the whole balance: unbounded, a horizon tile is styled at
 * the camera's zoom and emits the entire near-field content - every label, every arrow - over
 * ground tens of times wider, which was measurably slower on the demo.
 *
 * NOT covered here: that the decoded tile then carries those rules. That needs TileReader, which
 * pulls the symbolizers and vt's tile builders in (see ../README.md), and the layer side needs
 * Options and the renderer. Both are device checks - see the camera named in the PR.
 */

#include "layers/TileStyleZoom.h"

using namespace massif;

#include "TestCheck.h"

void testTileStyleZoom() {
    // The near field: the tile IS the zoom the camera asked for, so nothing moves. Every style that
    // renders correctly today does so at this case, and it has to stay byte-identical.
    TEST_CHECK(calculateStyleTileZoom(14, 14, 2) == 14, "a tile at the target zoom styles as itself");

    // The whole point: the first coarsening steps still match the rules of the camera's zoom, which
    // is where the visible edge was.
    TEST_CHECK(calculateStyleTileZoom(15, 16, 2) == 16, "one level of coarsening keeps the camera's style zoom");
    TEST_CHECK(calculateStyleTileZoom(14, 16, 2) == 16, "two levels keep it too");

    // And the horizon band does not: past the lift a tile keeps its OWN zoom, not a partial lift.
    // Snap-or-nothing, because a partial lift still emits near-field content over ground tens of
    // times wider, which is what made the first attempt slow at a grazing tilt.
    TEST_CHECK(calculateStyleTileZoom(13, 16, 2) == 13, "past the lift the tile keeps its own zoom");
    TEST_CHECK(calculateStyleTileZoom(11, 16, 2) == 11, "the horizon band pays nothing at all");
    TEST_CHECK(calculateStyleTileZoom(15, 16, 0) == 15, "a zero lift is the old tile-zoom behaviour");

    // A tile FINER than the target keeps its own zoom - it can happen while a cull that asked for a
    // deeper level is still draining, and styling it coarser would drop rules it does carry.
    TEST_CHECK(calculateStyleTileZoom(15, 14, 2) == 15, "a tile finer than the target styles as itself");

    // Before the first cull the target is -1, and every tile has to style as itself rather than
    // collapse to zoom 0.
    TEST_CHECK(calculateStyleTileZoom(12, -1, 2) == 12, "an unset target leaves the tile's own zoom");
}
