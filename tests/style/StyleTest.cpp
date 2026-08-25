/*
 * The style-side host tests: mapnikvt without the renderer. See ../README.md.
 */

#include "TestCheck.h"

int failures = 0;

void testLayerConfig();

int main() {
    testLayerConfig();

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
