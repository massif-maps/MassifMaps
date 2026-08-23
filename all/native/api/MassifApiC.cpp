#include "api/MassifApiC.h"
#include "api/Builtins.h"
#include "api/Context.h"
#include "api/Spec.h"
#include "core/BinaryData.h"
#include "core/Variant.h"

#include <cstring>
#include <string>
#include <vector>

using namespace massif;
using namespace massif::api;

namespace {

    // The C codes mirror Result one for one, so a translation table would be a thing to forget to
    // update. These fail the build instead.
    static_assert(MM_OK == RESULT_OK, "MM_OK");
    static_assert(MM_BAD_HANDLE == RESULT_BAD_HANDLE, "MM_BAD_HANDLE");
    static_assert(MM_UNKNOWN_CLASS == RESULT_UNKNOWN_CLASS, "MM_UNKNOWN_CLASS");
    static_assert(MM_UNKNOWN_PROPERTY == RESULT_UNKNOWN_PROPERTY, "MM_UNKNOWN_PROPERTY");
    static_assert(MM_READONLY == RESULT_READONLY, "MM_READONLY");
    static_assert(MM_UNSUPPORTED_TYPE == RESULT_UNSUPPORTED_TYPE, "MM_UNSUPPORTED_TYPE");
    static_assert(MM_DUPLICATE_ID == RESULT_DUPLICATE_ID, "MM_DUPLICATE_ID");
    static_assert(MM_NOT_TRAVERSABLE == RESULT_NOT_TRAVERSABLE, "MM_NOT_TRAVERSABLE");
    static_assert(MM_NULL_OBJECT == RESULT_NULL_OBJECT, "MM_NULL_OBJECT");
    static_assert(MM_BAD_SPEC == RESULT_BAD_SPEC, "MM_BAD_SPEC");
    static_assert(MM_UNKNOWN_TYPE == RESULT_UNKNOWN_TYPE, "MM_UNKNOWN_TYPE");
    static_assert(MM_UNKNOWN_METHOD == RESULT_UNKNOWN_METHOD, "MM_UNKNOWN_METHOD");
    static_assert(MM_FAILED == RESULT_FAILED, "MM_FAILED");
    static_assert(MM_REJECTED == RESULT_REJECTED, "MM_REJECTED");

    Context* resolve(mm_ctx ctx) {
        return static_cast<Context*>(ctx);
    }

    /** A null char* is an empty string, so a caller need not carry one just to pass nothing. */
    std::string text(const char* value) {
        return value ? std::string(value) : std::string();
    }

    /**
     * Copies a string out under the two-call protocol: a null buffer asks for the size, a short
     * one is refused rather than truncated. `needed` counts the terminating NUL.
     */
    int copyOut(const std::string& value, char* buffer, size_t size, size_t* needed) {
        size_t required = value.size() + 1;
        if (needed) {
            *needed = required;
        }
        if (!buffer) {
            return MM_OK;
        }
        if (size < required) {
            return MM_BUFFER_TOO_SMALL;
        }
        std::memcpy(buffer, value.c_str(), required);
        return MM_OK;
    }

    /** Reads one property, with the handle and context checks every getter repeats. */
    int read(mm_ctx ctx, mm_handle handle, const char* path, const char* projection,
             PropertyValue& value) {
        Context* context = resolve(ctx);
        if (!context) {
            return MM_BAD_CONTEXT;
        }
        return context->getProperty(handle, text(path), value, text(projection));
    }

    int write(mm_ctx ctx, mm_handle handle, const char* path, const PropertyValue& value) {
        Context* context = resolve(ctx);
        if (!context) {
            return MM_BAD_CONTEXT;
        }
        return context->setProperty(handle, text(path), value);
    }

}

extern "C" {

int mm_abi_version(void) {
    return 1;
}

const char* mm_result_name(int result) {
    switch (result) {
    case MM_OK:                 return "MM_OK";
    case MM_BAD_HANDLE:         return "MM_BAD_HANDLE";
    case MM_UNKNOWN_CLASS:      return "MM_UNKNOWN_CLASS";
    case MM_UNKNOWN_PROPERTY:   return "MM_UNKNOWN_PROPERTY";
    case MM_READONLY:           return "MM_READONLY";
    case MM_UNSUPPORTED_TYPE:   return "MM_UNSUPPORTED_TYPE";
    case MM_DUPLICATE_ID:       return "MM_DUPLICATE_ID";
    case MM_NOT_TRAVERSABLE:    return "MM_NOT_TRAVERSABLE";
    case MM_NULL_OBJECT:        return "MM_NULL_OBJECT";
    case MM_BAD_SPEC:           return "MM_BAD_SPEC";
    case MM_UNKNOWN_TYPE:       return "MM_UNKNOWN_TYPE";
    case MM_UNKNOWN_METHOD:     return "MM_UNKNOWN_METHOD";
    case MM_FAILED:             return "MM_FAILED";
    case MM_REJECTED:           return "MM_REJECTED";
    case MM_BAD_CONTEXT:        return "MM_BAD_CONTEXT";
    case MM_BUFFER_TOO_SMALL:   return "MM_BUFFER_TOO_SMALL";
    default:                    return "MM_UNKNOWN";
    }
}

mm_ctx mm_context_default(void) {
    return Context::GetDefault().get();
}

int mm_create(mm_ctx ctx, const char* kind, const char* id, const char* json, mm_handle* out) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    registerBuiltins();
    Handle handle = NULL_HANDLE;
    Result result = Spec::create(*context, text(kind), text(id), text(json), handle);
    if (out) {
        *out = result == RESULT_OK ? handle : MM_NULL_HANDLE;
    }
    return result;
}

int mm_destroy(mm_ctx ctx, const char* kind, const char* id) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    return context->unregisterObject(text(kind), text(id)) ? MM_OK : MM_BAD_HANDLE;
}

int mm_destroy_handle(mm_ctx ctx, mm_handle handle) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    return context->destroy(handle) ? MM_OK : MM_BAD_HANDLE;
}

int mm_find(mm_ctx ctx, const char* kind, const char* id, mm_handle* out) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    Handle handle = context->findObject(text(kind), text(id));
    if (out) {
        *out = handle;
    }
    return handle == NULL_HANDLE ? MM_BAD_HANDLE : MM_OK;
}

int mm_valid(mm_ctx ctx, mm_handle handle) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    return context->getObject(handle) ? MM_OK : MM_BAD_HANDLE;
}

int mm_set_bool(mm_ctx ctx, mm_handle handle, const char* path, int value) {
    return write(ctx, handle, path, PropertyValue::ofBool(value != 0));
}

int mm_set_long(mm_ctx ctx, mm_handle handle, const char* path, int64_t value) {
    return write(ctx, handle, path, PropertyValue::ofLong(value));
}

int mm_set_double(mm_ctx ctx, mm_handle handle, const char* path, double value) {
    return write(ctx, handle, path, PropertyValue::ofDouble(value));
}

int mm_set_string(mm_ctx ctx, mm_handle handle, const char* path, const char* value) {
    return write(ctx, handle, path, PropertyValue::ofString(text(value)));
}

int mm_set_object(mm_ctx ctx, mm_handle handle, const char* path, mm_handle value) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    return context->setObjectProperty(handle, text(path), value);
}

int mm_get_bool(mm_ctx ctx, mm_handle handle, const char* path, int* value) {
    PropertyValue read_;
    int result = read(ctx, handle, path, nullptr, read_);
    if (result == MM_OK && value) {
        *value = read_.asBool() ? 1 : 0;
    }
    return result;
}

int mm_get_long(mm_ctx ctx, mm_handle handle, const char* path, int64_t* value) {
    PropertyValue read_;
    int result = read(ctx, handle, path, nullptr, read_);
    if (result == MM_OK && value) {
        *value = read_.asLong();
    }
    return result;
}

int mm_get_double(mm_ctx ctx, mm_handle handle, const char* path, double* value) {
    PropertyValue read_;
    int result = read(ctx, handle, path, nullptr, read_);
    if (result == MM_OK && value) {
        *value = read_.asDouble();
    }
    return result;
}

int mm_get_string(mm_ctx ctx, mm_handle handle, const char* path, const char* projection,
                  char* buffer, size_t size, size_t* needed) {
    PropertyValue value;
    int result = read(ctx, handle, path, projection, value);
    if (result != MM_OK) {
        return result;
    }
    return copyOut(value.stringValue, buffer, size, needed);
}

int mm_get_position(mm_ctx ctx, mm_handle handle, const char* path, const char* projection,
                    double* out, size_t count, size_t* needed) {
    if (needed) {
        *needed = 0;
    }
    PropertyValue value;
    int result = read(ctx, handle, path, projection, value);
    if (result != MM_OK) {
        return result;
    }
    // The value crosses as the same JSON every struct does; this only saves the CALLER the parse,
    // it does not add a second wire format. Anything that is not an array of numbers is refused
    // rather than half-filled.
    Variant variant;
    try {
        variant = Variant::FromString(value.stringValue);
    } catch (const std::exception&) {
        return MM_UNSUPPORTED_TYPE;
    }
    if (variant.getType() != VariantType::VARIANT_TYPE_ARRAY) {
        return MM_UNSUPPORTED_TYPE;
    }
    std::size_t size = variant.getArraySize();
    for (std::size_t i = 0; i < size; i++) {
        VariantType::VariantType type = variant.getArrayElement(static_cast<int>(i)).getType();
        if (type != VariantType::VARIANT_TYPE_INTEGER && type != VariantType::VARIANT_TYPE_DOUBLE) {
            return MM_UNSUPPORTED_TYPE;
        }
    }
    if (needed) {
        *needed = size;
    }
    if (!out) {
        return size > count ? MM_BUFFER_TOO_SMALL : MM_OK;
    }
    for (std::size_t i = 0; i < size && i < count; i++) {
        out[i] = variant.getArrayElement(static_cast<int>(i)).getDouble();
    }
    return size > count ? MM_BUFFER_TOO_SMALL : MM_OK;
}

int mm_data_size(mm_ctx ctx, mm_handle handle, const char* path, size_t* size) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    std::shared_ptr<BinaryData> data;
    Result result = context->getData(handle, text(path), data);
    if (result != RESULT_OK) {
        return result;
    }
    if (size) {
        *size = data->size();
    }
    return MM_OK;
}

int mm_data_copy(mm_ctx ctx, mm_handle handle, const char* path, void* buffer, size_t size,
                 size_t* copied) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    std::shared_ptr<BinaryData> data;
    Result result = context->getData(handle, text(path), data);
    if (result != RESULT_OK) {
        return result;
    }
    if (copied) {
        *copied = data->size();
    }
    if (!buffer) {
        return MM_OK;
    }
    if (size < data->size()) {
        return MM_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, data->data(), data->size());
    return MM_OK;
}

int mm_doubles_count(mm_ctx ctx, mm_handle handle, size_t* count) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    std::vector<double> values;
    Result result = context->getDoubles(handle, values);
    if (result != RESULT_OK) {
        return result;
    }
    if (count) {
        *count = values.size();
    }
    return MM_OK;
}

int mm_doubles_copy(mm_ctx ctx, mm_handle handle, double* buffer, size_t count, size_t* copied) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    std::vector<double> values;
    Result result = context->getDoubles(handle, values);
    if (result != RESULT_OK) {
        return result;
    }
    if (copied) {
        *copied = values.size();
    }
    if (!buffer) {
        return MM_OK;
    }
    if (count < values.size()) {
        return MM_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, values.data(), values.size() * sizeof(double));
    return MM_OK;
}

int mm_call(mm_ctx ctx, mm_handle handle, const char* method, const char* args_json,
            mm_handle* result) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    registerBuiltins();
    Handle out = NULL_HANDLE;
    Result called = context->callHandle(handle, text(method), text(args_json), out);
    if (result) {
        *result = called == RESULT_OK ? out : MM_NULL_HANDLE;
    }
    return called;
}

int mm_call_async(mm_ctx ctx, mm_handle handle, const char* method, const char* args_json,
                  const char* event, mm_call_id* call) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    registerBuiltins();
    Call queued = NULL_CALL;
    Result result = context->callAsync(handle, text(method), text(args_json), text(event), &queued);
    if (call) {
        *call = queued;
    }
    return result;
}

int mm_cancel_call(mm_ctx ctx, mm_call_id call) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    return context->cancelCall(call) ? MM_OK : MM_BAD_HANDLE;
}

int mm_cancel_calls(mm_ctx ctx, mm_handle handle, int* count) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    int cancelled = context->cancelCalls(handle);
    if (count) {
        *count = cancelled;
    }
    return MM_OK;
}

int mm_on(mm_ctx ctx, mm_handle handle, const char* event, mm_handler handler, void* user_data,
          const char* opts_json, mm_subscription* out) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    if (out) {
        *out = MM_NULL_SUBSCRIPTION;
    }
    if (!handler) {
        return MM_BAD_SPEC;
    }

    // Options rather than parameters, so a new one never changes this signature.
    Delivery delivery = DELIVERY_ORIGIN;
    bool consume = false;
    bool coalesce = false;
    std::string projection;
    std::string opts = text(opts_json);
    if (!opts.empty()) {
        Variant options;
        try {
            options = Variant::FromString(opts);
        } catch (const std::exception&) {
            return MM_BAD_SPEC;
        }
        if (options.getType() != VariantType::VARIANT_TYPE_OBJECT) {
            return MM_BAD_SPEC;
        }
        if (options.containsObjectKey("delivery")) {
            std::string name = options.getObjectElement("delivery").getString();
            if (name == "ui") {
                delivery = DELIVERY_UI;
            } else if (name == "background") {
                delivery = DELIVERY_BACKGROUND;
            } else if (name != "origin") {
                return MM_BAD_SPEC;
            }
        }
        // Guarded, not just read: getString on a missing key returns "null", which would look
        // like a projection nobody has heard of.
        if (options.containsObjectKey("consume")) {
            consume = options.getObjectElement("consume").getBool();
        }
        if (options.containsObjectKey("coalesce")) {
            coalesce = options.getObjectElement("coalesce").getBool();
        }
        if (options.containsObjectKey("projection")) {
            projection = options.getObjectElement("projection").getString();
        }
    }

    // mm_handler and EventHandler are the same type, so nothing is wrapped and there is no
    // trampoline whose lifetime the ABI would have to track.
    Subscription subscription = context->subscribe(handle, text(event), handler, user_data,
                                                   consume, delivery, coalesce, projection);
    if (subscription == NULL_SUBSCRIPTION) {
        return MM_BAD_HANDLE;
    }
    if (out) {
        *out = subscription;
    }
    return MM_OK;
}

int mm_off(mm_ctx ctx, mm_subscription subscription) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    return context->unsubscribe(subscription) ? MM_OK : MM_BAD_HANDLE;
}

int mm_off_event(mm_ctx ctx, mm_handle handle, const char* event, int* count) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    int removed = context->unsubscribeEvent(handle, text(event));
    if (count) {
        *count = removed;
    }
    return MM_OK;
}

int mm_off_all(mm_ctx ctx, mm_handle handle, int* count) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    int removed = context->unsubscribeAll(handle);
    if (count) {
        *count = removed;
    }
    return MM_OK;
}

int mm_set_ui_dispatcher(mm_ctx ctx, mm_dispatcher dispatcher, void* user_data) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    context->setUiDispatcher(dispatcher, user_data);
    return MM_OK;
}

int mm_drain(mm_ctx ctx, int* count) {
    Context* context = resolve(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    int delivered = context->drainQueue();
    if (count) {
        *count = delivered;
    }
    return MM_OK;
}

}
