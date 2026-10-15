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
#include "geometry/Feature.h"
#include "geometry/FeatureCollection.h"
#include "geometry/LineGeometry.h"
#include "geometry/ManeuverArrowBuilder.h"

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

namespace {
    // Metres east / north of (0, 0) on the equator, where a degree is the same length both ways.
    MapPos metres(double east, double north) {
        const double metresPerDegree = 111319.49;
        return MapPos(east / metresPerDegree, north / metresPerDegree);
    }

    // The arrow's vertices, in metres, for a route turning at turnIndex (the end of its first segment by default).
    std::vector<MapPos> arrowVertices(const std::vector<MapPos>& route, float lengthBefore, float lengthAfter, int turnIndex = 1) {
        ManeuverArrowBuilder builder;
        builder.setLengthBefore(lengthBefore);
        builder.setLengthAfter(lengthAfter);
        std::shared_ptr<FeatureCollection> arrow = builder.buildArrowAtIndex(std::shared_ptr<Projection>(), route, turnIndex);
        std::vector<MapPos> vertices;
        if (arrow && arrow->getFeatureCount() == 1) {
            if (auto line = std::dynamic_pointer_cast<LineGeometry>(arrow->getFeature(0)->getGeometry())) {
                for (const MapPos& pos : line->getPoses()) {
                    vertices.push_back(MapPos(pos.getX() * 111319.49, pos.getY() * 111319.49));
                }
            }
        }
        return vertices;
    }
}

// The head hangs on the arrow's last vertex and points along its last segment, so a cut that lands a few
// metres past a route vertex left the head sitting on the join (docs/features/maneuver-arrows.md).
void testManeuverArrowEnd() {
    // North after the turn, then a 11 degree kink at (100, 40), and the cut lands 0.3 m past the next vertex.
    std::vector<MapPos> kinked = { metres(0, 0), metres(100, 0), metres(100, 40), metres(102, 50), metres(105, 60) };
    std::vector<MapPos> arrow = arrowVertices(kinked, 10, 50.3f);
    TEST_CHECK(arrow.size() == 3, "a kinked end whose last segment is a few centimetres is one straight run after the turn");
    TEST_CHECK(arrow.size() == 3 && std::abs(arrow[1].getX() - 100) < 1e-6 && std::abs(arrow[1].getY()) < 1e-6, "the turn itself is kept");
    TEST_CHECK(arrow.size() == 3 && std::abs(arrow[2].getX() - 102.029) < 0.01 && std::abs(arrow[2].getY() - 50.098) < 0.01, "and it still ends where lengthAfter puts it");

    // The tail the same way round: its round cap and join step out of the casing when the first segment is tiny.
    std::vector<MapPos> kinkedTail = { metres(105, -60), metres(102, -50), metres(100, -40), metres(100, 0), metres(140, 0) };
    std::vector<MapPos> tail = arrowVertices(kinkedTail, 50.3f, 10, 3);
    TEST_CHECK(tail.size() == 3, "a kinked start whose first segment is a few centimetres is one straight run into the turn");
    TEST_CHECK(tail.size() == 3 && std::abs(tail[0].getX() - 102.029) < 0.01 && std::abs(tail[0].getY() + 50.098) < 0.01, "and it still starts where lengthBefore puts it");

    // The same cut with a real corner in it keeps it: that is the route, not noise.
    std::vector<MapPos> corner = { metres(0, 0), metres(100, 0), metres(100, 40), metres(140, 40) };
    TEST_CHECK(arrowVertices(corner, 10, 51).size() == 4, "a corner in the last stretch stays");

    // A last segment that is already a good part of the walk is left alone.
    std::vector<MapPos> longEnd = { metres(0, 0), metres(100, 0), metres(100, 10), metres(103, 60) };
    TEST_CHECK(arrowVertices(longEnd, 10, 50).size() == 4, "a long last segment is not touched");

    // Nothing to iron out before the first segment past the turn.
    std::vector<MapPos> shortWalk = { metres(0, 0), metres(100, 0), metres(100, 0.5), metres(101, 1) };
    TEST_CHECK(arrowVertices(shortWalk, 10, 1.5f).size() >= 3, "a walk shorter than the run leaves the anchor and the end");
}
