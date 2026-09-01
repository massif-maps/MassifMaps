/*
 * The vt-side host tests: what of the renderer links without the renderer. See ../README.md.
 */

#include "TestCheck.h"

int failures = 0;

void testPlateBitmap();
void testLineLabel();
void testExtrusionCorner();
void testExtrusionRingOrientation();
void testLineJoinReach();
void testExtrusionBase();

int main() {
    testPlateBitmap();
    testLineLabel();
    testExtrusionCorner();
    testExtrusionRingOrientation();
    testLineJoinReach();
    testExtrusionBase();

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
