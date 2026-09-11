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

void testTargetTileZoomHysteresis();
void testStyleTileZoomStaleness();

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

    testTargetTileZoomHysteresis();
    testStyleTileZoomStaleness();
}

/*
 * The target zoom itself, which is the input above. A change here re-decodes every visible tile, so
 * what matters is that it does NOT move for a wobble: in terrain mode the focus rides the ground,
 * and a 2D/3D switch measured at Zermatt drifted the zoom from 12.05 to 11.95 and back - a tenth of
 * a level, invisible, and it re-decoded the whole map twice on top of the switch's own decode.
 */
void testTargetTileZoomHysteresis() {
    const double H = 0.15;

    // With no target yet, the zoom is taken as it is.
    TEST_CHECK(calculateTargetTileZoom(12.05, -1, H) == 12, "an unset target takes the camera's level");
    TEST_CHECK(calculateTargetTileZoom(11.95, -1, H) == 11, "and does so below the boundary too");

    // Inside the level: nothing to decide.
    TEST_CHECK(calculateTargetTileZoom(12.05, 12, H) == 12, "a zoom inside the level holds it");
    TEST_CHECK(calculateTargetTileZoom(12.99, 12, H) == 12, "right up to the top of it");

    // The measured wobble, both halves of it.
    TEST_CHECK(calculateTargetTileZoom(11.95, 12, H) == 12, "a tenth of a level below the boundary holds");
    TEST_CHECK(calculateTargetTileZoom(11.97, 12, H) == 12, "and holds on the way back");
    TEST_CHECK(calculateTargetTileZoom(12.05, 11, H) == 11, "the same wobble the other way round holds too");

    // A real move still gets through, at the margin and no later.
    TEST_CHECK(calculateTargetTileZoom(11.85, 12, H) == 11, "clear of the margin, the level follows");
    TEST_CHECK(calculateTargetTileZoom(12.15, 11, H) == 12, "and upwards at the margin as well");
    TEST_CHECK(calculateTargetTileZoom(14.50, 11, H) == 14, "a jump lands where it lands, not one level on");

    // A zero margin is the old behaviour: the boundary is the boundary.
    TEST_CHECK(calculateTargetTileZoom(11.99, 12, 0.0) == 11, "a zero margin follows every crossing");
}

/*
 * Whether a tile already in the cache still styles the way the camera asks. The style zoom is
 * snapshotted when the fetch is QUEUED; a target-zoom change invalidates the cache but not the
 * tasks in flight, and those land afterwards looking fresh. That is how a tile decoded for zoom 13
 * survived a zoom-out to 11 and went on drawing its `[zoom>=12]` contours - time-based validity
 * cannot see it, the stamp can.
 */
void testStyleTileZoomStaleness() {
    // The ordinary case: queued and landed under the same target.
    TEST_CHECK(isStyleTileZoomCurrent(12, 12, 12, 2), "a tile decoded at the current target is current");
    TEST_CHECK(isStyleTileZoomCurrent(11, 13, 13, 2), "a lifted tile is current while the lift holds");

    // The bug: queued at target 13 (lift 2, so styled 13), landed after the camera went to 11.
    TEST_CHECK(!isStyleTileZoomCurrent(11, 13, 11, 2), "a tile styled for the zoom the camera left is stale");
    TEST_CHECK(!isStyleTileZoomCurrent(10, 12, 10, 2), "and so is one that outlived a smaller step");

    // Re-fetched under the new target, it matches again - so this converges instead of looping.
    TEST_CHECK(isStyleTileZoomCurrent(11, 11, 11, 2), "the re-decode settles it");

    // The lift moving is the same kind of staleness, and TileLayer already re-decodes for it.
    TEST_CHECK(!isStyleTileZoomCurrent(11, 13, 13, 0), "dropping the lift invalidates what the lift styled");

    // Past the lift a tile styles as itself, whatever the target does - no false staleness on the
    // horizon band, which is most of a tilted frame.
    TEST_CHECK(isStyleTileZoomCurrent(9, 9, 13, 2), "a tile past the lift stays current as the target moves");

    // Before the first cull the target is -1 and everything styles as itself; it must not read as
    // stale on the very first frame and re-fetch the whole map.
    TEST_CHECK(isStyleTileZoomCurrent(12, 12, -1, 2), "an unset target is not staleness");
}
