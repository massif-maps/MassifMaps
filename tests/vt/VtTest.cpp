/*
 * The vt-side host tests: what of the renderer links without the renderer. See ../README.md.
 */

#include "TestCheck.h"

int failures = 0;

void testPlateBitmap();
void testPlateBox();
void testLineLabel();
void testExtrusionCorner();
void testExtrusionRingOrientation();
void testExtrusionBevel();
void testLineJoinReach();
void testExtrusionBase();
void testExtrusionFloor();
void testLabelDistance();
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
void testLabelBandTiles();
void testLabelTextOpacity();
void testLabelRadialOffset();
void testLabelEdgeOffset();

int main() {
    testPlateBitmap();
    testPlateBox();
    testLineLabel();
    testExtrusionCorner();
    testExtrusionRingOrientation();
    testExtrusionBevel();
    testLineJoinReach();
    testExtrusionBase();
    testExtrusionFloor();
    testLabelDistance();
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
    testLabelBandTiles();
    testLabelTextOpacity();
    testLabelRadialOffset();
    testLabelEdgeOffset();

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
