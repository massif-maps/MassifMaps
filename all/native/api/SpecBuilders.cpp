#include "api/SpecBuilders.h"

namespace massif { namespace api {

    std::string stringAt(const Variant& spec, const char* key, const std::string& fallback) {
        return spec.containsObjectKey(key) ? spec.getObjectElement(key).getString() : fallback;
    }

    int intAt(const Variant& spec, const char* key, int fallback) {
        return spec.containsObjectKey(key)
             ? static_cast<int>(spec.getObjectElement(key).getLong()) : fallback;
    }

    double floatAt(const Variant& spec, const char* key, double fallback) {
        return spec.containsObjectKey(key) ? spec.getObjectElement(key).getDouble() : fallback;
    }

    bool boolAt(const Variant& spec, const char* key, bool fallback) {
        return spec.containsObjectKey(key) ? spec.getObjectElement(key).getBool() : fallback;
    }

    Variant variantAt(const Variant& spec, const char* key) {
        return spec.containsObjectKey(key) ? spec.getObjectElement(key) : Variant();
    }

    Result childOf(Context& context, const Variant& spec, const char* key, const char* kind,
                   const char* requiredClass, std::shared_ptr<void>& out) {
        if (!spec.containsObjectKey(key)) {
            return RESULT_UNKNOWN_PROPERTY;
        }
        Variant child = spec.getObjectElement(key);
        if (child.getType() == VariantType::VARIANT_TYPE_STRING) {
            out = context.getObject(context.findObject(kind, child.getString()), requiredClass);
            return out ? RESULT_OK : RESULT_BAD_HANDLE;
        }
        ObjectRef object;
        std::set<std::string> consumed;
        Result result = Spec::build(context, kind, child, object, consumed);
        if (result != RESULT_OK) {
            return result;
        }
        if (!isSubclassOf(object.cppClass, requiredClass)) {
            return RESULT_UNKNOWN_CLASS;
        }
        out = object.obj;
        return RESULT_OK;
    }

} }

// The generated builders, at file scope because they bring their own class headers - and their own
// namespace. Last, so the helpers above are already defined.
#include "api/SpecConstructors.inc"
