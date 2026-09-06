/*
 * The light a DECK's roof puts on its drape (SpanDrapeLight, polygon3DFsh's uSpanDrapeLight).
 *
 * Why it exists: a bridge deck's roof shows the span drape - the road, baked exactly as the ground
 * bakes it. Composited before applyLighting3D, that finished ground pixel went through the
 * EXTRUSION's light as well: an ambient/sun sum in linear space plus the roof-shade term, which is
 * a facade model applied to a piece of road. At noon that factor is ~1 and nothing shows; at night
 * it left a deck at a quarter of the road it carries (Pont Neuf, z17.5, hour 22: deck 37 against a
 * quay road at 101 - the bridge darker than the water under it).
 *
 * So the drape is composited AFTER the lighting and takes the GROUND's light instead, which is what
 * this resolves. It is a CPU value because a deck is flat: the ground's per-fragment N.L collapses
 * to the sun's own height, so there is one value per frame rather than a term in the shader.
 *
 * The case that matters most is `colors-prelit`. Those styles carry the light in the colours
 * already and TileRenderer::buildTerrainLighting hands the ground a NEUTRAL light rather than none
 * (the ground's shadow multiply lives inside the same block, so switching it off would take the
 * shadow with it). That has to come out at exactly 1 here, or every converted style's deck is
 * wrong again in the other direction.
 *
 * NOT covered here: the shadow. It stays a per-fragment term in polygon3DFsh, multiplied over this
 * one exactly as backgroundFsh multiplies it over the ground's.
 */

#include "SpanDrapeLight.h"

#include "TestCheck.h"

using namespace massif::vt;

namespace {
    const cglib::vec3<float> WHITE(1.0f, 1.0f, 1.0f);
    const cglib::vec3<float> NOON(0.0f, 0.0f, 1.0f);   // sun overhead
    const cglib::vec3<float> NIGHT(0.0f, 0.5f, -0.5f); // sun below the horizon

    bool near(const cglib::vec3<float>& value, const cglib::vec3<float>& expected, float eps = 1.0e-5f) {
        for (int i = 0; i < 3; i++) {
            if (std::abs(value(i) - expected(i)) > eps) {
                return false;
            }
        }
        return true;
    }

    void testAPrelitStyleIsLeftExactlyAlone() {
        // What buildTerrainLighting sends for `colors-prelit`: white ambient at full weight, no sun.
        // Anything but exactly 1 tints every deck in every converted style.
        cglib::vec3<float> lit = SpanDrapeLight::resolve(true, NOON, WHITE, 0.0f, WHITE, 1.0f);
        TEST_CHECK(near(lit, WHITE), "a prelit style's deck drape is left exactly as authored");

        // And it stays 1 with the sun DOWN, which is the case the bug showed up in.
        cglib::vec3<float> night = SpanDrapeLight::resolve(true, NIGHT, WHITE, 0.0f, WHITE, 1.0f);
        TEST_CHECK(near(night, WHITE), "... at every hour, since it carries no sun to lose");
    }

    void testLightingOffLeavesTheDrapeUntouched() {
        // No TERRAIN_LIGHT in backgroundFsh means the surface samples its drape raw. A deck that
        // lit its drape anyway would be the only thing on the map that did.
        cglib::vec3<float> lit = SpanDrapeLight::resolve(false, NOON, cglib::vec3<float>(1.0f, 0.5f, 0.25f), 1.0f, cglib::vec3<float>(0.2f, 0.2f, 0.9f), 0.35f);
        TEST_CHECK(near(lit, WHITE), "with the ground unlit, the deck's drape is unlit too");
    }

    void testTheAmbientIsTheFloorAndTheSunFillsTheRest() {
        // backgroundFsh's normalised Lambert: ambient*y + sun*((1-y)*ndl*x). At ambient 0.25 with a
        // full white sun straight overhead the two must land on 1, not on 1.25 - adding them
        // outright is what blows the ground out to white at a high sun.
        cglib::vec3<float> lit = SpanDrapeLight::resolve(true, NOON, WHITE, 1.0f, WHITE, 0.25f);
        TEST_CHECK(near(lit, WHITE), "ambient floor plus sun headroom reaches 1, never past it");

        // Half the ambient, no sun at all: the deck sits on the floor alone.
        cglib::vec3<float> ambientOnly = SpanDrapeLight::resolve(true, NOON, WHITE, 0.0f, WHITE, 0.5f);
        TEST_CHECK(near(ambientOnly, cglib::vec3<float>(0.5f, 0.5f, 0.5f)), "with no sun the ambient is the whole light");
    }

    void testASunBelowTheHorizonAddsNothing() {
        // N.L is CLAMPED, as the ground clamps it. Unclamped, a sun below the map SUBTRACTED from
        // the ambient and a deck went darker than black at the bottom of the night.
        cglib::vec3<float> lit = SpanDrapeLight::resolve(true, NIGHT, WHITE, 1.0f, WHITE, 0.4f);
        TEST_CHECK(near(lit, cglib::vec3<float>(0.4f, 0.4f, 0.4f)), "a set sun contributes nothing rather than negative light");
    }

    void testOnlyTheSunsHeightReachesAFlatDeck() {
        // A deck is horizontal, so its N.L is the sun's up component - the azimuth cannot matter.
        // Two suns at the same height and opposite bearings must light one identically.
        cglib::vec3<float> east = SpanDrapeLight::resolve(true, cglib::vec3<float>(0.8f, 0.0f, 0.6f), WHITE, 1.0f, WHITE, 0.2f);
        cglib::vec3<float> west = SpanDrapeLight::resolve(true, cglib::vec3<float>(-0.8f, 0.0f, 0.6f), WHITE, 1.0f, WHITE, 0.2f);
        TEST_CHECK(near(east, west), "a flat deck takes the sun's height, not its bearing");
    }

    void testTheSunsOwnColourReachesTheDeck() {
        // A dusk sun is orange, and the deck has to warm with it exactly as the ground does -
        // otherwise a bridge stays neutral in a scene that has gone red.
        cglib::vec3<float> sunColor(1.0f, 0.5f, 0.0f);
        cglib::vec3<float> lit = SpanDrapeLight::resolve(true, NOON, sunColor, 1.0f, WHITE, 0.5f);
        // ambient 0.5 white + sun (1 - 0.5) * 1 * 1 * (1, 0.5, 0)
        TEST_CHECK(near(lit, cglib::vec3<float>(1.0f, 0.75f, 0.5f)), "the sun's colour reaches the deck at the sun's weight");
    }
}

void testSpanDrapeLight() {
    testAPrelitStyleIsLeftExactlyAlone();
    testLightingOffLeavesTheDrapeUntouched();
    testTheAmbientIsTheFloorAndTheSunFillsTheRest();
    testASunBelowTheHorizonAddsNothing();
    testOnlyTheSunsHeightReachesAFlatDeck();
    testTheSunsOwnColourReachesTheDeck();
}
