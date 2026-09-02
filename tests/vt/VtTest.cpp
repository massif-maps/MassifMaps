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

int main() {
    testPlateBitmap();
    testLineLabel();
    testExtrusionCorner();
    testExtrusionRingOrientation();
    testExtrusionBevel();
    testLineJoinReach();

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
