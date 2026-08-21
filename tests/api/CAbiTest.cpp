/*
 * Tests for the flat C ABI: the two-call buffer protocol, the option JSON, out-params being
 * optional, and a null context being refused everywhere rather than crashing.
 *
 * Deliberately written the way a binding would - through mm_* only, no C++ types.
 */

#include "api/MassifApiC.h"

#include "api/Builtins.h"
#include "api/Context.h"
#include "api/Methods.h"
#include "api/Spec.h"
#include "components/FogOptions.h"
#include "core/BinaryData.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "TestCheck.h"

using namespace massif;
using namespace massif::api;

namespace {

    /**
     * The builtins for this program.
     *
     * The SDK's Builtins.cpp pulls in every source, layer and method implementation, which is what
     * the host tests cannot link. A separate program may define the symbol itself, and this is what
     * lets mm_create and mm_call be exercised over a handful of classes.
     */
    Result fogFactory(Context&, const Variant&, ObjectRef& object, std::set<std::string>& consumed) {
        consumed.insert("type");
        object.obj = std::make_shared<FogOptions>();
        object.cppClass = "massif::FogOptions";
        return RESULT_OK;
    }

    Result blobMethod(Context& context, void*, const CallArgs& args, PropertyValue& result) {
        std::string text;
        if (!args.getString(0, text)) {
            return RESULT_BAD_SPEC;
        }
        std::vector<unsigned char> bytes(text.begin(), text.end());
        Handle handle = NULL_HANDLE;
        Result registered = context.registerResult("result", std::make_shared<BinaryData>(bytes),
                                                   "massif::BinaryData", handle);
        if (registered != RESULT_OK) {
            return registered;
        }
        result.type = PT_OBJECT;
        result.intValue = handle;
        return RESULT_OK;
    }

    Result rampMethod(Context& context, void*, const CallArgs& args, PropertyValue& result) {
        long long count = 0;
        if (!args.getLong(0, count)) {
            return RESULT_BAD_SPEC;
        }
        auto values = std::make_shared<std::vector<double> >();
        for (long long index = 0; index < count; index++) {
            values->push_back(index * 2.0);
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

    /** Returns a string, so the two-call buffer protocol has something to carry. */
    Result textMethod(Context&, void*, const CallArgs&, PropertyValue& result) {
        result = PropertyValue::ofString("a value long enough to need a buffer");
        return RESULT_OK;
    }

    int seen = 0;
    mm_handle lastPayload = MM_NULL_HANDLE;

    int abiHandler(void*, mm_handle, const char*, mm_handle payload) {
        seen++;
        lastPayload = payload;
        return 0;
    }

    int posted = 0;
    void countingDispatcher(void*, void (*)(void*), void*) { posted++; }

}

namespace massif { namespace api {
    void registerBuiltins() {
        static bool registered = false;
        if (registered) {
            return;
        }
        registered = true;
        Spec::registerFactory("options", &fogFactory);
        Methods::registerMethod("massif::FogOptions", "blob", &blobMethod);
        Methods::registerMethod("massif::FogOptions", "ramp", &rampMethod);
        Methods::registerMethod("massif::FogOptions", "text", &textMethod);
    }
} }

void testCAbi() {
    TEST_CHECK(mm_abi_version() == 1, "the ABI reports its version");
    TEST_CHECK(std::strcmp(mm_result_name(MM_UNKNOWN_PROPERTY), "MM_UNKNOWN_PROPERTY") == 0,
               "a result code has a readable name");
    TEST_CHECK(std::strcmp(mm_result_name(9999), "MM_UNKNOWN") == 0,
               "and one from a newer build does not read as a wrong name");

    mm_ctx ctx = mm_context_default();
    TEST_CHECK(ctx != nullptr, "the default context exists");

    // A null context is refused everywhere rather than dereferenced.
    TEST_CHECK(mm_create(nullptr, "options", "x", "{}", nullptr) == MM_BAD_CONTEXT,
               "a null context is refused by create");
    TEST_CHECK(mm_get_double(nullptr, 1, "a", nullptr) == MM_BAD_CONTEXT, "and by get");
    TEST_CHECK(mm_drain(nullptr, nullptr) == MM_BAD_CONTEXT, "and by drain");

    mm_handle fog = MM_NULL_HANDLE;
    TEST_CHECK(mm_create(ctx, "options", "cfog", "{\"type\":\"fog\",\"rangeStart\":2.5}", &fog) == MM_OK,
               "create builds from a spec");
    TEST_CHECK(fog != MM_NULL_HANDLE, "and hands back a handle");

    mm_handle found = MM_NULL_HANDLE;
    TEST_CHECK(mm_find(ctx, "options", "cfog", &found) == MM_OK && found == fog, "find resolves it");
    TEST_CHECK(mm_find(ctx, "options", "nope", &found) == MM_BAD_HANDLE && found == MM_NULL_HANDLE,
               "an unknown id is an error and clears the out-param");

    // Every out-param is optional: a caller that only wants the result code passes null.
    TEST_CHECK(mm_find(ctx, "options", "cfog", nullptr) == MM_OK, "find without an out-param");

    double number = 0;
    TEST_CHECK(mm_get_double(ctx, fog, "rangeStart", &number) == MM_OK && number == 2.5,
               "a spec key the factory did not consume was applied");
    TEST_CHECK(mm_set_double(ctx, fog, "rangeStart", 4) == MM_OK &&
               mm_get_double(ctx, fog, "rangeStart", &number) == MM_OK && number == 4,
               "set and get a double");

    int flag = 0;
    TEST_CHECK(mm_set_bool(ctx, fog, "enabled", 0) == MM_OK &&
               mm_get_bool(ctx, fog, "enabled", &flag) == MM_OK && flag == 0, "set and get a bool");
    TEST_CHECK(mm_set_bool(ctx, fog, "enabled", 1) == MM_OK &&
               mm_get_bool(ctx, fog, "enabled", &flag) == MM_OK && flag == 1, "and back again");

    int64_t colour = 0;
    TEST_CHECK(mm_set_long(ctx, fog, "color", 0xFF804020) == MM_OK &&
               mm_get_long(ctx, fog, "color", &colour) == MM_OK && colour == 0xFF804020,
               "a colour round-trips through the long accessors");

    TEST_CHECK(mm_get_double(ctx, fog, "nope", &number) == MM_UNKNOWN_PROPERTY,
               "an unknown property is reported");
    TEST_CHECK(mm_get_double(ctx, fog + 7777, "rangeStart", &number) == MM_BAD_HANDLE,
               "so is a stale handle");

    TEST_CHECK(mm_set_string(ctx, fog, "rangeStart", "3") == MM_OK &&
               mm_get_double(ctx, fog, "rangeStart", &number) == MM_OK && number == 3,
               "a number written as a string still lands as a number");

    // The two-call buffer protocol, over a string result.
    mm_handle text = MM_NULL_HANDLE;
    TEST_CHECK(mm_call(ctx, fog, "text", nullptr, &text) == MM_OK, "a string result");
    size_t needed = 0;
    TEST_CHECK(mm_get_string(ctx, text, "", nullptr, nullptr, 0, &needed) == MM_OK && needed > 1,
               "a null buffer asks for the size");
    std::vector<char> buffer(needed);
    TEST_CHECK(mm_get_string(ctx, text, "", nullptr, buffer.data(), buffer.size(), nullptr) == MM_OK,
               "and the second call fills it");
    TEST_CHECK(std::strlen(buffer.data()) == needed - 1, "the size counted the terminating NUL");

    char tiny[2];
    size_t retry = 0;
    TEST_CHECK(mm_get_string(ctx, text, "", nullptr, tiny, sizeof(tiny), &retry) ==
               MM_BUFFER_TOO_SMALL, "a short buffer is refused, not truncated");
    TEST_CHECK(retry == needed, "and it says how much was needed, so a retry costs one call");
    mm_destroy_handle(ctx, text);

    // call: a binary result, read without becoming a string.
    mm_handle blob = MM_NULL_HANDLE;
    TEST_CHECK(mm_call(ctx, fog, "blob", "[\"hello\"]", &blob) == MM_OK && blob != MM_NULL_HANDLE,
               "call returns a handle the caller owns");
    size_t size = 0;
    TEST_CHECK(mm_data_size(ctx, blob, "", &size) == MM_OK && size == 5, "the blob knows its size");
    std::vector<unsigned char> bytes(size);
    size_t copied = 0;
    TEST_CHECK(mm_data_copy(ctx, blob, "", bytes.data(), bytes.size(), &copied) == MM_OK &&
               copied == 5 && std::memcmp(bytes.data(), "hello", 5) == 0, "and copies out");
    TEST_CHECK(mm_data_copy(ctx, blob, "", tiny, 1, &copied) == MM_BUFFER_TOO_SMALL,
               "a short binary buffer is refused too");
    TEST_CHECK(mm_destroy_handle(ctx, blob) == MM_OK, "the result destroys by handle");
    TEST_CHECK(mm_data_size(ctx, blob, "", &size) == MM_BAD_HANDLE, "and is gone");

    // call: a bulk numeric result, flat.
    mm_handle ramp = MM_NULL_HANDLE;
    TEST_CHECK(mm_call(ctx, fog, "ramp", "[1000]", &ramp) == MM_OK, "a bulk numeric result");
    size_t count = 0;
    TEST_CHECK(mm_doubles_count(ctx, ramp, &count) == MM_OK && count == 1000, "with a count");
    std::vector<double> values(count);
    TEST_CHECK(mm_doubles_copy(ctx, ramp, values.data(), values.size(), &copied) == MM_OK &&
               copied == 1000 && values[999] == 1998, "copied flat in one crossing");
    TEST_CHECK(mm_doubles_copy(ctx, ramp, values.data(), 10, &copied) == MM_BUFFER_TOO_SMALL,
               "a short numeric buffer is refused");
    mm_destroy_handle(ctx, ramp);

    TEST_CHECK(mm_call(ctx, fog, "nope", nullptr, nullptr) == MM_UNKNOWN_METHOD,
               "an unknown method is reported");
    TEST_CHECK(mm_call(ctx, fog, "blob", "[]", nullptr) == MM_BAD_SPEC,
               "and arguments that do not fit it");
}

void testCAbiEvents() {
    mm_ctx ctx = mm_context_default();
    mm_handle fog = MM_NULL_HANDLE;
    mm_create(ctx, "options", "cevents", "{\"type\":\"fog\"}", &fog);

    seen = 0;
    mm_subscription subscription = MM_NULL_SUBSCRIPTION;
    TEST_CHECK(mm_on(ctx, fog, "tick", nullptr, nullptr, nullptr, &subscription) == MM_BAD_SPEC,
               "a null handler is refused");
    TEST_CHECK(mm_on(ctx, fog, "tick", &abiHandler, nullptr, "not json", &subscription) == MM_BAD_SPEC,
               "so is unparseable option JSON");
    TEST_CHECK(mm_on(ctx, fog, "tick", &abiHandler, nullptr, "{\"delivery\":\"sideways\"}",
                     &subscription) == MM_BAD_SPEC, "and an unknown delivery thread");
    TEST_CHECK(mm_on(ctx, fog + 7777, "tick", &abiHandler, nullptr, nullptr, &subscription) ==
               MM_BAD_HANDLE, "and a stale target");
    TEST_CHECK(subscription == MM_NULL_SUBSCRIPTION, "none of them left a subscription behind");

    TEST_CHECK(mm_on(ctx, fog, "tick", &abiHandler, nullptr, nullptr, &subscription) == MM_OK &&
               subscription != MM_NULL_SUBSCRIPTION, "a plain subscription is accepted");
    Context::GetDefault()->emit(fog, "tick", MM_NULL_HANDLE);
    TEST_CHECK(seen == 1, "and the handler ran");

    TEST_CHECK(mm_off(ctx, subscription) == MM_OK, "off removes it");
    TEST_CHECK(mm_off(ctx, subscription) == MM_BAD_HANDLE, "and says so the second time");

    // The option JSON, and off_event / off_all reporting counts through an optional out-param.
    mm_subscription ui = MM_NULL_SUBSCRIPTION;
    posted = 0;
    mm_set_ui_dispatcher(ctx, &countingDispatcher, nullptr);
    TEST_CHECK(mm_on(ctx, fog, "tick", &abiHandler, nullptr,
                     "{\"delivery\":\"ui\",\"coalesce\":true}", &ui) == MM_OK,
               "options are read from JSON, so a new one never changes the signature");
    seen = 0;
    Context::GetDefault()->emit(fog, "tick", MM_NULL_HANDLE);
    Context::GetDefault()->emit(fog, "tick", MM_NULL_HANDLE);
    TEST_CHECK(seen == 0 && posted == 1, "a ui subscription queues rather than running inline");
    int delivered = 0;
    TEST_CHECK(mm_drain(ctx, &delivered) == MM_OK && delivered == 1 && seen == 1,
               "drain delivers it, coalesced to one");
    mm_set_ui_dispatcher(ctx, nullptr, nullptr);

    TEST_CHECK(mm_on(ctx, fog, "other", &abiHandler, nullptr, nullptr, nullptr) == MM_OK,
               "on without an out-param");
    int removed = 0;
    TEST_CHECK(mm_off_event(ctx, fog, "tick", &removed) == MM_OK && removed == 1,
               "off_event reports how many it removed");
    TEST_CHECK(mm_off_all(ctx, fog, &removed) == MM_OK && removed == 1, "and so does off_all");
    TEST_CHECK(mm_off_all(ctx, fog, nullptr) == MM_OK, "which is optional too");

    // An async call, delivered as an event, and cancellable.
    seen = 0;
    lastPayload = MM_NULL_HANDLE;
    mm_on(ctx, fog, "ramp.done", &abiHandler, nullptr, nullptr, nullptr);
    mm_call_id call = MM_NULL_CALL;
    TEST_CHECK(mm_call_async(ctx, fog, "ramp", "[4]", "ramp.done", &call) == MM_OK &&
               call != MM_NULL_CALL, "call_async hands back a call id");
    Context::GetDefault()->waitForCalls();
    TEST_CHECK(seen == 1 && lastPayload != MM_NULL_HANDLE, "the result arrived as an event");
    TEST_CHECK(mm_cancel_call(ctx, call) == MM_BAD_HANDLE, "a finished call cannot be cancelled");

    TEST_CHECK(mm_call_async(ctx, fog, "nope", nullptr, "x.done", &call) == MM_UNKNOWN_METHOD,
               "an unknown method is refused before anything is queued");
    int cancelled = -1;
    TEST_CHECK(mm_cancel_calls(ctx, fog, &cancelled) == MM_OK && cancelled == 0,
               "cancel_calls reports zero when there is nothing to stop");

    TEST_CHECK(mm_destroy(ctx, "options", "cevents") == MM_OK, "destroy by kind and id");
    TEST_CHECK(mm_destroy(ctx, "options", "cevents") == MM_BAD_HANDLE, "and only once");
}
