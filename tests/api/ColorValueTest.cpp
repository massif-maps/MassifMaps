/*
 * The one decoder every facade colour goes through: a property write, a spec key, a spec
 * constructor argument and a struct field all have to read "#rrggbbaa" the same way.
 *
 * Two bugs this pins. A COLOR property took an ARGB NUMBER only, so "#b8c6d8ff" went through
 * asLong() and landed as 0 - a fully transparent colour, which on FogOptions.color renders exactly
 * like no fog at all and cost a debugging cycle to find. And the eight-digit form the facade did
 * take was "#aarrggbb", the REVERSE of the order mvt::parseCSSColor gives a style sheet, so one
 * spelling meant two colours inside one SDK.
 *
 * NOT covered here: the generated CONSTRUCTOR argument reader at its call site. SolidLayer is the
 * only class whose !spec takes a Color, and a Layer pulls in the renderer - past what the host
 * link carries. colorAt() below is the function that call site emits, tested directly.
 */

#include "api/Context.h"
#include "api/SpecBuilders.h"
#include "api/StructCodec.h"
#include "components/FogOptions.h"
#include "core/Variant.h"
#include "graphics/Color.h"

#include <map>
#include <memory>
#include <set>
#include <string>

using namespace massif;
using namespace massif::api;

#include "TestCheck.h"

namespace {

    const int SLATE = static_cast<int>(0xffb8c6d8);

    unsigned int argb(const Color& color) {
        return static_cast<unsigned int>(color.getARGB());
    }

}

/** The generated property setter - what an app reaches through Context::setProperty. */
void testColorProperty() {
    auto context = std::make_shared<Context>();
    auto fog = std::make_shared<FogOptions>();
    Handle handle = NULL_HANDLE;
    TEST_CHECK(context->registerObject("options", "fog", fog, "massif::FogOptions", handle) == RESULT_OK,
               "the fog options register");

    TEST_CHECK(context->setProperty(handle, "color", PropertyValue::ofLong(0xffb8c6d8)) == RESULT_OK &&
               argb(fog->getColor()) == 0xffb8c6d8u,
               "a colour property takes an ARGB number");

    fog->setColor(Color(0, 0, 0, 0));
    TEST_CHECK(context->setProperty(handle, "color", PropertyValue::ofString("#b8c6d8ff")) == RESULT_OK &&
               argb(fog->getColor()) == 0xffb8c6d8u,
               "the same colour written as \"#rrggbbaa\" lands on the same value");

    // The two shorthands a style sheet uses. Six digits are opaque, three expand pairwise.
    fog->setColor(Color(0, 0, 0, 0));
    context->setProperty(handle, "color", PropertyValue::ofString("#b8c6d8"));
    TEST_CHECK(argb(fog->getColor()) == 0xffb8c6d8u, "\"#rrggbb\" is opaque, not alpha 0");
    context->setProperty(handle, "color", PropertyValue::ofString("#0af"));
    TEST_CHECK(argb(fog->getColor()) == 0xff00aaffu, "\"#rgb\" expands each digit pairwise");

    // Half-transparent, so this check turns on the ORDER: read the old #aarrggbb way, "#b8c6d880"
    // would be 0xd880b8c6 - the disagreement with mvt::parseCSSColor that used to exist.
    context->setProperty(handle, "color", PropertyValue::ofString("#b8c6d880"));
    TEST_CHECK(argb(fog->getColor()) == 0x80b8c6d8u, "eight digits are #rrggbbaa, alpha LAST");
    context->setProperty(handle, "color", PropertyValue::ofString("#0af8"));
    TEST_CHECK(argb(fog->getColor()) == 0x8800aaffu, "\"#rgba\" expands each digit pairwise");

    // A string-only caller - the C ABI, a URL query - used to reach asLong(), and still must.
    context->setProperty(handle, "color", PropertyValue::ofString("4290299608"));
    TEST_CHECK(argb(fog->getColor()) == 0xffb8c6d8u, "an ARGB number spelled as text still works");

    // The whole point: garbage must not become a transparent colour, which is indistinguishable
    // from the property never having been set.
    fog->setColor(Color(SLATE));
    context->setProperty(handle, "color", PropertyValue::ofString("cornflower"));
    TEST_CHECK(argb(fog->getColor()) == 0xffb8c6d8u,
               "an unparseable colour leaves the property alone rather than writing 0");
    context->setProperty(handle, "color", PropertyValue::ofString("#b8zzd8ff"));
    TEST_CHECK(argb(fog->getColor()) == 0xffb8c6d8u, "a hex string with a bad digit is refused");
    context->setProperty(handle, "color", PropertyValue::ofString("#b8c6d8f"));
    TEST_CHECK(argb(fog->getColor()) == 0xffb8c6d8u, "a hex string of the wrong length is refused");

    // Reading back is the ARGB number, whichever spelling wrote it.
    PropertyValue read;
    TEST_CHECK(context->getProperty(handle, "color", read) == RESULT_OK &&
               read.type == PT_COLOR && read.intValue == 0xffb8c6d8,
               "a colour reads back as its ARGB number");
}

/** The spec paths: a key applied as a property, and the constructor argument reader. */
void testColorSpec() {
    auto context = std::make_shared<Context>();
    auto fog = std::make_shared<FogOptions>();
    ObjectRef object;
    object.obj = fog;
    object.cppClass = "massif::FogOptions";

    std::map<std::string, Variant> spec;
    spec["color"] = Variant(std::string("#b8c6d8ff"));
    std::set<std::string> consumed;
    applySpecProperties(*context, object, Variant(spec), consumed);
    TEST_CHECK(argb(fog->getColor()) == 0xffb8c6d8u, "a spec key writes a colour written as a string");

    std::map<std::string, Variant> numeric;
    numeric["color"] = Variant(static_cast<long long>(0xffb8c6d8));
    auto other = std::make_shared<FogOptions>();
    ObjectRef otherObject;
    otherObject.obj = other;
    otherObject.cppClass = "massif::FogOptions";
    std::set<std::string> numericConsumed;
    applySpecProperties(*context, otherObject, Variant(numeric), numericConsumed);
    TEST_CHECK(argb(other->getColor()) == argb(fog->getColor()),
               "the two spellings of one colour agree through a spec");

    // colorAt is what the generated constructor builder emits for a Color argument.
    TEST_CHECK(argb(colorAt(Variant(spec), "color", 0)) == 0xffb8c6d8u,
               "a spec constructor argument takes \"#rrggbbaa\"");
    TEST_CHECK(argb(colorAt(Variant(numeric), "color", 0)) == 0xffb8c6d8u,
               "a spec constructor argument takes an ARGB number");

    std::map<std::string, Variant> bad;
    bad["color"] = Variant(std::string("cornflower"));
    TEST_CHECK(argb(colorAt(Variant(bad), "color", SLATE)) == 0xffb8c6d8u,
               "an unparseable spec colour falls back rather than reading as 0");
    TEST_CHECK(argb(colorAt(Variant(std::map<std::string, Variant>()), "color", SLATE)) == 0xffb8c6d8u,
               "an absent spec colour is the fallback");
}

/** The decoder itself, on the spellings a struct field takes - lighting.sunColor and friends. */
void testColorDecode() {
    Color color(0);
    TEST_CHECK(StructCodec::decodeColor(Variant(std::string("#b8c6d8ff")), color) &&
               argb(color) == 0xffb8c6d8u, "decodeColor reads \"#rrggbbaa\"");
    TEST_CHECK(StructCodec::decodeColor(Variant(static_cast<long long>(0xffb8c6d8)), color) &&
               argb(color) == 0xffb8c6d8u, "decodeColor reads an ARGB number");
    TEST_CHECK(StructCodec::decodeColor(PropertyValue::ofString("#b8c6d8ff"), color) &&
               argb(color) == 0xffb8c6d8u, "decodeColor reads a property value's string");

    // A refusal must not have written anything on its way out.
    Color kept(SLATE);
    TEST_CHECK(!StructCodec::decodeColor(Variant(std::string("#ff")), kept) &&
               argb(kept) == 0xffb8c6d8u, "a refused colour leaves its output untouched");
    TEST_CHECK(!StructCodec::decodeColor(Variant(std::string("")), kept), "an empty string is refused");
    TEST_CHECK(!StructCodec::decodeColor(Variant(std::string("12ab")), kept),
               "a number with trailing letters is refused, not read up to the first one");
    TEST_CHECK(!StructCodec::decodeColor(Variant(), kept), "a null variant is refused");
}
