/*
 * Tests for the JSON encoding of the SDK's small by-value structs.
 */

#include "api/StructCodec.h"

#include "TestCheck.h"

using namespace massif;
using namespace massif::api;

void testStructCodec() {
    // A position is [x, y, z], and round-trips exactly.
    MapPos pos(1.5, -2.25, 30);
    MapPos decodedPos;
    TEST_CHECK(StructCodec::decode(StructCodec::encode(pos), decodedPos) && decodedPos == pos,
               "a MapPos round-trips");

    // z is optional, because an app writing a spec should not have to say 0.
    MapPos flat;
    TEST_CHECK(StructCodec::decode("[3,4]", flat) && flat.getX() == 3 && flat.getY() == 4 &&
               flat.getZ() == 0, "a two-element position leaves z at 0");

    MapVec vec(1, 2, 3);
    MapVec decodedVec;
    TEST_CHECK(StructCodec::decode(StructCodec::encode(vec), decodedVec) && decodedVec == vec,
               "a MapVec round-trips");

    ScreenPos screen(10.5f, 20.5f);
    ScreenPos decodedScreen;
    TEST_CHECK(StructCodec::decode(StructCodec::encode(screen), decodedScreen) &&
               decodedScreen == screen, "a ScreenPos round-trips");

    MapRange range(2.5f, 18.5f);
    MapRange decodedRange;
    TEST_CHECK(StructCodec::decode(StructCodec::encode(range), decodedRange) &&
               decodedRange == range, "a MapRange round-trips");
    TEST_CHECK(StructCodec::encode(range) == "[2.5,18.5]", "and reads as [min,max]");

    MapBounds bounds(MapPos(0, 0), MapPos(10, 20));
    MapBounds decodedBounds;
    TEST_CHECK(StructCodec::decode(StructCodec::encode(bounds), decodedBounds) &&
               decodedBounds == bounds, "a MapBounds round-trips as a pair of positions");

    Variant variant = Variant::FromString("{\"a\":[1,2],\"b\":\"x\"}");
    Variant decodedVariant;
    TEST_CHECK(StructCodec::decode(StructCodec::encode(variant), decodedVariant) &&
               decodedVariant == variant, "a Variant round-trips");

    // A wrong shape fails rather than being guessed at, so a malformed spec cannot quietly write
    // a default over a real value.
    MapPos unchanged(9, 9, 9);
    TEST_CHECK(!StructCodec::decode("not json", unchanged), "malformed JSON is refused");
    TEST_CHECK(!StructCodec::decode("[1]", unchanged), "too few elements are refused");
    TEST_CHECK(!StructCodec::decode("[1,2,3,4]", unchanged), "too many elements are refused");
    TEST_CHECK(!StructCodec::decode("[\"a\",\"b\"]", unchanged), "non-numbers are refused");
    TEST_CHECK(!StructCodec::decode("{\"x\":1}", unchanged), "an object is not a position");
    TEST_CHECK(unchanged == MapPos(9, 9, 9), "and none of them touched the value");

    MapRange unchangedRange(1, 2);
    TEST_CHECK(!StructCodec::decode("[1,2,3]", unchangedRange), "a range takes exactly two");
    MapBounds unchangedBounds;
    TEST_CHECK(!StructCodec::decode("[[1,2]]", unchangedBounds), "bounds take exactly two positions");
}

/* Path walking that continues INSIDE a Variant, so one key can be read without the whole bag. */

#include "api/Context.h"
#include "geometry/Feature.h"
#include "geometry/PointGeometry.h"

void testVariantPaths() {
    auto context = std::make_shared<Context>();
    Variant properties = Variant::FromString(
        "{\"name\":\"Aiguille\",\"ele\":3842,\"open\":true,\"ratio\":0.5,"
        "\"tags\":[\"peak\",\"alpine\"],\"nested\":{\"a\":{\"b\":7}}}");
    auto feature = std::make_shared<Feature>(std::make_shared<PointGeometry>(MapPos(1, 2)), properties);

    Handle handle = NULL_HANDLE;
    context->registerObject("feature", "f", feature, "massif::Feature", handle);

    PropertyValue value;
    TEST_CHECK(context->getProperty(handle, "properties", value) == RESULT_OK &&
               value.type == PT_VARIANT, "the whole bag reads as a Variant");

    TEST_CHECK(context->getProperty(handle, "properties.name", value) == RESULT_OK &&
               value.stringValue == "Aiguille", "one string key");
    TEST_CHECK(context->getProperty(handle, "properties.ele", value) == RESULT_OK &&
               value.asLong() == 3842, "one integer key");
    TEST_CHECK(context->getProperty(handle, "properties.open", value) == RESULT_OK &&
               value.asBool(), "one boolean key");
    TEST_CHECK(context->getProperty(handle, "properties.ratio", value) == RESULT_OK &&
               value.asDouble() == 0.5, "one double key");
    TEST_CHECK(context->getProperty(handle, "properties.tags.1", value) == RESULT_OK &&
               value.stringValue == "alpine", "a numeric segment indexes an array");
    TEST_CHECK(context->getProperty(handle, "properties.nested.a.b", value) == RESULT_OK &&
               value.asLong() == 7, "a deep key");
    TEST_CHECK(context->getProperty(handle, "properties.nested.a", value) == RESULT_OK &&
               value.type == PT_VARIANT, "a subtree reads as JSON");

    TEST_CHECK(context->getProperty(handle, "properties.nope", value) == RESULT_UNKNOWN_PROPERTY,
               "a missing key is reported");
    TEST_CHECK(context->getProperty(handle, "properties.tags.9", value) == RESULT_UNKNOWN_PROPERTY,
               "an out-of-range index is reported");
    TEST_CHECK(context->getProperty(handle, "properties.tags.x", value) == RESULT_UNKNOWN_PROPERTY,
               "a non-numeric index into an array is reported");
    TEST_CHECK(context->getProperty(handle, "properties.name.more", value) == RESULT_UNKNOWN_PROPERTY,
               "walking past a leaf is reported");

    // Traversal through an object property still works alongside it.
    TEST_CHECK(context->getProperty(handle, "geometry.type", value) == RESULT_OK &&
               value.asLong() == GeometryType::GEOMETRY_TYPE_POINT, "geometry.type resolves");
}
