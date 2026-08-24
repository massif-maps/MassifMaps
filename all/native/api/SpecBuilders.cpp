#include "api/SpecBuilders.h"
#include "utils/Log.h"

#include <cstring>

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

    const char* specKindOf(const char* cppClass) {
        if (!cppClass) {
            return nullptr;
        }
        for (const SpecKindEntry* entry = SPEC_KIND_OF_CLASS; entry->cppClass; entry++) {
            if (std::strcmp(entry->cppClass, cppClass) == 0) {
                return entry->kind;
            }
        }
        return nullptr;
    }

    bool applyObjectSpecProperty(Context& context, const ObjectRef& object, const Variant& spec,
                                 const std::string& key) {
        const ClassEntry* classEntry = findClass(object.cppClass);
        const PropertyEntry* entry = classEntry ? findProperty(classEntry, key.c_str()) : nullptr;
        if (!entry || entry->type != PT_OBJECT || !entry->objectSetter) {
            return false;
        }
        const char* kind = specKindOf(entry->objectClass);
        if (!kind) {
            Log::Warnf("Spec: nothing builds a %s for %s.%s",
                       entry->objectClass, object.cppClass, key.c_str());
            return true;
        }
        ObjectRef child;
        child.cppClass = entry->objectClass;
        if (childOf(context, spec, key.c_str(), kind, entry->objectClass, child.obj) != RESULT_OK) {
            return true;
        }
        try {
            entry->objectSetter(object.obj.get(), child);
        } catch (const std::exception& ex) {
            Log::Errorf("Spec: %s.%s refused: %s", object.cppClass, key.c_str(), ex.what());
        }
        return true;
    }

    void applySpecProperties(Context& context, const ObjectRef& object, const Variant& spec,
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
            if (!entry || !(entry->setter || entry->objectSetter)) {
                Log::Warnf("Spec: %s has no writable '%s', ignored", object.cppClass, key.c_str());
                continue;
            }
            // Marked either way: the caller's object is not the one Spec::create registers, and an
            // immutable style would warn about every key it was built from.
            consumed.insert(key);
            if (applyObjectSpecProperty(context, object, spec, key)) {
                continue;
            }
            PropertyValue value = specValue(spec.getObjectElement(key));
            // An enum spelled by its constant name: JSON has no enums, and strtoll would read
            // "VECTOR_TILE_RENDER_ORDER_LAST" as 0 - a real value, applied without a word.
            if (entry->type == PT_ENUM && value.type == PT_STRING) {
                long long constant = 0;
                if (!enumValueOf(value.stringValue.c_str(), constant)) {
                    Log::Errorf("Spec: %s.%s: no enum constant '%s'", object.cppClass, key.c_str(),
                                value.stringValue.c_str());
                    continue;
                }
                value = PropertyValue::ofLong(constant);
            }
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
            if (!out) {
                // Said out loud: this came back as a bare RESULT_BAD_HANDLE, and every binding
                // renders that as "see the log" over a log with nothing in it. A STRING here is
                // an ID in the registry - a well-known name like "EPSG:4326" is a TYPE, and has
                // to be written as { "type": "EPSG:4326" } so the kind's factory builds it.
                Log::Errorf("Spec: '%s' names no registered %s (as %s); a well-known name is a "
                            "type, not an id - write { \"type\": \"%s\" }",
                            child.getString().c_str(), kind, requiredClass,
                            child.getString().c_str());
                return RESULT_BAD_HANDLE;
            }
            return RESULT_OK;
        }
        // A NUMBER is a handle, which is how an app shares an object it already holds - the source
        // an overlay draws with the base map's tiles, and anything else that must not be built
        // twice. An id only exists for something the app chose to name.
        if (child.getType() == VariantType::VARIANT_TYPE_INTEGER) {
            out = context.getObject(static_cast<Handle>(child.getLong()), requiredClass);
            if (!out) {
                Log::Errorf("Spec: handle %lld is not a live %s",
                            static_cast<long long>(child.getLong()), requiredClass);
                return RESULT_BAD_HANDLE;
            }
            return RESULT_OK;
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
        // A nested spec gets its leftover keys applied too. Only Spec::create used to do this, so
        // everything that was not a constructor argument was silently dropped one level down - a
        // source's HTTPHeaders or tmsScheme inside a layer spec went nowhere, with no warning.
        applySpecProperties(context, object, child, consumed);
        out = object.obj;
        return RESULT_OK;
    }

} }

// The generated builders, at file scope because they bring their own class headers - and their own
// namespace. Last, so the helpers above are already defined.
#include "api/SpecConstructors.inc"
