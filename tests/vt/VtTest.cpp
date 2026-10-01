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
void testExtrusionGroundSkirtClip();
void testLineJoinReach();
void testExtrusionBase();
void testExtrusionFloor();
void testLabelDistance();
void testSpanGeometry();
void testShadowCasterClip();
void testShadowBox();
void testShaderFlags();
void testExtrusionAnchor();
void testExtrusionEmissive();
void testExtrusionGroupAnchor();
void testSpanDrapeLight();
void testSpanResolver();
void testSphericalTerrain();
void testExtrusionOccluder();
void testGridIndexOrder();
void testRenderTileBlend();
void testLabelSlice();
void testLabelFade();
void testLayerContentFlags();
void testDrawOnce();
void testViewStateMatrix();
void testLabelNormalBuild();
void testNormalMapSlope();
void testCollisionPadding();
void testLabelAnchorAlign();
void testLabelElevationAnchor();
void testLabelPadding();
void testLabelPerspective();
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
    testExtrusionGroundSkirtClip();
    testLineJoinReach();
    testExtrusionBase();
    testExtrusionFloor();
    testLabelDistance();
    testSpanGeometry();
    testShadowCasterClip();
    testShadowBox();
    testShaderFlags();
    testExtrusionAnchor();
    testExtrusionEmissive();
    testExtrusionGroupAnchor();
    testSpanDrapeLight();
    testSpanResolver();
    testSphericalTerrain();
    testExtrusionOccluder();
    testGridIndexOrder();
    testRenderTileBlend();
    testLabelSlice();
    testLabelFade();
    testLayerContentFlags();
    testDrawOnce();
    testViewStateMatrix();
    testLabelNormalBuild();
    testNormalMapSlope();
    testCollisionPadding();
    testLabelAnchorAlign();
    testLabelElevationAnchor();
    testLabelPadding();
    testLabelPerspective();
    testLabelBandTiles();
    testLabelTextOpacity();
    testLabelRadialOffset();
    testLabelEdgeOffset();

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
