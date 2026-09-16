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
void testFontNames();
void testValueJSON();

int main() {
    testLayerConfig();
    testCartoCSSParse();
    testCSSColor();
    testStyleParameterFold();
    testExpressionRoundTrip();
    testDataDrivenProperty();
    testInterpolateExpression();
    testViewStateProperty();
    testFontNames();
    testValueJSON();

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
