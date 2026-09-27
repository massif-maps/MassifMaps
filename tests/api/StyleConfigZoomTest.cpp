// The zoom a style slot's config is resolved at (vectortiles/StyleConfigZoom.h). With terrain the
// view zoom drifts every frame, so the cache key is quantised; mapnikvt selects rules on the
// integer zoom, so quantising must never cross an integer.

#include "vectortiles/StyleConfigZoom.h"

#include "TestCheck.h"

#include <cmath>
#include <limits>

using namespace massif;

void testStyleConfigZoom() {
    const float step = 1.0f / StyleConfigZoom::STEPS_PER_LEVEL;

    for (float zoom = 0.0f; zoom <= 24.0f; zoom += step / 4.0f) {
        if (static_cast<int>(StyleConfigZoom::quantise(zoom)) != static_cast<int>(zoom)) {
            TEST_CHECK(false, "quantising never moves the integer zoom the rules are selected at");
            return;
        }
    }
    TEST_CHECK(true, "quantising never moves the integer zoom the rules are selected at");

    TEST_CHECK(StyleConfigZoom::quantise(15.0f) == 15.0f, "a whole zoom level is unchanged");
    TEST_CHECK(StyleConfigZoom::quantise(15.0f + step) == 15.0f + step, "and so is an exact step");

    // Rounding up at 15.99 would select the rules of level 16.
    TEST_CHECK(StyleConfigZoom::quantise(15.99f) < 16.0f, "just under a level stays under it");
    TEST_CHECK(static_cast<int>(StyleConfigZoom::quantise(15.99f)) == 15, "at the level it came from");

    float drifted = StyleConfigZoom::quantise(15.5f + step * 0.4f);
    TEST_CHECK(drifted == StyleConfigZoom::quantise(15.5f), "a drift under one step keeps the key");
    TEST_CHECK(StyleConfigZoom::quantise(15.5f + step) != StyleConfigZoom::quantise(15.5f),
               "a drift of a full step does not");

    float once = StyleConfigZoom::quantise(17.3f);
    TEST_CHECK(StyleConfigZoom::quantise(once) == once, "quantising a quantised zoom is a no-op");

    float previous = -1000.0f;
    for (float zoom = 0.0f; zoom <= 24.0f; zoom += step / 3.0f) {
        float value = StyleConfigZoom::quantise(zoom);
        if (value < previous) {
            TEST_CHECK(false, "quantising is monotone in the zoom");
            return;
        }
        previous = value;
    }
    TEST_CHECK(true, "quantising is monotone in the zoom");

    for (float zoom = 0.0f; zoom <= 24.0f; zoom += step / 3.0f) {
        float value = StyleConfigZoom::quantise(zoom);
        if (value > zoom || zoom - value >= step) {
            TEST_CHECK(false, "a quantised zoom is within one step below the real one");
            return;
        }
    }
    TEST_CHECK(true, "a quantised zoom is within one step below the real one");

    // Free roam allows a negative zoom.
    TEST_CHECK(static_cast<int>(StyleConfigZoom::quantise(-0.9f)) == static_cast<int>(-0.9f),
               "a negative zoom keeps its integer");

    // NaN never equals itself, so a NaN key would never hit the cache.
    TEST_CHECK(std::isnan(StyleConfigZoom::quantise(std::numeric_limits<float>::quiet_NaN())),
               "a NaN zoom stays NaN");
    TEST_CHECK(std::isinf(StyleConfigZoom::quantise(std::numeric_limits<float>::infinity())),
               "an infinite zoom stays infinite");
}
