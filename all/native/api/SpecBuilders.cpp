#include "api/SpecBuilders.h"
#include "utils/Log.h"

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

    PropertyValue specValue(const Variant& value) {
        PropertyValue property;
        switch (value.getType()) {
        case VariantType::VARIANT_TYPE_BOOL:
            property.type = PT_BOOL; property.boolValue = value.getBool(); break;
        case VariantType::VARIANT_TYPE_INTEGER:
            property.type = PT_INT; property.intValue = value.getLong(); break;
        case VariantType::VARIANT_TYPE_DOUBLE:
            property.type = PT_FLOAT; property.floatValue = value.getDouble(); break;
        case VariantType::VARIANT_TYPE_STRING:
            property.type = PT_STRING; property.stringValue = value.getString(); break;
        default:
            // An array or an object is JSON in the string, which is what a struct thunk decodes.
            property.type = PT_STRUCT; property.stringValue = value.toString(); break;
        }
        return property;
    }

    void applySpecProperties(const ObjectRef& object, const Variant& spec,
                             std::set<std::string>& consumed) {
        const ClassEntry* classEntry = findClass(object.cppClass);
        if (!classEntry) {
            return;
        }
        for (const std::string& key : spec.getObjectKeys()) {
            if (consumed.count(key)) {
                continue;
            }
            const PropertyEntry* entry = findProperty(classEntry, key.c_str());
            if (!entry || !entry->setter) {
                Log::Warnf("Spec: %s has no writable '%s', ignored", object.cppClass, key.c_str());
                continue;
            }
            PropertyValue value = specValue(spec.getObjectElement(key));
            // Marked either way: the caller's object is not the one Spec::create registers, and an
            // immutable style would warn about every key it was built from.
            consumed.insert(key);
            try {
                entry->setter(object.obj.get(), value);
            } catch (const std::exception& ex) {
                Log::Errorf("Spec: %s.%s refused: %s", object.cppClass, key.c_str(), ex.what());
            }
        }
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
