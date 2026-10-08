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
void testLineEndArrowClip();
void testExtrusionBase();
void testExtrusionFloor();
void testExtrusionFloorClip();
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
void testSurfaceTransformerSwap();
void testRenderTileBlend();
void testGroundCover();
void testLabelSlice();
void testLabelFade();
void testLayerContentFlags();
void testDrawOnce();
void testPointGeoPosIndex();
void testViewStateMatrix();
void testLabelNormalBuild();
void testNormalMapSlope();
void testLabelGroupDistance();
void testCollisionPadding();
void testLabelSplitBox();
void testLabelAnchorAlign();
void testLabelElevationAnchor();
void testLabelPadding();
void testLabelPerspective();
void testLabelBandTiles();
void testLabelTextOpacity();
void testLabelRadialOffset();
void testLabelEdgeOffset();
void testLabelYield();

int main() {
    testPlateBitmap();
    testPlateBox();
    testLineLabel();
    testExtrusionCorner();
    testExtrusionRingOrientation();
    testExtrusionBevel();
    testExtrusionGroundSkirtClip();
    testLineJoinReach();
    testLineEndArrowClip();
    testExtrusionBase();
    testExtrusionFloor();
    testExtrusionFloorClip();
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
    testSurfaceTransformerSwap();
    testRenderTileBlend();
    testGroundCover();
    testLabelSlice();
    testLabelFade();
    testLayerContentFlags();
    testDrawOnce();
    testPointGeoPosIndex();
    testViewStateMatrix();
    testLabelNormalBuild();
    testNormalMapSlope();
    testLabelGroupDistance();
    testCollisionPadding();
    testLabelSplitBox();
    testLabelAnchorAlign();
    testLabelElevationAnchor();
    testLabelPadding();
    testLabelPerspective();
    testLabelBandTiles();
    testLabelTextOpacity();
    testLabelRadialOffset();
    testLabelEdgeOffset();
    testLabelYield();

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
