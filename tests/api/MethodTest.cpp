/*
 * Tests for call/callAsync: argument decoding, the base-chain lookup, result ownership, the
 * binary channel, and an async result arriving as an event.
 */

#include "api/Context.h"
#include "api/GeometryMethods.h"
#include "api/RoutingMethods.h"
#include "api/StructCodec.h"
#include "api/Methods.h"
#include "core/BinaryData.h"
#include "core/MapTile.h"
#include "datasources/components/TileData.h"
#include "geometry/Feature.h"
#include "geometry/FeatureCollection.h"
#include "geometry/PointGeometry.h"
#include "geometry/VectorTileFeature.h"
#include "geometry/VectorTileFeatureCollection.h"
#include "projections/EPSG3857.h"
#include "routing/RoutingInstruction.h"
#include "routing/RoutingRequest.h"
#include "routing/RoutingResult.h"
#include "routing/RoutingService.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <string>

#include "TestCheck.h"

using namespace massif;
using namespace massif::api;

namespace {

    // What the last call saw, so a test can assert on the decoding rather than only the result.
    struct Seen {
        MapTile tile;
        std::vector<MapPos> positions;
        std::string text;
        bool ran = false;
    };

    Seen seen;

    /** Returns a number, so the scalar-result path is exercised. */
    Result sum(Context&, void* obj, const CallArgs& args, PropertyValue& result) {
        std::vector<MapPos> positions;
        if (!args.getPositions(0, positions)) {
            return RESULT_BAD_SPEC;
        }
        seen.positions = positions;
        seen.ran = true;
        double total = 0;
        for (const MapPos& pos : positions) {
            total += pos.getX();
        }
        (void)obj;
        result = PropertyValue::ofDouble(total);
        return RESULT_OK;
    }

    /** Returns an object, so the handle-result and ownership paths are exercised. */
    Result makeTile(Context& context, void*, const CallArgs& args, PropertyValue& result) {
        MapTile tile;
        std::string text;
        if (!args.getTile(0, tile) || !args.getString(1, text)) {
            return RESULT_BAD_SPEC;
        }
        seen.tile = tile;
        seen.text = text;
        std::vector<unsigned char> bytes(text.begin(), text.end());
        auto data = std::make_shared<TileData>(std::make_shared<BinaryData>(bytes));

        Handle handle = NULL_HANDLE;
        Result registered = context.registerResult("result", data, "massif::TileData", handle);
        if (registered != RESULT_OK) {
            return registered;
        }
        result.type = PT_OBJECT;
        result.intValue = handle;
        return RESULT_OK;
    }

    Result fails(Context&, void*, const CallArgs&, PropertyValue&) {
        return RESULT_FAILED;
    }

    /** Throws rather than returning a code - the SDK's own setters and methods do validate. */
    Result throws(Context&, void*, const CallArgs&, PropertyValue&) {
        throw std::runtime_error("refused");
    }

    /** Returns a flat array of numbers, so the bulk channel is exercised. */
    Result doubles(Context& context, void*, const CallArgs& args, PropertyValue& result) {
        long long count = 0;
        if (!args.getLong(0, count)) {
            return RESULT_BAD_SPEC;
        }
        auto values = std::make_shared<std::vector<double> >();
        for (long long index = 0; index < count; index++) {
            values->push_back(index + 0.5);
        }
        Handle handle = NULL_HANDLE;
        Result registered = context.registerResult("result", values,
                                                   Context::DOUBLE_VECTOR_CLASS, handle);
        if (registered != RESULT_OK) {
            return registered;
        }
        result.type = PT_OBJECT;
        result.intValue = handle;
        return RESULT_OK;
    }

    /** Blocks until released, so a test can cancel a call while it is running. */
    std::mutex slowMutex;
    std::condition_variable slowCondition;
    bool slowReleased = false;
    int slowRuns = 0;

    Result slow(Context&, void*, const CallArgs&, PropertyValue& result) {
        std::unique_lock<std::mutex> lock(slowMutex);
        slowRuns++;
        slowCondition.notify_all();
        slowCondition.wait(lock, []() { return slowReleased; });
        result = PropertyValue::ofLong(1);
        return RESULT_OK;
    }

    /** Registered on Geometry, called on a PointGeometry, to prove the chain walk. */
    Result onBase(Context&, void*, const CallArgs&, PropertyValue& result) {
        result = PropertyValue::ofString("base");
        return RESULT_OK;
    }

    struct Received {
        Context* context = nullptr;
        double value = 0;
        std::uint32_t payload = 0;
        int count = 0;
    };

    int receivingHandler(void* userData, std::uint32_t, const char*, std::uint32_t payload) {
        Received* received = static_cast<Received*>(userData);
        received->payload = payload;
        received->count++;
        PropertyValue value;
        if (received->context->getProperty(payload, "", value) == RESULT_OK) {
            received->value = value.asDouble();
        }
        return false;
    }

    Handle registerPoint(const std::shared_ptr<Context>& context, const std::string& id) {
        Handle handle = NULL_HANDLE;
        context->registerObject("geometry", id, std::make_shared<PointGeometry>(MapPos(1, 2)),
                                "massif::PointGeometry", handle);
        return handle;
    }

}

void testCallArgs() {
    CallArgs args;
    TEST_CHECK(CallArgs::parse("", args) && args.count() == 0, "no arguments is an empty list");
    TEST_CHECK(!CallArgs::parse("not json", args), "malformed JSON is refused");
    TEST_CHECK(!CallArgs::parse("{\"a\":1}", args), "an object is not an argument list");

    TEST_CHECK(CallArgs::parse("[true,7,2.5,\"x\",[1,2],[[1,2],[3,4]],[8467,5852,14]]", args) &&
               args.count() == 7, "a full list parses");

    bool flag = false;
    long long number = 0;
    double real = 0;
    std::string text;
    TEST_CHECK(args.getBool(0, flag) && flag, "a bool");
    TEST_CHECK(args.getLong(1, number) && number == 7, "an integer");
    TEST_CHECK(args.getDouble(2, real) && real == 2.5, "a double");
    TEST_CHECK(args.getString(3, text) && text == "x", "a string");

    // JSON has one number type, so 7 is a valid double argument - but not a valid string.
    TEST_CHECK(args.getDouble(1, real) && real == 7, "an integer reads as a double");
    TEST_CHECK(!args.getLong(2, number), "but a double does not read as an integer");
    TEST_CHECK(!args.getString(1, text), "and a number is not a string");
    TEST_CHECK(!args.getBool(1, flag), "nor a bool");

    MapPos pos;
    TEST_CHECK(args.getPos(4, pos) && pos == MapPos(1, 2, 0), "a position");
    std::vector<MapPos> positions;
    TEST_CHECK(args.getPositions(5, positions) && positions.size() == 2 &&
               positions[1] == MapPos(3, 4, 0), "an array of positions");
    MapTile tile;
    TEST_CHECK(args.getTile(6, tile) && tile.getX() == 8467 && tile.getY() == 5852 &&
               tile.getZoom() == 14, "a tile");

    TEST_CHECK(!args.getPos(9, pos), "an index past the end is refused");
    TEST_CHECK(!args.getTile(4, tile), "a two-element array is not a tile");
    TEST_CHECK(!args.getPositions(6, positions), "and a tile is not an array of positions");
}

void testCall() {
    Methods::registerMethod("massif::TileData", "sum", &sum);
    Methods::registerMethod("massif::TileData", "makeTile", &makeTile);
    Methods::registerMethod("massif::TileData", "fails", &fails);
    Methods::registerMethod("massif::TileData", "doubles", &doubles);
    Methods::registerMethod("massif::TileData", "slow", &slow);
    Methods::registerMethod("massif::TileData", "throws", &throws);
    Methods::registerMethod("massif::Geometry", "onBase", &onBase);

    auto context = std::make_shared<Context>();
    auto data = std::make_shared<TileData>(std::make_shared<BinaryData>());
    Handle target = NULL_HANDLE;
    context->registerObject("tile", "t", data, "massif::TileData", target);

    PropertyValue value;
    TEST_CHECK(context->call(target, "sum", "[[[1,2],[3,4]]]", value) == RESULT_OK &&
               value.asDouble() == 4, "a method runs and returns its value");
    TEST_CHECK(seen.positions.size() == 2, "with its arguments decoded");

    TEST_CHECK(context->call(target, "nope", "", value) == RESULT_UNKNOWN_METHOD,
               "an unknown method is reported");
    TEST_CHECK(context->call(target + 7777, "sum", "[]", value) == RESULT_BAD_HANDLE,
               "so is a stale handle");
    TEST_CHECK(context->call(target, "sum", "nonsense", value) == RESULT_BAD_SPEC,
               "and unparseable arguments");
    TEST_CHECK(context->call(target, "sum", "[]", value) == RESULT_BAD_SPEC,
               "and arguments that do not fit the method");
    TEST_CHECK(context->call(target, "fails", "", value) == RESULT_FAILED,
               "a method that fails says so");
    // An exception crossing into Java or Objective-C kills the process, so it stops here.
    TEST_CHECK(context->call(target, "throws", "", value) == RESULT_REJECTED,
               "a method that throws is caught, not propagated");

    // A method registered on a base is callable on a subclass, without being registered again.
    Handle point = registerPoint(context, "p");
    TEST_CHECK(context->call(point, "onBase", "", value) == RESULT_OK &&
               value.stringValue == "base", "the lookup walks the base chain");
    TEST_CHECK(context->call(point, "sum", "[]", value) == RESULT_UNKNOWN_METHOD,
               "but not sideways into an unrelated class");

    // Traversal names the CONCRETE class, so what only the subclass has is reachable. Feature
    // declares geometry as a Geometry; pos is PointGeometry's alone.
    Handle held = NULL_HANDLE;
    context->registerObject("feature", "held",
                            std::make_shared<Feature>(std::make_shared<PointGeometry>(MapPos(7, 8)),
                                                      Variant()),
                            "massif::Feature", held);
    TEST_CHECK(context->getProperty(held, "geometry.centerPos", value) == RESULT_OK,
               "a base property through a path, as before");
    TEST_CHECK(context->getProperty(held, "geometry.pos", value) == RESULT_OK &&
               value.stringValue.find("7") != std::string::npos,
               "and a property only the concrete class declares");
    Methods::registerMethod("massif::PointGeometry", "onPoint", &onBase);
    TEST_CHECK(context->call(held, "geometry.onPoint", "", value) == RESULT_OK,
               "a method registered on the concrete class is found through a path too");

    // A method addressed through a path, so an intermediate object needs no id of its own.
    Handle feature = NULL_HANDLE;
    context->registerObject("feature", "pathed",
                            std::make_shared<Feature>(std::make_shared<PointGeometry>(MapPos(3, 4)),
                                                      Variant()),
                            "massif::Feature", feature);
    TEST_CHECK(context->call(feature, "geometry.onBase", "", value) == RESULT_OK &&
               value.stringValue == "base", "a path walks to the object the method belongs to");
    TEST_CHECK(context->call(feature, "nosuch.onBase", "", value) == RESULT_UNKNOWN_PROPERTY,
               "an unknown segment on the way is reported");
    TEST_CHECK(context->call(feature, "geometryGeoJSON.onBase", "", value) == RESULT_NOT_TRAVERSABLE,
               "and a scalar on the way is not traversable");

    // callHandle: one rule, a handle either way.
    Handle result = NULL_HANDLE;
    TEST_CHECK(context->callHandle(target, "sum", "[[[10,0],[5,0]]]", result) == RESULT_OK &&
               result != NULL_HANDLE, "a scalar result comes back as a handle");
    TEST_CHECK(context->getProperty(result, "", value) == RESULT_OK && value.asDouble() == 15,
               "an empty path reads the whole document");
    TEST_CHECK(context->destroy(result), "and the caller owns it");
    TEST_CHECK(context->getProperty(result, "", value) == RESULT_BAD_HANDLE, "so destroy frees it");

    // An object result is the object, and its binary property is readable without a string.
    TEST_CHECK(context->callHandle(target, "makeTile", "[[1,2,3],\"hello\"]", result) == RESULT_OK,
               "an object result comes back as a handle too");
    TEST_CHECK(seen.tile.getZoom() == 3 && seen.text == "hello", "with its arguments decoded");
    std::shared_ptr<BinaryData> blob;
    TEST_CHECK(context->getData(result, "data", blob) == RESULT_OK && blob->size() == 5,
               "getData reaches the blob through a path");
    TEST_CHECK(std::string(reinterpret_cast<const char*>(blob->data()), blob->size()) == "hello",
               "and the bytes survive");
    TEST_CHECK(context->getData(result, "", blob) == RESULT_UNSUPPORTED_TYPE,
               "an empty path only works when the handle IS the blob");
    TEST_CHECK(context->getData(result, "maxAge", blob) == RESULT_UNSUPPORTED_TYPE,
               "and a non-object property is not binary");
    context->destroy(result);

    TEST_CHECK(context->callHandle(target, "sum", "[[[1.5,0],[2.5,0]]]", result) == RESULT_OK &&
               context->getProperty(result, "", value) == RESULT_OK && value.asDouble() == 4,
               "a fractional result survives the Variant round trip");
    context->destroy(result);

    // Bulk numerics come back flat, not as JSON and not as a per-element proxy.
    std::vector<double> values;
    TEST_CHECK(context->callHandle(target, "doubles", "[2000]", result) == RESULT_OK,
               "a bulk numeric result is a handle");
    TEST_CHECK(context->getDoubles(result, values) == RESULT_OK && values.size() == 2000,
               "getDoubles reads the whole array at once");
    TEST_CHECK(values[0] == 0.5 && values[1999] == 1999.5, "with the values intact");
    TEST_CHECK(context->getProperty(result, "", value) == RESULT_UNKNOWN_CLASS,
               "and it is not a document: a container has no properties");
    context->destroy(result);
    TEST_CHECK(context->getDoubles(result, values) == RESULT_BAD_HANDLE, "destroy frees it");

    TEST_CHECK(context->getDoubles(target, values) == RESULT_UNSUPPORTED_TYPE,
               "a handle that is not a numeric result is refused");
    TEST_CHECK(context->callHandle(target, "sum", "[[[1,0]]]", result) == RESULT_OK &&
               context->getDoubles(result, values) == RESULT_UNSUPPORTED_TYPE,
               "and so is a scalar result, rather than being coerced into one value");
    context->destroy(result);
}

/*
 * A collection, read one element at a time - the channel a search result comes back through.
 *
 * The search services themselves are not exercised here: linking one would pull in a tile source
 * and a CartoCSS decoder, which is the point at which a check belongs on a device instead. What is
 * checked is everything between the service and the caller.
 */
void testCollections() {
    registerGeometryMethods();

    auto context = std::make_shared<Context>();
    std::vector<std::shared_ptr<VectorTileFeature> > features;
    for (int index = 0; index < 3; index++) {
        // Distinct positions and layer names, so reading element 2 cannot pass by reading element 0.
        features.push_back(std::make_shared<VectorTileFeature>(
            100 + index, MapTile(1, 2, 3, 0), "layer" + std::to_string(index),
            std::make_shared<PointGeometry>(MapPos(10 + index, 20 + index)), Variant()));
    }
    Handle collection = NULL_HANDLE;
    context->registerObject("result", "c", std::make_shared<VectorTileFeatureCollection>(features),
                            "massif::VectorTileFeatureCollection", collection);

    PropertyValue value;
    TEST_CHECK(context->getProperty(collection, "featureCount", value) == RESULT_OK &&
               value.asDouble() == 3, "the count is a property, not a method");

    Handle feature = NULL_HANDLE;
    TEST_CHECK(context->callHandle(collection, "getFeature", "[2]", feature) == RESULT_OK,
               "an element comes back as a handle");
    TEST_CHECK(context->getProperty(feature, "id", value) == RESULT_OK && value.asDouble() == 102,
               "and it is the element that was asked for");
    // Registered as the SUBCLASS, so what only a vector tile feature carries survives the crossing.
    TEST_CHECK(context->getProperty(feature, "layerName", value) == RESULT_OK &&
               value.stringValue == "layer2", "a subclass property is readable");
    TEST_CHECK(context->getProperty(feature, "geometry.centerPos", value) == RESULT_OK &&
               value.stringValue.find("12") != std::string::npos,
               "and a path walks on into the element");
    // The tile a feature came from, which had no accessor until MapTile got a codec.
    TEST_CHECK(context->getProperty(feature, "mapTile", value) == RESULT_OK &&
               value.stringValue == "[1,2,3]", "and the tile it came from");
    // A path walks INTO a struct, the same way it walks into a Variant.
    TEST_CHECK(context->getProperty(feature, "mapTile.2", value) == RESULT_OK &&
               value.asLong() == 3, "a path walks into a struct value");
    TEST_CHECK(context->getProperty(feature, "geometry.centerPos.0", value) == RESULT_OK &&
               value.asDouble() == 12, "including one reached through an object property");
    TEST_CHECK(context->getProperty(feature, "mapTile.9", value) == RESULT_UNKNOWN_PROPERTY,
               "and an index past its end is reported");
    context->destroy(feature);

    TEST_CHECK(context->call(collection, "getFeature", "[3]", value) == RESULT_BAD_SPEC,
               "an index past the end is refused rather than throwing out of the SDK");
    TEST_CHECK(context->call(collection, "getFeature", "[-1]", value) == RESULT_BAD_SPEC,
               "so is a negative one");
    TEST_CHECK(context->call(collection, "getFeature", "[]", value) == RESULT_BAD_SPEC,
               "and a missing index");
    TEST_CHECK(context->call(collection, "getFeature", "[\"2\"]", value) == RESULT_BAD_SPEC,
               "a string index is not coerced");

    // An empty collection has no element 0 - the case a search that found nothing produces.
    Handle empty = NULL_HANDLE;
    context->registerObject("result", "e", std::make_shared<VectorTileFeatureCollection>(
                                std::vector<std::shared_ptr<VectorTileFeature> >()),
                            "massif::VectorTileFeatureCollection", empty);
    TEST_CHECK(context->call(empty, "getFeature", "[0]", value) == RESULT_BAD_SPEC,
               "an empty collection has nothing at 0");

    // The object-argument channel: what a method gets handed, and what it refuses.
    CallArgs args;
    Handle handle = NULL_HANDLE;
    TEST_CHECK(CallArgs::parse("[7,-1,\"7\",4294967296]", args), "handle arguments parse");
    TEST_CHECK(args.getHandle(0, handle) && handle == 7, "a handle is a number");
    TEST_CHECK(!args.getHandle(1, handle), "a negative one is not a handle");
    TEST_CHECK(!args.getHandle(2, handle), "nor a string");
    TEST_CHECK(!args.getHandle(3, handle), "nor one that does not fit 32 bits");

    TEST_CHECK(context->getObject(collection, "massif::FeatureCollection") != nullptr,
               "an object argument resolves as its base class");
    TEST_CHECK(context->getObject(collection, "massif::Feature") == nullptr,
               "but not as an unrelated one - the check is what stops a wrong handle being cast");
    TEST_CHECK(context->getObject(collection + 7777, "massif::FeatureCollection") == nullptr,
               "and a stale handle resolves to nothing");

    // A result is in the projection of whatever produced it, so its positions convert without the
    // caller knowing where they came from.
    context->setObjectProjection(collection, std::make_shared<EPSG3857>());
    Handle inherited = NULL_HANDLE;
    TEST_CHECK(context->callHandle(collection, "getFeature", "[0]", inherited) == RESULT_OK,
               "an element of a projected collection");
    TEST_CHECK(context->getProperty(inherited, "geometry.centerPos", value, "EPSG:4326") ==
               RESULT_OK, "reads in another projection");
    MapPos converted;
    TEST_CHECK(StructCodec::decode(value.stringValue, converted) &&
               std::fabs(converted.getX()) < 1e-3,
               "with the position really converted - 10 metres east of Greenwich is 0 degrees");
    context->destroy(inherited);
}

void testCallCancel() {
    auto context = std::make_shared<Context>();
    auto data = std::make_shared<TileData>(std::make_shared<BinaryData>());
    Handle target = NULL_HANDLE;
    context->registerObject("tile", "t", data, "massif::TileData", target);

    Received received;
    received.context = context.get();
    context->subscribe(target, "slow.done", &receivingHandler, &received, false);
    context->subscribe(target, "sum.done", &receivingHandler, &received, false);

    {
        std::lock_guard<std::mutex> lock(slowMutex);
        slowReleased = false;
        slowRuns = 0;
    }

    // One slow call occupies the worker; the rest queue behind it and can be cancelled outright.
    Call running = NULL_CALL;
    TEST_CHECK(context->callAsync(target, "slow", "", "slow.done", &running) == RESULT_OK &&
               running != NULL_CALL, "callAsync hands back a call id");
    {
        std::unique_lock<std::mutex> lock(slowMutex);
        slowCondition.wait(lock, []() { return slowRuns == 1; });
    }

    Call queued = NULL_CALL;
    context->callAsync(target, "sum", "[[[1,0]]]", "sum.done", &queued);
    TEST_CHECK(context->getPendingCallCount() == 2, "the second call is waiting behind it");
    TEST_CHECK(context->cancelCall(queued), "a queued call cancels");
    TEST_CHECK(context->getPendingCallCount() == 1, "and leaves the queue");
    TEST_CHECK(!context->cancelCall(queued), "cancelling it twice reports nothing to cancel");
    TEST_CHECK(!context->cancelCall(NULL_CALL), "and so does the null call");

    // The running one cannot be stopped, but its result is dropped rather than delivered.
    TEST_CHECK(context->cancelCall(running), "a running call cancels");
    {
        std::lock_guard<std::mutex> lock(slowMutex);
        slowReleased = true;
    }
    slowCondition.notify_all();
    context->waitForCalls();
    TEST_CHECK(slowRuns == 1, "it still ran to completion - the work cannot be aborted");
    TEST_CHECK(received.count == 0, "but neither call delivered an event");
    TEST_CHECK(!context->cancelCall(running), "and it is no longer cancellable");

    // Cancelling by target, and dying with the target.
    {
        std::lock_guard<std::mutex> lock(slowMutex);
        slowReleased = false;
        slowRuns = 0;
    }
    context->callAsync(target, "slow", "", "slow.done", &running);
    {
        std::unique_lock<std::mutex> lock(slowMutex);
        slowCondition.wait(lock, []() { return slowRuns == 1; });
    }
    context->callAsync(target, "sum", "[[[1,0]]]", "sum.done", &queued);
    context->callAsync(target, "sum", "[[[2,0]]]", "sum.done", nullptr);
    TEST_CHECK(context->cancelCalls(target) == 3, "cancelCalls takes the queued and the running");
    TEST_CHECK(context->getPendingCallCount() == 1, "only the one that cannot be stopped is left");

    {
        std::lock_guard<std::mutex> lock(slowMutex);
        slowReleased = true;
    }
    slowCondition.notify_all();
    context->waitForCalls();
    TEST_CHECK(received.count == 0, "and nothing was delivered");

    // Destroying the target cancels what is queued on it, the way its subscriptions die with it.
    {
        std::lock_guard<std::mutex> lock(slowMutex);
        slowReleased = false;
        slowRuns = 0;
    }
    context->callAsync(target, "slow", "", "slow.done", &running);
    {
        std::unique_lock<std::mutex> lock(slowMutex);
        slowCondition.wait(lock, []() { return slowRuns == 1; });
    }
    context->callAsync(target, "sum", "[[[1,0]]]", "sum.done", &queued);
    context->unregisterObject("tile", "t");
    TEST_CHECK(!context->cancelCall(queued), "a queued call died with its target");

    {
        std::lock_guard<std::mutex> lock(slowMutex);
        slowReleased = true;
    }
    slowCondition.notify_all();
    context->waitForCalls();
    TEST_CHECK(received.count == 0, "and the running one delivered nothing either");
}

void testCallAsync() {
    auto context = std::make_shared<Context>();
    auto data = std::make_shared<TileData>(std::make_shared<BinaryData>());
    Handle target = NULL_HANDLE;
    context->registerObject("tile", "t", data, "massif::TileData", target);

    Received received;
    received.context = context.get();
    context->subscribe(target, "sum.done", &receivingHandler, &received, false);

    TEST_CHECK(context->callAsync(target, "nope", "", "sum.done") == RESULT_UNKNOWN_METHOD,
               "an unknown method is refused before anything is queued");
    TEST_CHECK(context->callAsync(target, "sum", "nonsense", "sum.done") == RESULT_BAD_SPEC,
               "so are unparseable arguments");
    TEST_CHECK(context->callAsync(target, "sum", "[]", "") == RESULT_BAD_SPEC,
               "and an empty event name, which nothing could subscribe to");
    TEST_CHECK(context->getPendingCallCount() == 0, "none of them queued anything");

    TEST_CHECK(context->callAsync(target, "sum", "[[[6,0],[7,0]]]", "sum.done") == RESULT_OK,
               "a good call is accepted");
    context->waitForCalls();
    TEST_CHECK(received.count == 1, "the result arrived as an event");
    TEST_CHECK(received.value == 13, "with the value readable from the payload");
    PropertyValue afterwards;
    TEST_CHECK(context->getProperty(received.payload, "", afterwards) == RESULT_BAD_HANDLE,
               "and the payload is freed once the handlers have run");

    // A failure is reported as a payload of 0 rather than as a missing event.
    received.count = 0;
    received.value = 0;
    TEST_CHECK(context->callAsync(target, "sum", "[]", "sum.done") == RESULT_OK,
               "arguments that do not fit are only found when it runs");
    context->waitForCalls();
    TEST_CHECK(received.count == 1 && received.payload == NULL_HANDLE,
               "so the event fires with no payload");

    // Several calls run in submission order, and the target survives until each has finished.
    received.count = 0;
    for (int index = 0; index < 5; index++) {
        context->callAsync(target, "sum", "[[[1,0]]]", "sum.done");
    }
    context->waitForCalls();
    TEST_CHECK(received.count == 5, "every queued call was delivered");
    TEST_CHECK(context->getPendingCallCount() == 0, "and the queue is empty");
}

/*
 * Routing: the read path of a result, and what calculateRoute checks before it runs.
 *
 * The result is built here rather than fetched, because the concrete Valhalla services need sqlite
 * and the routing library. What is left to a device is the service; everything between it and the
 * caller is below.
 */

namespace {

    /** A service that answers without a network, so the argument checks can be exercised. */
    class StubRoutingService : public RoutingService {
    public:
        std::string getProfile() const override { return _profile; }
        void setProfile(const std::string& profile) override { _profile = profile; }

        std::shared_ptr<RouteMatchingResult> matchRoute(
            const std::shared_ptr<RouteMatchingRequest>&) const override {
            return std::shared_ptr<RouteMatchingResult>();
        }

        std::shared_ptr<RoutingResult> calculateRoute(
            const std::shared_ptr<RoutingRequest>& request) const override {
            seenPoints = static_cast<int>(request->getPoints().size());
            seenLanguage = request->getCustomParameter("language").toString();

            std::vector<MapPos> points;
            points.push_back(MapPos(1000, 2000));
            points.push_back(MapPos(3000, 4000));
            points.push_back(MapPos(5000, 6000));
            std::vector<RoutingInstruction> instructions;
            instructions.push_back(RoutingInstruction(
                RoutingAction::ROUTING_ACTION_HEAD_ON, 0, "First street", "Head on", 0, 90, 10, 5,
                Variant()));
            instructions.push_back(RoutingInstruction(
                RoutingAction::ROUTING_ACTION_TURN_RIGHT, 2, "Second street", "Turn right",
                90, 180, 20, 7, Variant()));
            return std::make_shared<RoutingResult>(std::make_shared<EPSG3857>(), points,
                                                   instructions, "{}");
        }

        mutable int seenPoints = 0;
        mutable std::string seenLanguage;

    private:
        std::string _profile;
    };

}

void testRouting() {
    registerRoutingMethods();

    auto context = std::make_shared<Context>();
    auto service = std::make_shared<StubRoutingService>();
    Handle serviceHandle = NULL_HANDLE;
    context->registerObject("routing", "s", service, "massif::RoutingService", serviceHandle);

    auto request = std::make_shared<RoutingRequest>(
        std::make_shared<EPSG3857>(), std::vector<MapPos>{ MapPos(0, 0), MapPos(10, 10) });
    Handle requestHandle = NULL_HANDLE;
    context->registerObject("routing", "r", request, "massif::RoutingRequest", requestHandle);

    PropertyValue value;
    TEST_CHECK(context->call(serviceHandle, "calculateRoute", "[]", value) == RESULT_BAD_SPEC,
               "calculateRoute needs a request");
    TEST_CHECK(context->call(serviceHandle, "calculateRoute",
                             "[" + std::to_string(requestHandle + 7777) + "]", value) ==
               RESULT_BAD_HANDLE, "a stale request handle is refused");
    TEST_CHECK(context->call(serviceHandle, "calculateRoute",
                             "[" + std::to_string(serviceHandle) + "]", value) ==
               RESULT_BAD_HANDLE, "and so is a handle onto something that is not a request");

    // A custom parameter is free-form JSON, so it goes through a method rather than a property.
    TEST_CHECK(context->call(requestHandle, "setCustomParameter", "[\"language\",\"fr-FR\"]",
                             value) == RESULT_OK, "a custom parameter is accepted");
    TEST_CHECK(context->call(requestHandle, "setCustomParameter", "[\"language\"]", value) ==
               RESULT_BAD_SPEC, "without a value it is not");

    Handle route = NULL_HANDLE;
    TEST_CHECK(context->callHandle(serviceHandle, "calculateRoute",
                                   "[" + std::to_string(requestHandle) + "]", route) == RESULT_OK,
               "a route comes back as a handle");
    TEST_CHECK(service->seenPoints == 2, "with the request's via points");
    TEST_CHECK(service->seenLanguage == "\"fr-FR\"", "and its custom parameter");

    TEST_CHECK(context->getProperty(route, "instructionCount", value) == RESULT_OK &&
               value.asDouble() == 2, "the instruction count is a property");
    TEST_CHECK(context->getProperty(route, "pointCount", value) == RESULT_OK &&
               value.asDouble() == 3, "so is the point count");
    TEST_CHECK(context->getProperty(route, "totalDistance", value) == RESULT_OK &&
               value.asDouble() == 30, "and the totals the SDK sums");

    // An instruction is a VALUE type, so the element is a copy on the heap - it still reads by name.
    Handle instruction = NULL_HANDLE;
    TEST_CHECK(context->callHandle(route, "getInstruction", "[1]", instruction) == RESULT_OK,
               "an instruction comes back as a handle");
    TEST_CHECK(context->getProperty(instruction, "streetName", value) == RESULT_OK &&
               value.stringValue == "Second street", "the one that was asked for");
    TEST_CHECK(context->getProperty(instruction, "pointIndex", value) == RESULT_OK &&
               value.asDouble() == 2, "with the index into the path it refers to");
    TEST_CHECK(context->getProperty(instruction, "action", value) == RESULT_OK &&
               value.asDouble() == RoutingAction::ROUTING_ACTION_TURN_RIGHT,
               "and its action as the enum constant");
    context->destroy(instruction);
    TEST_CHECK(context->call(route, "getInstruction", "[2]", value) == RESULT_BAD_SPEC,
               "an index past the end is refused");

    // The path is flat, through the bulk channel: JSON is what it exists to avoid.
    Handle path = NULL_HANDLE;
    std::vector<double> flat;
    TEST_CHECK(context->callHandle(route, "getPoints", "", path) == RESULT_OK &&
               context->getDoubles(path, flat) == RESULT_OK, "the path reads as flat doubles");
    TEST_CHECK(flat.size() == 6, "two numbers per position");
    TEST_CHECK(flat[0] == 1000 && flat[1] == 2000 && flat[4] == 5000 && flat[5] == 6000,
               "interleaved x, y, in order");
    context->destroy(path);

    // The result carries the service's projection, so its positions convert.
    TEST_CHECK(context->getProperty(route, "points", value) == RESULT_UNSUPPORTED_TYPE,
               "and points is deliberately NOT a readable property");
    context->destroy(route);
}

/*
 * A class with no instance.
 *
 * Log is the SDK's only all-static class. Every verb here is addressed by handle, so the context
 * gives such a class one at construction, derived from the table rather than named in code.
 */

#include "utils/Log.h"

void testStatics() {
    auto context = std::make_shared<Context>();

    Handle log = context->findObject("static", "Log");
    TEST_CHECK(log != NULL_HANDLE, "an all-static class is addressable without an instance");
    TEST_CHECK(context->findObject("static", "massif::Log") == NULL_HANDLE,
               "under its short name, not its qualified one");
    TEST_CHECK(context->findObject("static", "FogOptions") == NULL_HANDLE,
               "and a class with instance properties is not registered this way");

    bool before = Log::IsShowDebug();
    PropertyValue value;
    TEST_CHECK(context->getProperty(log, "showDebug", value) == RESULT_OK &&
               value.asBool() == before, "a static property reads");
    TEST_CHECK(context->setProperty(log, "showDebug", PropertyValue::ofBool(!before)) == RESULT_OK,
               "and writes");
    TEST_CHECK(Log::IsShowDebug() == !before, "reaching the SDK's own static setter");
    TEST_CHECK(context->getProperty(log, "showDebug", value) == RESULT_OK &&
               value.asBool() == !before, "and reads back what was written");
    Log::SetShowDebug(before);

    std::string tag = Log::GetTag();
    TEST_CHECK(context->setProperty(log, "tag", PropertyValue::ofString("probe")) == RESULT_OK &&
               Log::GetTag() == "probe", "a static string property too");
    Log::SetTag(tag);

    TEST_CHECK(context->getProperty(log, "nope", value) == RESULT_UNKNOWN_PROPERTY,
               "an unknown one is still reported");
}

/*
 * Two objects, two slow calls: they must overlap. One worker meant a 20 s search blocked a route
 * queued behind it, and a free-for-all pool would instead lose the order of five loadTiles on one
 * source - which the event cannot distinguish, since it carries the result and not the call id.
 */
void testCallConcurrency() {
    auto context = std::make_shared<Context>();
    auto first = std::make_shared<TileData>(std::make_shared<BinaryData>());
    auto second = std::make_shared<TileData>(std::make_shared<BinaryData>());
    Handle a = NULL_HANDLE, b = NULL_HANDLE;
    context->registerObject("tile", "a", first, "massif::TileData", a);
    context->registerObject("tile", "b", second, "massif::TileData", b);

    {
        std::lock_guard<std::mutex> lock(slowMutex);
        slowReleased = false;
        slowRuns = 0;
    }
    context->callAsync(a, "slow", "", "slow.done");
    context->callAsync(b, "slow", "", "slow.done");
    {
        // Bounded: with a single worker the second call never starts, and a suite that hangs
        // reports nothing. Five seconds is far past the microseconds this needs.
        std::unique_lock<std::mutex> lock(slowMutex);
        slowCondition.wait_for(lock, std::chrono::seconds(5), []() { return slowRuns == 2; });
    }
    TEST_CHECK(slowRuns == 2, "calls on different objects run at the same time");
    TEST_CHECK(context->getPendingCallCount() == 2, "and both count as pending");
    {
        std::lock_guard<std::mutex> lock(slowMutex);
        slowReleased = true;
    }
    slowCondition.notify_all();
    context->waitForCalls();
    TEST_CHECK(context->getPendingCallCount() == 0, "and both finish");

    // Same object: the second waits, whatever the pool has spare.
    {
        std::lock_guard<std::mutex> lock(slowMutex);
        slowReleased = false;
        slowRuns = 0;
    }
    context->callAsync(a, "slow", "", "slow.done");
    context->callAsync(a, "slow", "", "slow.done");
    {
        std::unique_lock<std::mutex> lock(slowMutex);
        slowCondition.wait_for(lock, std::chrono::seconds(5), []() { return slowRuns >= 1; });
    }
    // A worker is free - the previous pair grew the pool - so only the serialisation holds it back.
    TEST_CHECK(slowRuns == 1, "calls on ONE object are still serialised");
    {
        std::lock_guard<std::mutex> lock(slowMutex);
        slowReleased = true;
    }
    slowCondition.notify_all();
    context->waitForCalls();
    TEST_CHECK(slowRuns == 2, "and the second one did run, after the first");
}

/*
 * Factories generated from a constructor signature.
 *
 * The reduced table declares two: PointGeometry(const MapPos&) and Feature(shared_ptr<Geometry>,
 * Variant). Between them they cover every argument shape the emitter knows - a struct, a child
 * resolved by id or inline, and free-form JSON - plus the failure modes.
 */

#include "api/Spec.h"
#include "api/SpecBuilders.h"

namespace {

    Result generatedGeometry(Context& context, const Variant& spec, ObjectRef& object,
                             std::set<std::string>& consumed) {
        return buildFromConstructor(context, "geometry", spec, object, consumed);
    }

    Result generatedFeature(Context& context, const Variant& spec, ObjectRef& object,
                            std::set<std::string>& consumed) {
        return buildFromConstructor(context, "feature", spec, object, consumed);
    }

}

void testGeneratedFactories() {
    Spec::registerFactory("geometry", &generatedGeometry);
    Spec::registerFactory("feature", &generatedFeature);
    auto context = std::make_shared<Context>();

    // A struct argument, decoded by the same codec a property uses.
    Handle point = NULL_HANDLE;
    TEST_CHECK(Spec::create(*context, "geometry", "p", "{\"type\":\"point\",\"pos\":[5.5,45.25]}", point) == RESULT_OK,
               "a class builds from its constructor");
    PropertyValue value;
    TEST_CHECK(context->getProperty(point, "pos", value) == RESULT_OK &&
               value.stringValue.find("5.5") != std::string::npos &&
               value.stringValue.find("45.25") != std::string::npos,
               "with the struct argument decoded into it");

    // A child by id, and free-form JSON alongside it.
    Handle feature = NULL_HANDLE;
    TEST_CHECK(Spec::create(*context, "feature", "f",
                            "{\"type\":\"feature\",\"geometry\":\"p\","
                            "\"properties\":{\"name\":\"Aiguille\",\"ele\":3842}}", feature) == RESULT_OK,
               "a child argument resolves from the registry by id");
    TEST_CHECK(context->getProperty(feature, "geometry.pos", value) == RESULT_OK &&
               value.stringValue.find("5.5") != std::string::npos,
               "and it is the object that id named");
    TEST_CHECK(context->getProperty(feature, "properties.ele", value) == RESULT_OK &&
               value.asLong() == 3842, "a Variant argument arrives whole");

    // A child as an INLINE spec, built on the spot - no id for the intermediate.
    Handle nested = NULL_HANDLE;
    TEST_CHECK(Spec::create(*context, "feature", "n",
                            "{\"type\":\"feature\","
                            "\"geometry\":{\"type\":\"point\",\"pos\":[1,2]},"
                            "\"properties\":{}}", nested) == RESULT_OK,
               "a child argument also takes an inline spec");
    TEST_CHECK(context->getProperty(nested, "geometry.pos", value) == RESULT_OK &&
               value.stringValue.find("1") != std::string::npos,
               "built on the spot, with no id of its own");

    // The failure modes, which is what a generated builder has to get right to be trustworthy.
    Handle refused = NULL_HANDLE;
    TEST_CHECK(Spec::create(*context, "geometry", "bad1", "{\"type\":\"nosuch\"}", refused) == RESULT_UNKNOWN_TYPE,
               "a type no declared class claims is reported");
    TEST_CHECK(Spec::create(*context, "feature", "bad2", "{\"type\":\"feature\"}", refused) == RESULT_BAD_SPEC,
               "a missing required argument is refused, not defaulted");
    TEST_CHECK(Spec::create(*context, "feature", "bad3",
                            "{\"type\":\"feature\",\"geometry\":\"nosuchid\",\"properties\":{}}", refused) == RESULT_BAD_HANDLE,
               "a child id that names nothing is reported");
    TEST_CHECK(Spec::create(*context, "feature", "bad4",
                            "{\"type\":\"feature\",\"geometry\":\"f\",\"properties\":{}}", refused) == RESULT_BAD_HANDLE,
               "and one that names the wrong class - a Feature is not a Geometry");

    ObjectRef nothing;
    std::set<std::string> consumed;
    TEST_CHECK(buildFromConstructor(*context, "nosuchkind", Variant(), nothing, consumed) ==
               RESULT_UNKNOWN_TYPE, "an undeclared kind builds nothing");
}
