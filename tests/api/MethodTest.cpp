/*
 * Tests for call/callAsync: argument decoding, the base-chain lookup, result ownership, the
 * binary channel, and an async result arriving as an event.
 */

#include "api/Context.h"
#include "api/Methods.h"
#include "core/BinaryData.h"
#include "core/MapTile.h"
#include "datasources/components/TileData.h"
#include "geometry/PointGeometry.h"

#include <memory>
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

    bool receivingHandler(void* userData, std::uint32_t, const char*, std::uint32_t payload) {
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

    // A method registered on a base is callable on a subclass, without being registered again.
    Handle point = registerPoint(context, "p");
    TEST_CHECK(context->call(point, "onBase", "", value) == RESULT_OK &&
               value.stringValue == "base", "the lookup walks the base chain");
    TEST_CHECK(context->call(point, "sum", "[]", value) == RESULT_UNKNOWN_METHOD,
               "but not sideways into an unrelated class");

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
