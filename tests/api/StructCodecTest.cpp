/*
 * Tests for the JSON encoding of the SDK's small by-value structs.
 */

#include "api/StructCodec.h"
#include "components/LightStop.h"
#include "core/MapTile.h"
#include "ui/ClickInfo.h"

#include <map>

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

    // A screen rectangle, which is what fitBounds takes. Asymmetric on purpose: a codec that
    // swapped min and max, or x and y, would still pass a square.
    ScreenBounds rect(ScreenPos(4, 7), ScreenPos(320, 480));
    ScreenBounds decodedRect;
    TEST_CHECK(StructCodec::encode(rect) == "[[4,7],[320,480]]",
               "a ScreenBounds reads as a pair of screen points");
    TEST_CHECK(StructCodec::decode(StructCodec::encode(rect), decodedRect) &&
               decodedRect.getMin() == rect.getMin() &&
               decodedRect.getMax() == rect.getMax(), "and round-trips");
    TEST_CHECK(!StructCodec::decode("[[4,7]]", decodedRect), "a rectangle needs both corners");

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
    // The same on the geometry itself, so a shape with no feature around it serialises too - which
    // is what an app writing a track or a route back out has.
    TEST_CHECK(context->getProperty(handle, "geometry.geoJSON", value) == RESULT_OK &&
               value.stringValue.find("\"Point\"") != std::string::npos,
               "and a geometry serialises on its own");

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

/*
 * The struct types added after the first round, each because something real was unreachable
 * without it. Unit level: the properties that carry them live on classes too heavy for this link.
 */
void testMoreStructs() {
    // A tile - what a raster click and a vector tile feature carry.
    MapTile tile(8467, 5852, 14, 0);
    MapTile decodedTile;
    TEST_CHECK(StructCodec::encode(tile) == "[8467,5852,14]", "a tile reads as [x,y,zoom]");
    TEST_CHECK(StructCodec::decode(StructCodec::encode(tile), decodedTile) &&
               decodedTile.getX() == 8467 && decodedTile.getY() == 5852 &&
               decodedTile.getZoom() == 14, "and round-trips");
    // frameNr is not part of the spelling: a tile named from outside the map belongs to no frame.
    TEST_CHECK(decodedTile.getFrameNr() == 0, "with no frame number");
    TEST_CHECK(!StructCodec::decode("[1,2]", decodedTile), "a two-element array is not a tile");

    // A string map - HTTP headers, and a layer's metadata.
    std::map<std::string, std::string> headers;
    headers["X-Client-Id"] = "MassifMaps";
    headers["Accept"] = "*/*";
    std::map<std::string, std::string> decodedHeaders;
    TEST_CHECK(StructCodec::decode(StructCodec::encode(headers), decodedHeaders) &&
               decodedHeaders == headers, "a string map round-trips");
    TEST_CHECK(StructCodec::decode("{\"n\":7}", decodedHeaders) && decodedHeaders["n"] == "7",
               "a number is spelled out rather than refused");
    decodedHeaders = headers;
    TEST_CHECK(!StructCodec::decode("[1,2]", decodedHeaders) && decodedHeaders == headers,
               "an array is not a map, and the refusal leaves the value alone");

    std::map<std::string, Variant> meta;
    meta["count"] = Variant(static_cast<long long>(3));
    meta["name"] = Variant(std::string("x"));
    std::map<std::string, Variant> decodedMeta;
    TEST_CHECK(StructCodec::decode(StructCodec::encode(meta), decodedMeta) &&
               decodedMeta["count"].getLong() == 3 && decodedMeta["name"].getString() == "x",
               "a variant map keeps each value's type");

    // A click, as an object - its two fields mean different things and neither order is natural.
    ClickInfo click(ClickType::CLICK_TYPE_LONG, 0.75f);
    ClickInfo decodedClick(ClickType::CLICK_TYPE_SINGLE, 0);
    TEST_CHECK(StructCodec::encode(click).find("\"clickType\":") != std::string::npos,
               "a click reads as a named object, not a tuple");
    TEST_CHECK(StructCodec::decode(StructCodec::encode(click), decodedClick) &&
               decodedClick.getClickType() == ClickType::CLICK_TYPE_LONG &&
               decodedClick.getDuration() == 0.75f, "and round-trips");
    TEST_CHECK(!StructCodec::decode("{\"duration\":1}", decodedClick),
               "without a clickType it is not a click");

    // A list of positions, which the routing request spec needs. Deliberately NOT a property type.
    std::vector<MapPos> path;
    path.push_back(MapPos(1, 2));
    path.push_back(MapPos(3, 4));
    std::vector<MapPos> decodedPath;
    TEST_CHECK(StructCodec::decode(StructCodec::encode(path), decodedPath) &&
               decodedPath.size() == 2 && decodedPath[1] == MapPos(3, 4, 0),
               "a list of positions round-trips");
    TEST_CHECK(!StructCodec::decode("[[1,2],[3]]", decodedPath), "one bad element fails the list");

    // Rings, which a polygon spec needs: an outline and its holes.
    std::vector<std::vector<MapPos> > rings;
    rings.push_back(path);
    std::vector<MapPos> hole;
    hole.push_back(MapPos(5, 6));
    rings.push_back(hole);
    std::vector<std::vector<MapPos> > decodedRings;
    TEST_CHECK(StructCodec::encode(rings) == "[[[1,2,0],[3,4,0]],[[5,6,0]]]",
               "rings nest one level deeper than a path");
    TEST_CHECK(StructCodec::decode(StructCodec::encode(rings), decodedRings) &&
               decodedRings.size() == 2 && decodedRings[1].size() == 1 &&
               decodedRings[1][0] == MapPos(5, 6, 0), "and round-trip");
    TEST_CHECK(!StructCodec::decode("[[1,2],[3,4]]", decodedRings),
               "a flat path is not a list of rings");

    // A day-cycle light curve: the "formula" an app replaces to change the whole map's palette at
    // every hour. An OBJECT per stop, because five fields of three kinds have no natural order.
    std::vector<LightStop> curve;
    curve.push_back(LightStop(-9.0f, Color(0, 20, 56, 255), 0.5f, Color(63, 68, 85, 255), 0.5f));
    curve.push_back(LightStop(38.0f, Color(255, 255, 255, 255), 0.8f, Color(255, 255, 255, 255), 0.2f));
    std::vector<LightStop> decodedCurve;
    TEST_CHECK(StructCodec::decode(StructCodec::encode(curve), decodedCurve) &&
               decodedCurve.size() == 2 && decodedCurve[0] == curve[0] && decodedCurve[1] == curve[1],
               "a light curve round-trips");
    TEST_CHECK(StructCodec::encode(curve).find("\"ambientColor\":\"#ff001438\"") != std::string::npos,
               "and a stop's colours are written as #aarrggbb, which is what a style spells");
    // Lenient on the way IN, so an app may write any of the spellings it already uses elsewhere.
    LightStop stop;
    TEST_CHECK(StructCodec::decode("{\"sunAltitude\":10,\"ambientColor\":\"#abc\"}", stop) &&
               stop.getAmbientColor() == Color(0xffaabbcc),
               "a short hex colour is accepted");
    TEST_CHECK(StructCodec::decode("{\"sunAltitude\":10,\"sunColor\":-1}", stop) &&
               stop.getSunColor() == Color(255, 255, 255, 255),
               "and so is the plain ARGB number every other colour property carries");
    TEST_CHECK(!StructCodec::decode("{\"ambientIntensity\":1}", stop),
               "a stop with no sun height is meaningless and is refused");
    TEST_CHECK(!StructCodec::decode("{\"sunAltitude\":10,\"sunColor\":\"blue\"}", stop),
               "and a colour name is not a colour here");
    // Clearing the curve - going back to the built-in one - is what an app does to UNDO a custom
    // formula, and it must not read as "the value was rubbish, keep the old one".
    decodedCurve = curve;
    TEST_CHECK(StructCodec::decode("[]", decodedCurve) && decodedCurve.empty(),
               "an empty curve is an empty curve, not a failure");
    decodedCurve = curve;
    TEST_CHECK(StructCodec::decode("", decodedCurve) && decodedCurve.empty(),
               "and so is an empty string, which is what clearing a property spells");
}
