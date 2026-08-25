/*
 * Tests for the two additions that make a path read like a property everywhere: BAG properties,
 * whose last segment is a key, and ALIASES, a second spelling of one segment.
 *
 * Plus the write side of the projection default, which a read has always been able to name.
 */

#include "api/Context.h"
#include "api/MassifApiC.h"
#include "api/PropertyTable.h"
#include "api/StructCodec.h"
#include "components/FogOptions.h"
#include "geometry/Feature.h"
#include "geometry/VectorTileFeature.h"
#include "geometry/PointGeometry.h"
#include "geocoding/GeocodingRequest.h"
#include "projections/EPSG3857.h"
#include "routing/RoutingRequest.h"

#include <memory>
#include <string>
#include <vector>

#include "TestCheck.h"

using namespace massif;
using namespace massif::api;

extern int failures;

namespace {

    std::shared_ptr<RoutingRequest> makeRequest() {
        std::vector<MapPos> points;
        points.push_back(MapPos(0, 0));
        points.push_back(MapPos(1, 1));
        return std::make_shared<RoutingRequest>(std::make_shared<EPSG3857>(), points);
    }

    bool near(double value, double expected, double tolerance) {
        return value > expected - tolerance && value < expected + tolerance;
    }

}

/*
 * A bag: setStyleParameter and setCustomParameter were reachable only as METHODS, which is the one
 * place the facade stopped looking like a property surface.
 */
void testBagProperties() {
    auto context = std::make_shared<Context>();
    auto request = makeRequest();
    Handle handle = NULL_HANDLE;
    context->registerObject("routing", "r", request, "massif::RoutingRequest", handle);

    const PropertyEntry* params = findProperty(findClass("massif::RoutingRequest"), "params");
    TEST_CHECK(params && params->indexed, "the bag property is in the table");
    TEST_CHECK(params && !params->getter && !params->setter,
               "and has no whole-value accessor - its thunks take a key");

    TEST_CHECK(context->setProperty(handle, "params.costing", PropertyValue::ofString("auto")) ==
               RESULT_OK, "one entry writes");
    TEST_CHECK(request->getCustomParameter("costing").getString() == "auto",
               "and reached the SDK's own setter");

    PropertyValue value;
    TEST_CHECK(context->getProperty(handle, "params.costing", value) == RESULT_OK &&
               value.asString() == "auto", "and reads back");

    // The entry keeps its type: a Variant bag is not a string map, and a binding that read every
    // value back as text would have to guess which ones were numbers.
    TEST_CHECK(context->setProperty(handle, "params.alternates", PropertyValue::ofLong(3)) ==
               RESULT_OK, "a number writes");
    TEST_CHECK(context->getProperty(handle, "params.alternates", value) == RESULT_OK &&
               value.type == PT_INT && value.asLong() == 3, "and reads back as a number");

    TEST_CHECK(context->getProperty(handle, "params.nothing", value) == RESULT_UNKNOWN_PROPERTY,
               "a key the bag does not hold is UNKNOWN_PROPERTY, not an empty value");

    // A dotted key is the WHOLE rest of the path: valhalla's parameters are nested.
    TEST_CHECK(context->setProperty(handle, "params.costing_options.auto.top_speed",
                                    PropertyValue::ofLong(90)) == RESULT_OK,
               "a dotted key stays one key");
    TEST_CHECK(context->getProperty(handle, "params.costing_options.auto.top_speed", value) ==
               RESULT_OK && value.asLong() == 90, "and reads back through the same path");
}

/* The whole bag at once - one crossing instead of one call per key. */
void testBagBatch() {
    auto context = std::make_shared<Context>();
    auto request = makeRequest();
    Handle handle = NULL_HANDLE;
    context->registerObject("routing", "r", request, "massif::RoutingRequest", handle);

    PropertyValue json = PropertyValue::ofString("{\"costing\":\"bicycle\",\"alternates\":2}");
    TEST_CHECK(context->setProperty(handle, "params", json) == RESULT_OK, "a JSON object writes");
    TEST_CHECK(request->getCustomParameter("costing").getString() == "bicycle" &&
               request->getCustomParameter("alternates").getLong() == 2,
               "every key applied, each keeping its own type");

    TEST_CHECK(context->setProperty(handle, "params", PropertyValue::ofString("nonsense")) ==
               RESULT_BAD_SPEC, "a value that is not JSON is refused");
    TEST_CHECK(context->setProperty(handle, "params", PropertyValue::ofString("[1,2]")) ==
               RESULT_BAD_SPEC, "and so is an array - a bag is written from an object");
    TEST_CHECK(request->getCustomParameter("costing").getString() == "bicycle",
               "a refused write leaves what was there");
}

/* Several properties in ONE crossing, which is what a binding's apply({...}) is made of. */
void testSetAll() {
    auto context = std::make_shared<Context>();
    auto options = std::make_shared<FogOptions>();
    Handle handle = NULL_HANDLE;
    context->registerObject("options", "fog", options, "massif::FogOptions", handle);

    TEST_CHECK(context->setProperties(
                   handle, "{\"rangeStart\":2,\"rangeEnd\":8,\"enabled\":false}") == RESULT_OK,
               "a JSON object of paths writes");
    TEST_CHECK(near(options->getRangeStart(), 2, 1e-6) && near(options->getRangeEnd(), 8, 1e-6) &&
               !options->isEnabled(), "every key reached its own setter");

    // Reported, but not at the cost of the keys that were fine: an app writing a dozen options
    // against a slightly older SDK must not lose eleven of them to one name.
    TEST_CHECK(context->setProperties(handle, "{\"nope\":1,\"rangeStart\":3}") ==
               RESULT_UNKNOWN_PROPERTY, "an unknown key is reported");
    TEST_CHECK(near(options->getRangeStart(), 3, 1e-6), "and the others still applied");

    TEST_CHECK(context->setProperties(handle, "[1,2]") == RESULT_BAD_SPEC,
               "an array is not a set of properties");
    TEST_CHECK(context->setProperties(handle, "nonsense") == RESULT_BAD_SPEC,
               "and neither is anything that does not parse");
}

/*
 * An alias resolves to the real property, including from a SUBCLASS - which is the whole reason
 * `fog.rangeStart` can be spelled that way while the table keeps the mechanical name.
 */
void testAliases() {
    auto context = std::make_shared<Context>();
    auto options = std::make_shared<FogOptions>();
    Handle handle = NULL_HANDLE;
    context->registerObject("options", "fog", options, "massif::FogOptions", handle);

    TEST_CHECK(std::string(findAlias(findClass("massif::FogOptions"), "blend")) == "horizonBlend",
               "the alias table resolves");
    TEST_CHECK(findAlias(findClass("massif::FogOptions"), "nope") == nullptr,
               "an unknown alias does not");
    TEST_CHECK(findAlias(nullptr, "blend") == nullptr, "and a null class does not crash");

    TEST_CHECK(context->setProperty(handle, "blend", PropertyValue::ofDouble(0.25)) == RESULT_OK,
               "a write through an alias reaches the property");
    TEST_CHECK(near(options->getHorizonBlend(), 0.25, 1e-6), "with the value it was given");
    PropertyValue value;
    TEST_CHECK(context->getProperty(handle, "blend", value) == RESULT_OK &&
               near(value.asDouble(), 0.25, 1e-6), "and a read through it answers the same");
    TEST_CHECK(context->getProperty(handle, "nope", value) == RESULT_UNKNOWN_PROPERTY,
               "a name that is neither a property nor an alias is still UNKNOWN_PROPERTY");

    // Declared on Feature, used from VectorTileFeature - the base chain, and a variant path
    // continuing past the alias.
    std::map<std::string, Variant> properties;
    properties["name"] = Variant(std::string("Chamechaude"));
    auto feature = std::make_shared<VectorTileFeature>(
        1, MapTile(0, 0, 0, 0), "peaks",
        std::make_shared<PointGeometry>(MapPos(5.85, 45.29)), Variant(properties));
    Handle featureHandle = NULL_HANDLE;
    context->registerObject("feature", "f", feature, "massif::VectorTileFeature", featureHandle);
    TEST_CHECK(context->getProperty(featureHandle, "props.name", value) == RESULT_OK &&
               value.asString() == "Chamechaude",
               "an alias declared on a base resolves from a subclass, and the path walks on");
}

/*
 * The write side of the projection.
 *
 * A read names its projection per call; a write could not, so an app holding metres had to convert
 * by hand - and a mistake reads as a plausible position somewhere else entirely.
 */
void testWriteProjection() {
    auto context = std::make_shared<Context>();
    auto mercator = std::make_shared<EPSG3857>();
    auto request = std::make_shared<GeocodingRequest>(mercator, "grenoble");
    Handle handle = NULL_HANDLE;
    context->registerObject("request", "r", request, "massif::GeocodingRequest", handle);

    PropertyValue metres = PropertyValue::ofString("[641200,5659384]");
    TEST_CHECK(context->setProperty(handle, "location", metres, "EPSG:3857") == RESULT_OK,
               "a position writes in a named projection");
    TEST_CHECK(near(request->getLocation().getX(), 641200, 1.0) &&
               near(request->getLocation().getY(), 5659384, 1.0),
               "and is left alone, because the object is already in it");

    PropertyValue degrees = PropertyValue::ofString("[5.76,45.24]");
    TEST_CHECK(context->setProperty(handle, "location", degrees, "EPSG:4326") == RESULT_OK,
               "the same value in degrees writes too");
    TEST_CHECK(near(request->getLocation().getX(), 641200, 1.0),
               "converted into the object's own projection");

    TEST_CHECK(context->setProperty(handle, "location", degrees, "EPSG:9999") ==
               RESULT_UNKNOWN_TYPE,
               "a projection nobody registered is an error, never a silent pass-through");
    TEST_CHECK(near(request->getLocation().getX(), 641200, 1.0), "and wrote nothing");

    // The ABI's own half: doubles in, no JSON for the caller to format. The default context,
    // because that is the one mm_* addresses.
    Handle shared = NULL_HANDLE;
    Context::GetDefault()->registerObject("request", "cabi", request, "massif::GeocodingRequest",
                                          shared);
    double wgs84[2] = { 6.865, 45.832 };
    TEST_CHECK(mm_set_position(mm_context_default(), shared, "location", "EPSG:4326", wgs84, 2) ==
               MM_OK, "mm_set_position writes from doubles");
    TEST_CHECK(near(request->getLocation().getX(), 764000, 2000.0),
               "converted into the object's projection, so a binding never names one twice");
    TEST_CHECK(mm_set_position(mm_context_default(), shared, "location", "", nullptr, 2) ==
               MM_UNSUPPORTED_TYPE, "a null array is refused");
    Context::GetDefault()->unregisterObject("request", "cabi");
}
