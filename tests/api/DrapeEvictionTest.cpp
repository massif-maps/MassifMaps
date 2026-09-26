// What the drape cache may evict over budget (terrain/DrapeEviction.h). A mask is part of its
// colour drape: evicting it forces a re-bake of the whole tile.

#include "terrain/DrapeEviction.h"

#include "TestCheck.h"

using namespace massif;

void testDrapeEviction() {
    const int COLOUR = 0, MASK = 1, MASK2 = 2;

    TEST_CHECK(!DrapeEviction::isEvictable(COLOUR, true, true), "a colour drape used this frame stays");
    TEST_CHECK(!DrapeEviction::isEvictable(MASK, true, true), "a mask used this frame stays");

    // isBaked reads the mask without marking it used, so an idle mask can still be needed.
    TEST_CHECK(!DrapeEviction::isEvictable(MASK, false, true), "an idle mask of a cached tile stays");
    TEST_CHECK(!DrapeEviction::isEvictable(MASK2, false, true), "and so does the second mask of the stack");

    TEST_CHECK(DrapeEviction::isEvictable(MASK, false, false), "a mask whose drape is gone is evictable");

    TEST_CHECK(DrapeEviction::isEvictable(COLOUR, false, true), "an idle colour drape is evictable");
    TEST_CHECK(DrapeEviction::isEvictable(COLOUR, false, false), "colourCached says nothing about stack 0 itself");

    // One mask per drape cut (#175); every stack index follows the same rule.
    for (int stack = 1; stack <= 8; stack++) {
        if (DrapeEviction::isEvictable(stack, false, true)) {
            TEST_CHECK(false, "no mask of a cached tile is evictable, at any stack index");
            return;
        }
        if (!DrapeEviction::isEvictable(stack, false, false)) {
            TEST_CHECK(false, "every orphan mask is evictable, at any stack index");
            return;
        }
    }
    TEST_CHECK(true, "the rule holds across the whole mask stack");
}
