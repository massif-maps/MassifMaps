// When a layer retained for the cross-fade gives up its ground (vt/RenderTileBlend.h). A timer fade
// leaves a gap while the new tile is still fetching; holding until covered strands the layer forever
// when the new tile arrived without it.

#include "RenderTileBlend.h"

using namespace massif::vt;

#include "TestCheck.h"

void testRenderTileBlend() {
    // Fading both at once leaves coverage at blend + (1-blend)^2, a blink at every zoom step.
    TEST_CHECK(!retainedLayerFades(true, true, true, true), "a layer under a still-fading replacement holds");
    TEST_CHECK(!retainedLayerFades(true, false, false, false), "and holds off screen too");

    // Held ground: on screen, nothing active in the tile, and the tile not of the current set yet.
    TEST_CHECK(!retainedLayerFades(false, true, false, false), "the only thing painting held ground holds");

    TEST_CHECK(retainedLayerFades(false, true, true, false), "a tile that has its own content lets the old go");

    // Off screen the caller's blend delta is 1, so it goes in one step.
    TEST_CHECK(retainedLayerFades(false, false, false, false), "a tile that left the view fades");

    // A current-set tile that decoded without this layer is the answer, not a gap.
    TEST_CHECK(retainedLayerFades(false, true, false, true), "a current tile without the layer is an answer, not a gap");
}
