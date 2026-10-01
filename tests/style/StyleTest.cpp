/*
 * The style-side host tests: mapnikvt without the renderer. See ../README.md.
 */

#include "TestCheck.h"

int failures = 0;

void testLayerConfig();
void testCartoCSSParse();
void testCSSColor();
void testStyleParameterFold();
void testExpressionRoundTrip();
void testDataDrivenProperty();
void testInterpolateExpression();
void testViewStateProperty();
void testContextFold();
void testFontNames();
void testValueJSON();
void testAnchorLabelId();
void testSymbolizerProperty();
void testLegendResolver();
void testBuiltinParameters();
void testLineAnchors();
void testMBVTSubtile();
void testMBVTGeometryBounds();
void testZoomShift();
void testDrawOnce();

int main() {
    testLayerConfig();
    testCartoCSSParse();
    testCSSColor();
    testStyleParameterFold();
    testExpressionRoundTrip();
    testDataDrivenProperty();
    testInterpolateExpression();
    testViewStateProperty();
    testContextFold();
    testFontNames();
    testValueJSON();
    testAnchorLabelId();
    testSymbolizerProperty();
    testLegendResolver();
    testBuiltinParameters();
    testLineAnchors();
    testMBVTSubtile();
    testMBVTGeometryBounds();
    testZoomShift();
    testDrawOnce();

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
