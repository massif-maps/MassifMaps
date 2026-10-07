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
void testTargetTileZoomSettlesAtRest();
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
    testTargetTileZoomSettlesAtRest();
    testStyleTileZoomStaleness();
}

// A target-zoom change re-decodes every visible tile, and in terrain mode the zoom wobbles around
// a boundary (12.05 -> 11.95 and back), so it needs hysteresis.
void testTargetTileZoomHysteresis() {
    const double H = 0.15;

    TEST_CHECK(calculateTargetTileZoom(12.05, -1, H) == 12, "an unset target takes the camera's level");
    TEST_CHECK(calculateTargetTileZoom(11.95, -1, H) == 11, "and does so below the boundary too");

    TEST_CHECK(calculateTargetTileZoom(12.05, 12, H) == 12, "a zoom inside the level holds it");
    TEST_CHECK(calculateTargetTileZoom(12.99, 12, H) == 12, "right up to the top of it");

    TEST_CHECK(calculateTargetTileZoom(11.95, 12, H) == 12, "a tenth of a level below the boundary holds");
    TEST_CHECK(calculateTargetTileZoom(11.97, 12, H) == 12, "and holds on the way back");
    TEST_CHECK(calculateTargetTileZoom(12.05, 11, H) == 11, "the same wobble the other way round holds too");

    TEST_CHECK(calculateTargetTileZoom(11.85, 12, H) == 11, "clear of the margin, the level follows");
    TEST_CHECK(calculateTargetTileZoom(12.15, 11, H) == 12, "and upwards at the margin as well");
    TEST_CHECK(calculateTargetTileZoom(14.50, 11, H) == 14, "a jump lands where it lands, not one level on");

    TEST_CHECK(calculateTargetTileZoom(11.99, 12, 0.0) == 11, "a zero margin follows every crossing");
}

// The bug: the terrain-3d example opens at 11.5, the launch camera then sets 12.05, and the margin kept z11 tiles for
// good - on the Crosscall the same camera settled on 8 z11 or 16 z12 tiles depending on whether a cull fell in between.
void testTargetTileZoomSettlesAtRest() {
    const double H = 0.15;
    bool held = false;

    int target = settleTargetTileZoom(12.05, 11, H, false, held);
    TEST_CHECK(target == 11 && held, "moving into the margin keeps the level, and says it is held");
    target = settleTargetTileZoom(12.05, target, H, true, held);
    TEST_CHECK(target == 12 && !held, "the same view culled again takes the camera's own level");

    // The 2D/3D switch at Zermatt: the focus drifts 12.05 -> 11.95 -> 12.05, a new view every frame.
    target = settleTargetTileZoom(11.95, 12, H, false, held);
    TEST_CHECK(target == 12 && held, "a wobble while the view moves still holds");
    target = settleTargetTileZoom(12.05, target, H, false, held);
    TEST_CHECK(target == 12 && !held, "and back over the boundary nothing changed");

    target = settleTargetTileZoom(11.95, 12, H, true, held);
    TEST_CHECK(target == 11 && !held, "a wobble that stops below the boundary settles on the level below");
}

// Whether a cached tile still styles the way the camera asks. Tasks in flight survive a
// target-zoom change and land looking fresh; time-based validity cannot see that, the stamp can.
void testStyleTileZoomStaleness() {
    TEST_CHECK(isStyleTileZoomCurrent(12, 12, 12, 2), "a tile decoded at the current target is current");
    TEST_CHECK(isStyleTileZoomCurrent(11, 13, 13, 2), "a lifted tile is current while the lift holds");

    // Queued at target 13 (lift 2, so styled 13), landed after the camera went to 11.
    TEST_CHECK(!isStyleTileZoomCurrent(11, 13, 11, 2), "a tile styled for the zoom the camera left is stale");
    TEST_CHECK(!isStyleTileZoomCurrent(10, 12, 10, 2), "and so is one that outlived a smaller step");

    TEST_CHECK(isStyleTileZoomCurrent(11, 11, 11, 2), "the re-decode settles it");

    TEST_CHECK(!isStyleTileZoomCurrent(11, 13, 13, 0), "dropping the lift invalidates what the lift styled");

    // No false staleness on the horizon band, which is most of a tilted frame.
    TEST_CHECK(isStyleTileZoomCurrent(9, 9, 13, 2), "a tile past the lift stays current as the target moves");

    // Target -1 before the first cull must not re-fetch the whole map.
    TEST_CHECK(isStyleTileZoomCurrent(12, 12, -1, 2), "an unset target is not staleness");

    // Why VectorTileLayer::FetchTask::loadTile reads the target at decode time, not queue time.
    TEST_CHECK(calculateStyleTileZoom(17, 19, 2) == 19, "queued under the old target, it styled at 19");
    TEST_CHECK(!isStyleTileZoomCurrent(17, 19, 17, 2), "which the settled camera then threw away");
    TEST_CHECK(calculateStyleTileZoom(17, 17, 2) == 17, "read at decode time it styles at 17 instead");
    TEST_CHECK(isStyleTileZoomCurrent(17, 17, 17, 2), "and is not re-decoded at all");

    TEST_CHECK(calculateStyleTileZoom(15, 17, 2) == 17, "zooming in, a coarse tile still lifts");

    // A crossing stales only the lifted tiles, which is why a per-tile stamp beats wiping the cache.
    TEST_CHECK(!isStyleTileZoomCurrent(15, 17, 16, 2), "a lifted tile is staled by the crossing");
    TEST_CHECK(isStyleTileZoomCurrent(16, 16, 16, 2), "a tile at the new target is untouched");
    TEST_CHECK(isStyleTileZoomCurrent(13, 13, 16, 2), "and so is one past the lift");

    TEST_CHECK(isStyleTileZoomCurrent(15, 15, 17, 0), "at lift 0 a crossing stales nothing");
    TEST_CHECK(calculateStyleTileZoom(15, 17, 0) == 15, "because every tile styles at its own zoom");
}
