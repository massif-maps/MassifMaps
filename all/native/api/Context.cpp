#include "api/Context.h"

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

    Handle Context::findObject(const std::string& kind, const std::string& id) const {
        std::lock_guard<std::mutex> lock(_mutex);
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
        const Slot* slot = nullptr;
        Result result = RESULT_OK;
        const PropertyEntry* entry = lookup(handle, path, slot, result);
        if (!entry) {
            return result;
        }
        if (!entry->getter) {
            return RESULT_UNSUPPORTED_TYPE;
        }
        entry->getter(slot->obj.get(), value);
        return RESULT_OK;
    }

    Result Context::setProperty(Handle handle, const std::string& path, const PropertyValue& value) {
        std::lock_guard<std::mutex> lock(_mutex);
        const Slot* slot = nullptr;
        Result result = RESULT_OK;
        const PropertyEntry* entry = lookup(handle, path, slot, result);
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
        entry->setter(slot->obj.get(), value);
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
                                         const Slot*& slot, Result& result) const {
        slot = resolve(handle);
        if (!slot) {
            result = RESULT_BAD_HANDLE;
            return nullptr;
        }
        const ClassEntry* classEntry = findClass(slot->cppClass);
        if (!classEntry) {
            result = RESULT_UNKNOWN_CLASS;
            return nullptr;
        }
        const PropertyEntry* entry = findProperty(classEntry, path.c_str());
        if (!entry) {
            result = RESULT_UNKNOWN_PROPERTY;
            return nullptr;
        }
        return entry;
    }

} }
