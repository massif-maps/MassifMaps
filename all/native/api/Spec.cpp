#include "api/Spec.h"
#include "api/Context.h"
#include "utils/Log.h"

#include <map>

namespace massif { namespace api {

    namespace {
        // Keyed by "kind" for the SDK's own factories, and by "kind/type" for the ones a plugin
        // adds - so registering a type never displaces the built-ins for that kind.
        std::map<std::string, Spec::Factory>& factories() {
            static std::map<std::string, Spec::Factory> registry;
            return registry;
        }
    }

    void Spec::registerFactory(const std::string& kind, const std::string& type, Factory factory) {
        factories()[kind + "/" + type] = factory;
    }

    void Spec::registerFactory(const std::string& kind, Factory factory) {
        factories()[kind] = factory;
    }

    Result Spec::create(Context& context, const std::string& kind, const std::string& id,
                        const std::string& json, Handle& handle) {
        Variant spec;
        try {
            spec = Variant::FromString(json);
        } catch (const std::exception& e) {
            Log::Errorf("Spec::create: %s does not parse: %s", id.c_str(), e.what());
            return RESULT_BAD_SPEC;
        }
        // toString is the canonical form: picojson keeps object keys sorted, so two specs that
        // mean the same thing compare equal whatever order they were written in.
        std::string canonical = spec.toString();

        // An identical spec reuses: that is how two maps come to share one source without
        // coordinating. A different spec under the same id is a conflict, never a replace.
        Handle existing = context.findObject(kind, id);
        if (existing != NULL_HANDLE) {
            if (context.getObjectSpec(existing) == canonical) {
                handle = existing;
                return RESULT_OK;
            }
            return RESULT_DUPLICATE_ID;
        }

        ObjectRef object;
        std::set<std::string> consumed;
        Result result = build(context, kind, spec, object, consumed);
        if (result != RESULT_OK) {
            return result;
        }
        result = context.registerObject(kind, id, object.obj, object.cppClass, handle, canonical);
        if (result != RESULT_OK) {
            return result;
        }

        // Everything the factory did not need is a property. An option the SDK does not have is
        // a warning, so a spec from another version still applies what it can.
        for (const std::string& key : spec.getObjectKeys()) {
            if (consumed.count(key)) {
                continue;
            }
            Variant value = spec.getObjectElement(key);
            PropertyValue propertyValue;
            switch (value.getType()) {
            case VariantType::VARIANT_TYPE_BOOL:
                propertyValue.type = PT_BOOL;
                propertyValue.boolValue = value.getBool();
                break;
            case VariantType::VARIANT_TYPE_INTEGER:
                propertyValue.type = PT_INT;
                propertyValue.intValue = value.getLong();
                break;
            case VariantType::VARIANT_TYPE_DOUBLE:
                propertyValue.type = PT_FLOAT;
                propertyValue.floatValue = value.getDouble();
                break;
            case VariantType::VARIANT_TYPE_STRING:
                propertyValue.type = PT_STRING;
                propertyValue.stringValue = value.getString();
                break;
            default:
                Log::Warnf("Spec::create: %s.%s is not a scalar, ignored", id.c_str(), key.c_str());
                continue;
            }
            Result applied = context.setProperty(handle, key, propertyValue);
            if (applied != RESULT_OK) {
                Log::Warnf("Spec::create: %s.%s ignored (%d)", id.c_str(), key.c_str(), applied);
            }
        }
        return RESULT_OK;
    }


    Result Spec::build(Context& context, const std::string& kind, const Variant& spec,
                       ObjectRef& object, std::set<std::string>& consumed) {
        if (spec.getType() != VariantType::VARIANT_TYPE_OBJECT) {
            Log::Error("Spec: a spec has to be a JSON object");
            return RESULT_BAD_SPEC;
        }
        // A type-level factory wins, so a plugin's type is reached before the SDK's fallback.
        std::string type = spec.containsObjectKey("type") ? spec.getObjectElement("type").getString()
                                                          : std::string();
        auto it = factories().find(kind + "/" + type);
        if (it == factories().end()) {
            it = factories().find(kind);
        }
        if (it == factories().end()) {
            Log::Errorf("Spec: no kind '%s'", kind.c_str());
            return RESULT_UNKNOWN_TYPE;
        }
        return it->second(context, spec, object, consumed);
    }

} }
