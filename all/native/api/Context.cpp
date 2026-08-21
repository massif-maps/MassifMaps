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
                                   Handle& handle, const std::string& spec) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto& kindIds = _ids[kind];
        if (kindIds.find(id) != kindIds.end()) {
            return RESULT_DUPLICATE_ID;
        }
        handle = allocate(obj, cppClass);
        _slots[handle & INDEX_MASK].spec = spec;
        kindIds[id] = handle;
        return RESULT_OK;
    }

    std::string Context::getObjectSpec(Handle handle) const {
        std::lock_guard<std::mutex> lock(_mutex);
        const Slot* slot = resolve(handle);
        return slot ? slot->spec : std::string();
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
        // Subscriptions die with their target: otherwise the first destroy on an object with a
        // handler is a use-after-free, and that is not the app's job to prevent.
        _events.unsubscribeAll(idIt->second);
        kindIt->second.erase(idIt);
        return true;
    }

    Subscription Context::subscribe(Handle handle, const std::string& event, EventHandler handler,
                                    void* userData, bool consume) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!resolve(handle)) {
            return NULL_SUBSCRIPTION;
        }
        return _events.subscribe(handle, event, handler, userData, consume);
    }

    bool Context::unsubscribe(Subscription subscription) {
        std::lock_guard<std::mutex> lock(_mutex);
        return _events.unsubscribe(subscription);
    }

    int Context::unsubscribeEvent(Handle handle, const std::string& event) {
        std::lock_guard<std::mutex> lock(_mutex);
        return _events.unsubscribeEvent(handle, event);
    }

    int Context::unsubscribeAll(Handle handle) {
        std::lock_guard<std::mutex> lock(_mutex);
        return _events.unsubscribeAll(handle);
    }

    bool Context::emit(Handle handle, const std::string& event, Handle payload) {
        // Two phases. The handler list cannot be walked unlocked, and the handlers cannot run
        // under the lock - they are app code, and one that calls back would deadlock. So the
        // matching subscriptions are collected under the lock, then each is resolved again just
        // before it is called: a handler removed earlier in this same pass is skipped, and a
        // recycled slot fails the generation check rather than being called by mistake.
        std::vector<Subscription> subscriptions;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _events.collect(handle, event, subscriptions);
        }

        for (Subscription subscription : subscriptions) {
            EventHandler handler = nullptr;
            void* userData = nullptr;
            bool consume = false;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (!_events.lookup(subscription, handler, userData, consume)) {
                    continue;
                }
            }
            bool result = handler(userData, handle, event.c_str(), payload);
            if (consume && result) {
                return true;
            }
        }
        return false;
    }

    std::size_t Context::getSubscriptionCount() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _events.getSubscriptionCount();
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
