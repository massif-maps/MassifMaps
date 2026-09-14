/*
 * What the drape cache may throw away when it is over budget (terrain/DrapeEviction.h).
 *
 * The bug this exists for, measured on the Crosscall while panning in 3D with nothing else moving:
 *   drapeCache entries=41 colour=21/24 bytes=106496/98304 res=1024
 *   drape evicted colour=7 mask=9 | maskAcquireFail=0
 *   drape queued blank=0 restack=0 standIn=0 partial=0 stale=184, bakes=64, totalMs=207
 * The cache sat permanently ~8 MB over its byte budget, so the eviction pass ran every frame. It
 * took the coverage masks of tiles that were ON SCREEN - a mask is a quarter of a drape in bytes
 * and looks like the cheapest thing to drop - and the owner re-bakes the WHOLE tile to bring a
 * missing mask back. One megabyte reclaimed, a 3 ms bake spent undoing it, ~23 tiles a frame, and
 * the queue could never drain: 8 bakes got through per frame against 23 newly stale.
 *
 * So a mask is not evictable while its own colour drape is cached. It is part of that drape.
 *
 * NOT covered here: the byte budget itself. Once the loop stops, 21 live tiles at 4 MB plus their
 * masks still exceed 96 MB, so the cache stays over budget - it simply no longer fights itself.
 * Whether that is answered by a bigger budget or a smaller resolution is a memory decision, named
 * in the PR.
 */

#include "terrain/DrapeEviction.h"

#include "TestCheck.h"

using namespace massif;

void testDrapeEviction() {
    const int COLOUR = 0, MASK = 1, MASK2 = 2;

    // Read this frame: never a candidate, whatever it is. The cache relied on this before and still
    // does - it is the only thing protecting the tile being drawn right now.
    TEST_CHECK(!DrapeEviction::isEvictable(COLOUR, true, true), "a colour drape used this frame stays");
    TEST_CHECK(!DrapeEviction::isEvictable(MASK, true, true), "a mask used this frame stays");

    // The bug, in one line: an idle mask of a tile whose picture is still cached must NOT go. It
    // was not read this frame because the read that needs it (isBaked) does not mark it used.
    TEST_CHECK(!DrapeEviction::isEvictable(MASK, false, true), "an idle mask of a cached tile stays");
    TEST_CHECK(!DrapeEviction::isEvictable(MASK2, false, true), "and so does the second mask of the stack");

    // An orphan is the opposite case and the first thing to drop: its drape is already gone, so
    // nothing will ever read it and no bake is undone by losing it.
    TEST_CHECK(DrapeEviction::isEvictable(MASK, false, false), "a mask whose drape is gone is evictable");

    // A colour drape not read this frame is what the budget is meant to reclaim - the previous
    // generation, kept only because it comes back constantly while panning.
    TEST_CHECK(DrapeEviction::isEvictable(COLOUR, false, true), "an idle colour drape is evictable");
    TEST_CHECK(DrapeEviction::isEvictable(COLOUR, false, false), "colourCached says nothing about stack 0 itself");

    // The rule never depends on which mask of the stack it is: #175 bakes one per drape cut, and a
    // tile with three cuts must not have its third mask treated differently from its first.
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
