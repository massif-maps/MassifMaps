/*
 * parseCSSColor, which every colour in every style sheet goes through.
 *
 * It read each pair of hex digits with `istringstream >> std::hex` and checked `bad()`. A non-hex
 * character sets FAILBIT, not badbit, and a failed extraction leaves the target at 0 - so
 * "#gg0000" parsed as black and rendered, instead of being refused as the typo it is.
 *
 * The order is pinned here too: eight digits are #rrggbbaa, and StructCodec::decodeColor now
 * matches it so one spelling means one colour in a style sheet and in the facade.
 */

#include "TestCheck.h"

#include <mapnikvt/CSSColorParser.h>
#include <mapnikvt/ParserUtils.h>
#include <cartocss/Color.h>

#include <sstream>

#include <string>

namespace mvt = massif::mvt;

namespace {
    bool parses(const std::string& text, unsigned int expected) {
        unsigned int value = 0;
        return mvt::parseCSSColor(text, value) && value == expected;
    }

    bool refused(const std::string& text) {
        unsigned int value = 0;
        return !mvt::parseCSSColor(text, value);
    }
}

void testCSSColor() {
    TEST_CHECK(parses("#b8c6d8", 0xffb8c6d8), "six digits are #rrggbb and opaque");
    TEST_CHECK(parses("#0af", 0xff00aaff), "three digits expand pairwise");

    // Half-transparent, so the check turns on the ORDER rather than on a palindrome.
    TEST_CHECK(parses("#b8c6d880", 0x80b8c6d8), "eight digits are #rrggbbaa, alpha LAST");
    TEST_CHECK(parses("#0af8", 0x8800aaff), "four digits are #rgba and expand pairwise");

    TEST_CHECK(parses("cornflowerblue", 0xff6495ed), "a CSS colour name is opaque");
    TEST_CHECK(parses("transparent", 0), "transparent is the one name with no alpha");

    // The bug: every one of these used to parse, as black or as a colour missing a component.
    TEST_CHECK(refused("#gg0000"), "a non-hex digit is refused rather than read as 0");
    TEST_CHECK(refused("#00ff0g"), "a bad digit in the LAST pair is refused too");
    TEST_CHECK(refused("# 0ff00"), "a space is not a hex digit");
    TEST_CHECK(refused("#-10000"), "a sign is not a hex digit");

    TEST_CHECK(refused("#b8c6d"), "five digits are no colour");
    TEST_CHECK(refused("#"), "an empty code is no colour");
    TEST_CHECK(refused("marzipan"), "an unknown name is refused");

    // A config colour (hillshade-shadow-color) reaches CompositeVectorTileLayer as the translator
    // spells it, rgb()/rgba(), never #hex: a hex-only reader drew every such shade black.
    std::stringstream opaque, translucent;
    opaque << massif::css::Color::fromRGBA8(84, 77, 69, 255);
    translucent << massif::css::Color::fromRGBA8(84, 77, 69, 128);
    massif::vt::Color parsed;
    TEST_CHECK(mvt::tryParseColor(opaque.str(), parsed) && parsed.rgba8() == (std::array<std::uint8_t, 4> { 84, 77, 69, 255 }),
               "the translator's rgb() reads back as the same colour");
    // parseColorValue divides it back out: the layer's colours are straight.
    TEST_CHECK(mvt::tryParseColor(translucent.str(), parsed) && parsed.rgba8() == (std::array<std::uint8_t, 4> { 42, 39, 35, 128 }),
               "its rgba() reads back premultiplied");
}
