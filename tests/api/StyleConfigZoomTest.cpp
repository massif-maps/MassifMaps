/*
 * The zoom a style slot's config is resolved at (vectortiles/StyleConfigZoom.h).
 *
 * Why it is quantised at all, measured on the Crosscall panning in 3D:
 *   PROF PRELUDE: 254.0 ms | ... paintTiles 249.6 (layers 249.1 [lock 0.0]) tail 0.1
 * 249 ms of WORK, no lock wait: every slot's config re-resolved in one frame. Resolving one walks
 * the style and evaluates its rules, ~8 ms a slot, and the render thread asks for every slot every
 * frame. It was already memoised on the exact view zoom - which with terrain moves every frame,
 * because the camera clearance bound is rebuilt from the ground height under the camera and pans
 * over a slope drift it. In 2D the zoom is bit-stable while panning, which is exactly why 2D never
 * showed this.
 *
 * The property that makes flooring safe rather than merely cheap: mapnikvt selects which rules match
 * on the INTEGER zoom, so a quantised zoom must never land in a different integer than the zoom it
 * came from. A config property that interpolates with the view zoom does step, by one step.
 *
 * NOT covered here: that the resolve is expensive (mapnikvt), and that the cache is dropped when the
 * style or a live parameter changes - see CompositeVectorTileLayer::resolveLayerConfigCached and
 * VectorTileDecoder::getConfigVersion.
 */

#include "vectortiles/StyleConfigZoom.h"

#include "TestCheck.h"

#include <cmath>
#include <limits>

using namespace massif;

void testStyleConfigZoom() {
    const float step = 1.0f / StyleConfigZoom::STEPS_PER_LEVEL;

    // THE safety property, over the whole usable zoom range: quantising never changes the integer
    // zoom, so the rules selected at it cannot change. Checked at the dangerous places - just under
    // and just over a level boundary - and everywhere in between.
    for (float zoom = 0.0f; zoom <= 24.0f; zoom += step / 4.0f) {
        if (static_cast<int>(StyleConfigZoom::quantise(zoom)) != static_cast<int>(zoom)) {
            TEST_CHECK(false, "quantising never moves the integer zoom the rules are selected at");
            return;
        }
    }
    TEST_CHECK(true, "quantising never moves the integer zoom the rules are selected at");

    // An exact step, and an exact level, are left alone: the common case must not drift.
    TEST_CHECK(StyleConfigZoom::quantise(15.0f) == 15.0f, "a whole zoom level is unchanged");
    TEST_CHECK(StyleConfigZoom::quantise(15.0f + step) == 15.0f + step, "and so is an exact step");

    // Rounded DOWN, never up: rounding up at 15.99 would select the rules of level 16.
    TEST_CHECK(StyleConfigZoom::quantise(15.99f) < 16.0f, "just under a level stays under it");
    TEST_CHECK(static_cast<int>(StyleConfigZoom::quantise(15.99f)) == 15, "at the level it came from");

    // The point of the whole thing: a drift smaller than a step gives the SAME key, which is what
    // turns a per-frame re-resolve into a hit.
    float drifted = StyleConfigZoom::quantise(15.5f + step * 0.4f);
    TEST_CHECK(drifted == StyleConfigZoom::quantise(15.5f), "a drift under one step keeps the key");
    TEST_CHECK(StyleConfigZoom::quantise(15.5f + step) != StyleConfigZoom::quantise(15.5f),
               "a drift of a full step does not");

    // Idempotent, or a value resolved at a quantised zoom would re-quantise to another key.
    float once = StyleConfigZoom::quantise(17.3f);
    TEST_CHECK(StyleConfigZoom::quantise(once) == once, "quantising a quantised zoom is a no-op");

    // Monotone: zooming in never reports a lower zoom to the style.
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

    // Never above the zoom it came from, and never a whole step below it.
    for (float zoom = 0.0f; zoom <= 24.0f; zoom += step / 3.0f) {
        float value = StyleConfigZoom::quantise(zoom);
        if (value > zoom || zoom - value >= step) {
            TEST_CHECK(false, "a quantised zoom is within one step below the real one");
            return;
        }
    }
    TEST_CHECK(true, "a quantised zoom is within one step below the real one");

    // A negative zoom exists (free roam), and must not be mapped into a different integer either.
    TEST_CHECK(static_cast<int>(StyleConfigZoom::quantise(-0.9f)) == static_cast<int>(-0.9f),
               "a negative zoom keeps its integer");

    // Non-finite is passed through rather than turned into a key that can never match again: NaN
    // compares false against itself, so a NaN in the cache would re-resolve for ever.
    TEST_CHECK(std::isnan(StyleConfigZoom::quantise(std::numeric_limits<float>::quiet_NaN())),
               "a NaN zoom stays NaN");
    TEST_CHECK(std::isinf(StyleConfigZoom::quantise(std::numeric_limits<float>::infinity())),
               "an infinite zoom stays infinite");
}
