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

    // A list of names - what a search's layer filter is, and the first non-numeric struct.
    std::vector<std::string> names;
    names.push_back("place");
    names.push_back("mountain_peak");
    std::vector<std::string> decodedNames;
    TEST_CHECK(StructCodec::encode(names) == "[\"place\",\"mountain_peak\"]",
               "a list of names reads as a JSON array");
    TEST_CHECK(StructCodec::decode(StructCodec::encode(names), decodedNames) &&
               decodedNames == names, "and round-trips in order");
    TEST_CHECK(StructCodec::decode("[]", decodedNames) && decodedNames.empty(),
               "an empty list is a list, not a failure");
    decodedNames = names;
    TEST_CHECK(!StructCodec::decode("[\"a\",2]", decodedNames) && decodedNames == names,
               "a non-string element is refused, and the value is left alone");
    TEST_CHECK(!StructCodec::decode("\"place\"", decodedNames), "a bare string is not a list");

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

    // GeoJSON without the binding having to build a writer.
    TEST_CHECK(context->getProperty(handle, "geometryGeoJSON", value) == RESULT_OK &&
               value.stringValue.find("\"Point\"") != std::string::npos &&
               value.stringValue.find("\"coordinates\"") != std::string::npos,
               "geometryGeoJSON serialises the geometry");

    auto empty = std::make_shared<Feature>(std::shared_ptr<Geometry>(), Variant());
    Handle emptyHandle = NULL_HANDLE;
    context->registerObject("feature", "empty", empty, "massif::Feature", emptyHandle);
    TEST_CHECK(context->getProperty(emptyHandle, "geometryGeoJSON", value) == RESULT_OK &&
               value.stringValue.empty(), "a feature with no geometry gives an empty string");
}

/* The clicked position of a MultiPoint feature, which needed a downcast before. */

#include "ui/VectorTileClickInfo.h"
#include "geometry/MultiPointGeometry.h"

void testFeaturePos() {
    auto context = std::make_shared<Context>();
    std::vector<std::shared_ptr<PointGeometry> > points;
    // Deliberately asymmetric: with evenly spaced points the middle one IS the centre, and the
    // test would pass whether or not the index was used.
    points.push_back(std::make_shared<PointGeometry>(MapPos(0, 0)));
    points.push_back(std::make_shared<PointGeometry>(MapPos(20, 20)));
    points.push_back(std::make_shared<PointGeometry>(MapPos(100, 100)));
    auto multi = std::make_shared<MultiPointGeometry>(points);
    auto feature = std::make_shared<VectorTileFeature>(1, MapTile(0, 0, 0, 0), "poi", multi, Variant());

    // Index 1 is the second point, not the centre of all three.
    VectorTileClickInfo clicked(ClickInfo(ClickType::CLICK_TYPE_SINGLE, 0),
                                MapPos(0, 0), MapPos(0, 0), feature, std::shared_ptr<Layer>(), 1);
    TEST_CHECK(clicked.getFeaturePos() == MapPos(20, 20), "a MultiPoint gives the clicked point");
    TEST_CHECK(clicked.getFeaturePos() != multi->getCenterPos(), "not the centre of the set");

    VectorTileClickInfo noIndex(ClickInfo(ClickType::CLICK_TYPE_SINGLE, 0),
                                MapPos(0, 0), MapPos(0, 0), feature, std::shared_ptr<Layer>(), -1);
    TEST_CHECK(noIndex.getFeaturePos() == multi->getCenterPos(),
               "with no index it falls back to the centre");

    VectorTileClickInfo outOfRange(ClickInfo(ClickType::CLICK_TYPE_SINGLE, 0),
                                   MapPos(0, 0), MapPos(0, 0), feature, std::shared_ptr<Layer>(), 99);
    TEST_CHECK(outOfRange.getFeaturePos() == multi->getCenterPos(),
               "an out-of-range index falls back rather than reading past the end");

    auto point = std::make_shared<VectorTileFeature>(2, MapTile(0, 0, 0, 0), "poi",
                                                     std::make_shared<PointGeometry>(MapPos(5, 6)), Variant());
    VectorTileClickInfo single(ClickInfo(ClickType::CLICK_TYPE_SINGLE, 0),
                               MapPos(0, 0), MapPos(0, 0), point, std::shared_ptr<Layer>(), 0);
    TEST_CHECK(single.getFeaturePos() == MapPos(5, 6), "a plain Point is unaffected by the index");
}
