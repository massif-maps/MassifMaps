/*
 * The style-side host tests: mapnikvt without the renderer. See ../README.md.
 */

#include "TestCheck.h"

int failures = 0;

void testLayerConfig();
void testExpressionRoundTrip();
void testDataDrivenProperty();
void testInterpolateExpression();

int main() {
    testLayerConfig();
    testExpressionRoundTrip();
    testDataDrivenProperty();
    testInterpolateExpression();

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
