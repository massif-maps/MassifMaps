/*
 * When a render layer retained for the cross-fade gives up its ground (vt/RenderTileBlend.h).
 *
 * The whole file is one rule with four inputs, and it has been wrong in both directions:
 * - fading on a timer stranded ground, because the tile meant to take over was still being
 *   fetched and a fetch does not fit in the ten frames a fade lasts (buildings vanishing while
 *   pinching z17-19);
 * - holding whenever nothing active covered the ground stranded the layer FOREVER when the new
 *   tile had arrived and simply did not carry it - a zoom-out from z13 to z10 kept drawing the z12
 *   tiles' `#contour[zoom>=12]` lines, and no further zoom cleared them.
 *
 * NOT covered here: that the blend then reaches 0 and the layer is erased, and that the rendered
 * frame loses the lines. Both need GL - see the camera named in the PR.
 */

#include "RenderTileBlend.h"

using namespace massif::vt;

#include "TestCheck.h"

void testRenderTileBlend() {
    // Replaced: an active layer of the same style layer already covers this ground and is still
    // fading IN. Fading both at once leaves coverage at blend + (1-blend)^2 - a blink at every zoom
    // step - so this one holds until the active layer is opaque and erases it outright.
    TEST_CHECK(!retainedLayerFades(true, true, true, true), "a layer under a still-fading replacement holds");
    TEST_CHECK(!retainedLayerFades(true, false, false, false), "and holds off screen too");

    // The other hold: on screen, nothing active in the tile at all, and the tile is NOT of the
    // current set - ground the new cull did not name, whose tile is still coming.
    TEST_CHECK(!retainedLayerFades(false, true, false, false), "the only thing painting held ground holds");

    // The same tile with an active layer of its own: the replacement is here, so this fades.
    TEST_CHECK(retainedLayerFades(false, true, true, false), "a tile that has its own content lets the old go");

    // Off screen it goes in one step, held ground or not - the caller's blend delta is 1 there.
    TEST_CHECK(retainedLayerFades(false, false, false, false), "a tile that left the view fades");

    // The contour bug: a render tile of the CURRENT set that decoded without this layer. Same
    // three other inputs as the hold above, and it must fade - the tile IS the answer.
    TEST_CHECK(retainedLayerFades(false, true, false, true), "a current tile without the layer is an answer, not a gap");
}
