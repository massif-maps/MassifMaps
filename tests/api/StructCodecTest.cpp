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
