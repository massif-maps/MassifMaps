/*
 * The vt-side host tests: what of the renderer links without the renderer. See ../README.md.
 */

#include "TestCheck.h"

int failures = 0;

void testPlateBitmap();
void testLineLabel();
void testExtrusionCorner();
void testExtrusionRingOrientation();
void testExtrusionBevel();
void testLineJoinReach();
void testExtrusionBase();
void testSpanGeometry();
void testShadowCasterClip();
void testExtrusionAnchor();
void testExtrusionEmissive();
void testExtrusionGroupAnchor();
void testSpanDrapeLight();
void testSpanResolver();
void testCollisionPadding();
void testLabelAnchorAlign();
void testLabelPadding();
void testLabelTextOpacity();
void testLabelRadialOffset();
void testLabelEdgeOffset();

int main() {
    testPlateBitmap();
    testLineLabel();
    testExtrusionCorner();
    testExtrusionRingOrientation();
    testExtrusionBevel();
    testLineJoinReach();
    testExtrusionBase();
    testSpanGeometry();
    testShadowCasterClip();
    testExtrusionAnchor();
    testExtrusionEmissive();
    testExtrusionGroupAnchor();
    testSpanDrapeLight();
    testSpanResolver();
    testCollisionPadding();
    testLabelAnchorAlign();
    testLabelPadding();
    testLabelTextOpacity();
    testLabelRadialOffset();
    testLabelEdgeOffset();

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
