/*
 * The vt-side host tests: the renderer's header-only maths, without the renderer. See ../README.md.
 */

#include "TestCheck.h"

int failures = 0;

void testPlateBitmap();

int main() {
    testPlateBitmap();

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
