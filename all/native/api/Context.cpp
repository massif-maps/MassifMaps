#include "api/Context.h"
#include "api/Spec.h"
#include "utils/Log.h"

#include <set>

namespace massif { namespace api {

    Context::Context() {
        // Slot 0 is never handed out, so NULL_HANDLE cannot collide with a real object.
        _slots.resize(1);
    }

    Context::~Context() {
    }

    const std::shared_ptr<Context>& Context::GetDefault() {
        static std::shared_ptr<Context> defaultContext = std::make_shared<Context>();
        return defaultContext;
    }

    Result Context::registerObject(const std::string& kind, const std::string& id,
                                   const std::shared_ptr<void>& obj, const char* cppClass,
                                   Handle& handle) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto& kindIds = _ids[kind];
        if (kindIds.find(id) != kindIds.end()) {
            return RESULT_DUPLICATE_ID;
        }
        handle = allocate(obj, cppClass);
        kindIds[id] = handle;
        return RESULT_OK;
    }

    Result Context::create(const std::string& kind, const std::string& id, const std::string& json,
                           Handle& handle) {
        Variant spec;
        try {
            spec = Variant::FromString(json);
        } catch (const std::exception& e) {
            Log::Errorf("Context::create: %s does not parse: %s", id.c_str(), e.what());
            return RESULT_BAD_SPEC;
        }
        // toString is the canonical form: picojson keeps object keys sorted, so two specs that
        // mean the same thing compare equal whatever order they were written in.
        std::string canonical = spec.toString();

        {
            std::lock_guard<std::mutex> lock(_mutex);
            Handle existing = findObjectLocked(kind, id);
            if (existing != NULL_HANDLE) {
                const Slot* slot = resolve(existing);
                if (slot && slot->spec == canonical) {
                    handle = existing;
                    return RESULT_OK;
                }
                return RESULT_DUPLICATE_ID;
            }
        }

        ObjectRef object;
        std::set<std::string> consumed;
        Result result = Spec::build(*this, kind, spec, object, consumed);
        if (result != RESULT_OK) {
            return result;
        }

        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (findObjectLocked(kind, id) != NULL_HANDLE) {
                return RESULT_DUPLICATE_ID;
            }
            handle = allocate(object.obj, object.cppClass);
            _slots[handle & INDEX_MASK].spec = canonical;
            _ids[kind][id] = handle;
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
                propertyValue.boolValue = value.getBool();
                break;
            case VariantType::VARIANT_TYPE_INTEGER:
                propertyValue.intValue = value.getLong();
                propertyValue.floatValue = static_cast<double>(value.getLong());
                break;
            case VariantType::VARIANT_TYPE_DOUBLE:
                propertyValue.floatValue = value.getDouble();
                propertyValue.intValue = static_cast<long long>(value.getDouble());
                break;
            case VariantType::VARIANT_TYPE_STRING:
                propertyValue.stringValue = value.getString();
                break;
            default:
                Log::Warnf("Context::create: %s.%s is not a scalar, ignored", id.c_str(), key.c_str());
                continue;
            }
            Result applied = setProperty(handle, key, propertyValue);
            if (applied != RESULT_OK) {
                Log::Warnf("Context::create: %s.%s ignored (%d)", id.c_str(), key.c_str(), applied);
            }
        }
        return RESULT_OK;
    }

    std::shared_ptr<void> Context::getObject(Handle handle) const {
        std::lock_guard<std::mutex> lock(_mutex);
        const Slot* slot = resolve(handle);
        return slot ? slot->obj : std::shared_ptr<void>();
    }

    Handle Context::findObject(const std::string& kind, const std::string& id) const {
        std::lock_guard<std::mutex> lock(_mutex);
        return findObjectLocked(kind, id);
    }

    Handle Context::findObjectLocked(const std::string& kind, const std::string& id) const {
        auto kindIt = _ids.find(kind);
        if (kindIt == _ids.end()) {
            return NULL_HANDLE;
        }
        auto idIt = kindIt->second.find(id);
        return idIt == kindIt->second.end() ? NULL_HANDLE : idIt->second;
    }

    bool Context::unregisterObject(const std::string& kind, const std::string& id) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto kindIt = _ids.find(kind);
        if (kindIt == _ids.end()) {
            return false;
        }
        auto idIt = kindIt->second.find(id);
        if (idIt == kindIt->second.end()) {
            return false;
        }

        std::uint32_t index = idIt->second & INDEX_MASK;
        if (index < _slots.size() && _slots[index].used) {
            Slot& slot = _slots[index];
            slot.obj.reset();
            slot.cppClass = nullptr;
            slot.used = false;
            slot.spec.clear();
            // Bumping the generation is what turns a stale handle into an error instead of a
            // reference to whatever takes the slot next. Wrapping is the ABA window.
            slot.generation = slot.generation >= MAX_GENERATION ? 1 : slot.generation + 1;
            _freeSlots.push_back(index);
        }
        kindIt->second.erase(idIt);
        return true;
    }

    std::size_t Context::getObjectCount() const {
        std::lock_guard<std::mutex> lock(_mutex);
        std::size_t count = 0;
        for (const Slot& slot : _slots) {
            if (slot.used) {
                count++;
            }
        }
        return count;
    }

    Result Context::getProperty(Handle handle, const std::string& path, PropertyValue& value) const {
        std::lock_guard<std::mutex> lock(_mutex);
        ObjectRef target;
        Result result = RESULT_OK;
        const PropertyEntry* entry = lookup(handle, path, target, result);
        if (!entry) {
            return result;
        }
        if (!entry->getter) {
            return RESULT_UNSUPPORTED_TYPE;
        }
        entry->getter(target.obj.get(), value);
        return RESULT_OK;
    }

    Result Context::setProperty(Handle handle, const std::string& path, const PropertyValue& value) {
        std::lock_guard<std::mutex> lock(_mutex);
        ObjectRef target;
        Result result = RESULT_OK;
        const PropertyEntry* entry = lookup(handle, path, target, result);
        if (!entry) {
            return result;
        }
        if (entry->flags & PF_READONLY) {
            return RESULT_READONLY;
        }
        if (!entry->setter) {
            return RESULT_UNSUPPORTED_TYPE;
        }
        // The generated thunk calls the class' own setter, so the option-changed notification and
        // therefore the redraw granularity are exactly those of a direct call.
        entry->setter(target.obj.get(), value);
        return RESULT_OK;
    }

    Handle Context::allocate(const std::shared_ptr<void>& obj, const char* cppClass) {
        std::uint32_t index;
        if (!_freeSlots.empty()) {
            index = _freeSlots.back();
            _freeSlots.pop_back();
        } else {
            index = static_cast<std::uint32_t>(_slots.size());
            _slots.resize(_slots.size() + 1);
        }
        Slot& slot = _slots[index];
        slot.obj = obj;
        slot.cppClass = cppClass;
        slot.used = true;
        return (slot.generation << INDEX_BITS) | index;
    }

    const Context::Slot* Context::resolve(Handle handle) const {
        std::uint32_t index = handle & INDEX_MASK;
        if (handle == NULL_HANDLE || index >= _slots.size()) {
            return nullptr;
        }
        const Slot& slot = _slots[index];
        if (!slot.used || slot.generation != (handle >> INDEX_BITS)) {
            return nullptr;
        }
        return &slot;
    }

    const PropertyEntry* Context::lookup(Handle handle, const std::string& path,
                                         ObjectRef& target, Result& result) const {
        const Slot* slot = resolve(handle);
        if (!slot) {
            result = RESULT_BAD_HANDLE;
            return nullptr;
        }
        target.obj = slot->obj;
        target.cppClass = slot->cppClass;

        // A dotted path walks OBJECT properties: every segment but the last has to be one, and
        // the reference keeps each intermediate alive while the walk continues.
        std::size_t start = 0;
        while (true) {
            std::size_t dot = path.find('.', start);
            std::string segment = path.substr(start, dot == std::string::npos ? dot : dot - start);

            const ClassEntry* classEntry = findClass(target.cppClass);
            if (!classEntry) {
                result = RESULT_UNKNOWN_CLASS;
                return nullptr;
            }
            const PropertyEntry* entry = findProperty(classEntry, segment.c_str());
            if (!entry) {
                result = RESULT_UNKNOWN_PROPERTY;
                return nullptr;
            }
            if (dot == std::string::npos) {
                return entry;
            }
            if (!entry->objectGetter) {
                result = RESULT_NOT_TRAVERSABLE;
                return nullptr;
            }
            ObjectRef next;
            entry->objectGetter(target.obj.get(), next);
            if (!next.obj) {
                result = RESULT_NULL_OBJECT;
                return nullptr;
            }
            target = next;
            start = dot + 1;
        }
    }

} }
