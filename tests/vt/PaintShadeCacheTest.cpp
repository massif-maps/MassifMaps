// When the terrain paint's shaded-DEM picture is current and when it must be drawn again
// (vt/PaintShadeCache.h). The GL side - that the picture is what the per-pixel shader gives - is a
// device check.

#include <vector>

#include "PaintShadeCache.h"

using namespace massif::vt;

#include "TestCheck.h"

namespace {
    // What the renderer does on a bake: acquire, shade if not current, mark.
    bool bake(PaintShadeCache& cache, unsigned int elevation, std::uint64_t version, std::size_t fingerprint, unsigned int& nextTexture, std::vector<unsigned int>& released) {
        bool current = false;
        PaintShadeCache::Entry& entry = cache.acquire(elevation, version, fingerprint, released, current);
        if (current) {
            return false;
        }
        if (entry.texture == 0) {
            entry.texture = nextTexture++;
        }
        PaintShadeCache::markShaded(entry, version, fingerprint);
        return true;
    }
}

void testPaintShadeCache() {
    PaintShadeCache cache(3);
    std::vector<unsigned int> released;
    unsigned int next = 100;

    TEST_CHECK(bake(cache, 7, 1, 11, next, released), "a first bake shades");
    TEST_CHECK(!bake(cache, 7, 1, 11, next, released), "the same elevation, content and paint is not shaded again");
    TEST_CHECK(!bake(cache, 7, 1, 11, next, released), "...however many drape tiles bake from it");

    TEST_CHECK(bake(cache, 7, 2, 11, next, released), "a border patch or a re-upload (new content version) shades again");
    TEST_CHECK(!bake(cache, 7, 2, 11, next, released), "...once");
    TEST_CHECK(bake(cache, 7, 2, 12, next, released), "a changed paint (light, contrast, exaggeration, colours: the fingerprint) shades again");
    TEST_CHECK(bake(cache, 7, 2, 11, next, released), "...and going back to the old one is a change too");

    bool current = false;
    unsigned int first = cache.acquire(7, 2, 11, released, current).texture;
    TEST_CHECK(bake(cache, 7, 3, 11, next, released), "new content shades");
    cache.acquire(7, 3, 11, released, current);
    TEST_CHECK(current && cache.acquire(7, 3, 11, released, current).texture == first, "a stale picture is redrawn into its own texture, not a new one");

    TEST_CHECK(bake(cache, 8, 1, 11, next, released), "another elevation texture has its own slot");
    TEST_CHECK(!bake(cache, 7, 3, 11, next, released), "...without disturbing the first");

    // A texture id the elevation cache recycles for other content is told apart by the version alone.
    TEST_CHECK(bake(cache, 8, 9, 11, next, released), "the same texture id with new content shades again");
    TEST_CHECK(released.empty(), "nothing is released below the capacity");

    PaintShadeCache small(2);
    std::vector<unsigned int> dropped;
    unsigned int id = 200;
    bake(small, 1, 1, 5, id, dropped); // texture 200
    bake(small, 2, 1, 5, id, dropped); // texture 201
    bake(small, 1, 1, 5, id, dropped); // touches the first
    bake(small, 3, 1, 5, id, dropped); // texture 202, pushing out the least recently used: the second
    TEST_CHECK(small.size() == 2, "the capacity holds");
    TEST_CHECK(dropped.size() == 1 && dropped[0] == 201, "the least recently used slot is pushed out, its texture handed back");
    TEST_CHECK(!bake(small, 1, 1, 5, id, dropped), "the one touched since is kept");
    TEST_CHECK(bake(small, 2, 1, 5, id, dropped), "the one pushed out shades again");
    TEST_CHECK(dropped.size() == 2 && dropped[1] == 202, "and the slot then least recently used makes room for it");

    std::vector<unsigned int> freed;
    cache.clear(freed);
    TEST_CHECK(cache.size() == 0 && freed.size() == 2, "clearing hands back every texture");
    cache.forget();
    TEST_CHECK(cache.size() == 0, "forgetting after a context loss leaves none");
}
