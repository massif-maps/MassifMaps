/*
 * Tests for the two facade gaps the maneuver-arrows example hit (#289): the builder reached through
 * create + call, and a bare GeoJSON Feature accepted where a FeatureCollection was the only form.
 *
 * NOT covered here: the setLayerGeoJSON facade thunk itself, which lives in MethodImpls.cpp and
 * needs the hillshade layer; the data source method it forwards to is what is checked.
 */

#include "api/Context.h"
#include "api/GeometryMethods.h"
#include "api/Spec.h"
#include "api/SpecBuilders.h"
#include "core/MapTile.h"
#include "core/BinaryData.h"
#include "core/Variant.h"
#include "datasources/GeoJSONVectorTileDataSource.h"
#include "datasources/components/TileData.h"

#include <cmath>
#include <memory>
#include <string>

using namespace massif;
using namespace massif::api;

#include "TestCheck.h"

namespace {

    Result generatedGeometry(Context& context, const Variant& spec, ObjectRef& object,
                             std::set<std::string>& consumed) {
        return buildFromConstructor(context, "geometry", spec, object, consumed);
    }

    // An L at 45N: 100 m east, then north. Before and after differ, so a swap cannot pass.
    const char* ROUTE = "[[0,45],[0.0012700,45],[0.0012700,45.0009]]";

    Variant arrowLine(const PropertyValue& value) {
        Variant collection = Variant::FromString(value.stringValue);
        return collection.getObjectElement("features").getArrayElement(0)
            .getObjectElement("geometry");
    }

    double coord(const Variant& line, int point, int axis) {
        const Variant& coords = line.getObjectElement("coordinates");
        int index = point < 0 ? coords.getArraySize() + point : point;
        return coords.getArrayElement(index).getArrayElement(axis).getDouble();
    }

    std::size_t tileSize(GeoJSONVectorTileDataSource& source) {
        std::shared_ptr<TileData> tile = source.loadTile(MapTile(0, 0, 0, 0));
        return tile && tile->getData() ? tile->getData()->size() : 0;
    }

}

void testManeuverArrowFacade() {
    registerGeometryMethods();
    Spec::registerFactory("geometry", &generatedGeometry);
    auto context = std::make_shared<Context>();

    Handle builder = NULL_HANDLE;
    TEST_CHECK(Spec::create(*context, "geometry", "arrow",
                            "{\"type\":\"maneuver-arrow\",\"lengthBefore\":10,\"lengthAfter\":20}",
                            builder) == RESULT_OK, "a maneuver arrow builder is created from a spec");
    PropertyValue value;
    TEST_CHECK(context->getProperty(builder, "lengthAfter", value) == RESULT_OK &&
               value.asDouble() == 20, "with the lengths its spec set");

    TEST_CHECK(context->call(builder, "buildArrowAtIndex", std::string("[") + ROUTE + ",1]", value) ==
               RESULT_OK && value.type == PT_VARIANT, "buildArrowAtIndex returns JSON, not a handle");
    Variant line = arrowLine(value);
    TEST_CHECK(line.getObjectElement("type").getString() == "LineString",
               "the arrow is one LineString in a FeatureCollection");
    double metresPerDegree = 111319.49;
    double before = (0.00127 - coord(line, 0, 0)) * metresPerDegree * std::cos(45 * M_PI / 180);
    double after = (coord(line, -1, 1) - 45) * metresPerDegree;
    TEST_CHECK(std::abs(before - 10) < 0.5, "it starts lengthBefore metres back along the route");
    TEST_CHECK(std::abs(after - 20) < 0.5, "and ends lengthAfter metres on, after the turn");

    TEST_CHECK(context->call(builder, "buildArrow", std::string("[") + ROUTE + ",[0.00127,45]]", value) ==
               RESULT_OK && std::abs(coord(arrowLine(value), -1, 1) - coord(line, -1, 1)) < 1e-9,
               "buildArrow at the turn's position cuts the same arrow");

    TEST_CHECK(context->call(builder, "buildArrowAtIndex", "[[[0,45]],0]", value) == RESULT_OK &&
               Variant::FromString(value.stringValue).getObjectElement("features")
               .getArraySize() == 0, "a route too short for an arrow is an empty collection, not an error");
    TEST_CHECK(context->call(builder, "buildArrowAtIndex", "[[[0,45]]]", value) == RESULT_BAD_SPEC,
               "a missing index is refused");
    TEST_CHECK(context->call(builder, "buildArrow", "[5,[0,45]]", value) == RESULT_BAD_SPEC,
               "and points that are not an array are refused");
}

void testGeoJSONBareFeature() {
    GeoJSONVectorTileDataSource source(0, 14);
    int layer = source.createLayer("maneuver");
    std::size_t empty = tileSize(source);

    const std::string feature =
        "{\"type\":\"Feature\",\"properties\":{\"head\":\"wide\"},"
        "\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[0,45],[10,46]]}}";
    bool accepted = true;
    try {
        source.setLayerGeoJSONString(layer, feature);
    } catch (const std::exception&) {
        accepted = false;
    }
    TEST_CHECK(accepted, "setLayerGeoJSON takes a bare Feature");
    TEST_CHECK(tileSize(source) > empty, "and its geometry reaches the tile");

    source.setLayerGeoJSON(layer, Variant::FromString("{\"type\":\"FeatureCollection\",\"features\":[]}"));
    accepted = true;
    try {
        source.setLayerGeoJSON(layer, Variant::FromString(feature));
    } catch (const std::exception&) {
        accepted = false;
    }
    TEST_CHECK(accepted && tileSize(source) > empty, "so does the Variant overload");

    bool refused = false;
    try {
        source.setLayerGeoJSONString(layer, "{\"type\":\"Point\",\"coordinates\":[0,45]}");
    } catch (const std::exception&) {
        refused = true;
    }
    TEST_CHECK(refused, "a bare geometry is still refused");
}
