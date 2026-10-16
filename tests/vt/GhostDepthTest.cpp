// A ghosted line (line-occlusion-opacity) is drawn GL_GREATER after the extrusions, so it passes
// wherever the depth buffer holds something nearer than it. At its own ordinal it passed against a
// LATER layer's decal too (a maneuver arrow over the route, the shaft over the casing) and painted its
// translucent colour over it. ghostLayerOffset puts it at the stack's top ordinal instead.
// Not covered: the draw itself and the polygon-offset margin (GL) - a device check.

#include "GhostDepth.h"

#include "TestCheck.h"

using namespace massif::vt;

namespace {
    // applyDepthBias: z += offset * (2^-19 * w + shift); smaller z is nearer. Only the offset term, as
    // every draw of a tile shares the rest.
    float depthShift(float offset, float w) {
        return offset * (1.0f / 524288.0f * w + 0.02f);
    }
}

void testGhostDepth() {
    const int top = 12;
    for (float w : { 5.0f, 400.0f, 90000.0f }) {
        bool nearerThanEveryDecal = true;
        bool ownDecalLoses = true;
        for (int ordinal = 1; ordinal <= top; ordinal++) {
            float ghost = depthShift(ghostLayerOffset(0.0f, ordinal, top), w);
            for (int decalOrdinal = 1; decalOrdinal <= top; decalOrdinal++) {
                // GL_GREATER passes only where the buffer is nearer than the fragment.
                if (depthShift(-static_cast<float>(decalOrdinal), w) < ghost) {
                    nearerThanEveryDecal = false;
                }
            }
            ownDecalLoses = ownDecalLoses && ghost <= depthShift(-static_cast<float>(ordinal), w);
        }
        TEST_CHECK(nearerThanEveryDecal, "a ghost of any layer is nearer than or level with every decal of the stack, so none passes it");
        TEST_CHECK(ownDecalLoses, "a ghost is never farther than its own layer's decal");
    }

    TEST_CHECK(ghostLayerOffset(0.0f, 3, 12) == ghostLayerOffset(0.0f, 11, 12), "every layer's ghost is made at the same ordinal");
    TEST_CHECK(ghostLayerOffset(0.0f, 3, 0) == -3.0f, "a top not published yet keeps the layer's own ordinal");
    TEST_CHECK(ghostLayerOffset(0.0f, 9, 4) == -9.0f, "a stale top below the layer's own never pushes the ghost back");
    TEST_CHECK(ghostLayerOffset(2.0f, 3, 12) == 2.0f - 12.0f, "a stand-in tile's ghost stays pushed back by its proxy depth");

    // The sweeps that can show anything: nothing in front of a line, nothing to draw.
    TEST_CHECK(ghostSweeps(true, false, false) == 0, "flat map, no extrusion drawn: no ghost pass at all");
    TEST_CHECK(ghostSweeps(false, false, false) == 0, "and none without a stencil buffer either");
    TEST_CHECK(ghostSweeps(true, true, false) == GHOST_SWEEP_STAMP, "buildings on a flat map: the stamp sweep only");
    TEST_CHECK(ghostSweeps(true, false, true) == GHOST_SWEEP_DEPTH, "terrain without buildings: the ridges, by depth");
    TEST_CHECK(ghostSweeps(true, true, true) == (GHOST_SWEEP_STAMP | GHOST_SWEEP_DEPTH), "terrain with buildings: both");
    TEST_CHECK(ghostSweeps(false, true, false) == GHOST_SWEEP_DEPTH, "without a stencil buffer the depth test serves buildings too");
}
