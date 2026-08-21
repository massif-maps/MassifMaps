#include "api/Context.h"
#include "api/Projections.h"
#include "api/StructCodec.h"
#include "core/Variant.h"
#include "projections/Projection.h"
#include "utils/Log.h"

#include <cmath>
#include <cstdlib>

namespace massif { namespace api {

    namespace {
        // The projection reads default to for the duration of one event handler. Per thread, so a
        // GL-thread emit and a UI-thread drain do not overwrite each other's.
        thread_local std::string tActiveProjection;

        /** Saves and restores, so a handler that emits another event nests correctly. */
        struct ScopedProjection {
            std::string saved;
            explicit ScopedProjection(const std::string& name) : saved(tActiveProjection) {
                tActiveProjection = name;
            }
            ~ScopedProjection() { tActiveProjection = saved; }
        };

        /**
         * Rewrites an encoded MapPos or MapBounds into another projection.
         *
         * The two shapes are told apart by decoding: bounds are a pair of positions, and a
         * position never parses as one. Corner-wise is right for the axis-aligned projections
         * reachable by name here.
         */
        bool finite(const MapPos& pos) {
            // Mercator sends the poles to infinity, and "inf" is not JSON. Refused rather than
            // handed over as a number that will not parse.
            return std::isfinite(pos.getX()) && std::isfinite(pos.getY()) && std::isfinite(pos.getZ());
        }

        bool reproject(std::string& json, const Projection& source, const Projection& target) {
            MapBounds bounds;
            if (StructCodec::decode(json, bounds)) {
                MapPos min = target.fromWgs84(source.toWgs84(bounds.getMin()));
                MapPos max = target.fromWgs84(source.toWgs84(bounds.getMax()));
                if (!finite(min) || !finite(max)) {
                    return false;
                }
                json = StructCodec::encode(MapBounds(min, max));
                return true;
            }
            MapPos pos;
            if (StructCodec::decode(json, pos)) {
                MapPos converted = target.fromWgs84(source.toWgs84(pos));
                if (!finite(converted)) {
                    return false;
                }
                json = StructCodec::encode(converted);
                return true;
            }
            return false;
        }
    }

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

    void Context::setObjectProjection(Handle handle, const std::shared_ptr<Projection>& projection) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (resolve(handle)) {
            _slots[handle & INDEX_MASK].projection = projection;
        }
    }

    std::shared_ptr<Projection> Context::getObjectProjection(Handle handle) const {
        std::lock_guard<std::mutex> lock(_mutex);
        const Slot* slot = resolve(handle);
        if (!slot) {
            return std::shared_ptr<Projection>();
        }
        ObjectRef target;
        target.obj = slot->obj;
        target.cppClass = slot->cppClass;
        return sourceProjection(target, handle);
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
            _slots[index].idDropped = true;
            freeSlot(index);
        }
        // Subscriptions die with their target: otherwise the first destroy on an object with a
        // handler is a use-after-free, and that is not the app's job to prevent.
        _events.unsubscribeAll(idIt->second);
        kindIt->second.erase(idIt);
        return true;
    }

    void Context::freeSlot(std::uint32_t index) {
        Slot& slot = _slots[index];
        if (!slot.used || !slot.idDropped || slot.retainCount > 0) {
            return;
        }
        slot.obj.reset();
        slot.cppClass = nullptr;
        slot.used = false;
        slot.idDropped = false;
        slot.spec.clear();
        slot.projection.reset();
        // Bumping the generation is what turns a stale handle into an error instead of a
        // reference to whatever takes the slot next. Wrapping is the ABA window.
        slot.generation = slot.generation >= MAX_GENERATION ? 1 : slot.generation + 1;
        _freeSlots.push_back(index);
    }

    void Context::retainLocked(Handle handle) {
        std::uint32_t index = handle & INDEX_MASK;
        if (resolve(handle)) {
            _slots[index].retainCount++;
        }
    }

    void Context::releaseLocked(Handle handle) {
        std::uint32_t index = handle & INDEX_MASK;
        if (!resolve(handle) || _slots[index].retainCount == 0) {
            return;
        }
        _slots[index].retainCount--;
        freeSlot(index);
    }

    void Context::retain(Handle handle) {
        std::lock_guard<std::mutex> lock(_mutex);
        retainLocked(handle);
    }

    void Context::release(Handle handle) {
        std::lock_guard<std::mutex> lock(_mutex);
        releaseLocked(handle);
    }

    void Context::setUiDispatcher(Dispatcher dispatcher, void* userData) {
        std::lock_guard<std::mutex> lock(_mutex);
        _dispatcher = dispatcher;
        _dispatcherUserData = userData;
    }

    Subscription Context::subscribe(Handle handle, const std::string& event, EventHandler handler,
                                    void* userData, bool consume, Delivery delivery, bool coalesce,
                                    const std::string& projection) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!resolve(handle)) {
            return NULL_SUBSCRIPTION;
        }
        if (!projection.empty() && !Projections::find(projection)) {
            // Refused here rather than silently ignored: a typo would otherwise show up as
            // coordinates that look plausible and are in the wrong system.
            Log::Errorf("Context::subscribe: unknown projection '%s'", projection.c_str());
            return NULL_SUBSCRIPTION;
        }
        if (consume && delivery != DELIVERY_ORIGIN) {
            // The SDK asks whether the event was consumed NOW; a queued handler answers later.
            // Rejected at registration rather than discovered as a race.
            Log::Error("Context::subscribe: a consuming handler must be DELIVERY_ORIGIN");
            return NULL_SUBSCRIPTION;
        }
        return _events.subscribe(handle, event, handler, userData, consume, delivery, coalesce,
                                 projection);
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

    void Context::postDrain() {
        // One post per batch: the drain empties the whole queue, so a second would find nothing.
        if (_drainPosted || !_dispatcher) {
            return;
        }
        _drainPosted = true;
        Dispatcher dispatcher = _dispatcher;
        void* userData = _dispatcherUserData;
        Context* self = this;
        dispatcher(userData, [](void* argument) {
            static_cast<Context*>(argument)->drainQueue();
        }, self);
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

        bool queued = false;
        for (Subscription subscription : subscriptions) {
            Dispatch dispatch;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (!_events.lookup(subscription, dispatch)) {
                    continue;
                }
                if (dispatch.delivery != DELIVERY_ORIGIN) {
                    if (!_dispatcher) {
                        if (!_warnedNoDispatcher) {
                            _warnedNoDispatcher = true;
                            Log::Warn("Context: no UI dispatcher set, delivering inline");
                        }
                    } else {
                        // Coalescing replaces the pending payload rather than adding a second, so
                        // a UI handler for a per-frame event cannot outrun the loop.
                        bool replaced = false;
                        if (dispatch.coalesce) {
                            for (Queued& pending : _queue) {
                                if (pending.subscription == subscription) {
                                    if (pending.payload != payload) {
                                        releaseLocked(pending.payload);
                                        pending.payload = payload;
                                        retainLocked(payload);
                                    }
                                    replaced = true;
                                    break;
                                }
                            }
                        }
                        if (!replaced) {
                            Queued pending;
                            pending.subscription = subscription;
                            pending.target = handle;
                            pending.event = event;
                            pending.payload = payload;
                            retainLocked(payload);
                            _queue.push_back(pending);
                        }
                        queued = true;
                        postDrain();
                        continue;
                    }
                }
            }
            ScopedProjection active(dispatch.projection);
            bool result = dispatch.handler(dispatch.userData, handle, event.c_str(), payload);
            if (dispatch.consume && result) {
                return true;
            }
        }
        (void)queued;
        return false;
    }

    int Context::drainQueue() {
        std::vector<Queued> batch;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            batch.swap(_queue);
            _drainPosted = false;
        }

        for (const Queued& pending : batch) {
            Dispatch dispatch;
            bool live;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                live = _events.lookup(pending.subscription, dispatch);
            }
            // Unsubscribed between the emit and the drain: the payload still has to be released.
            if (live) {
                ScopedProjection active(dispatch.projection);
                dispatch.handler(dispatch.userData, pending.target, pending.event.c_str(),
                                 pending.payload);
            }
            release(pending.payload);
        }
        return static_cast<int>(batch.size());
    }

    std::size_t Context::getQueuedCount() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.size();
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

    namespace {
        /**
         * Walks the rest of a path inside a Variant: object keys, and a numeric segment indexes
         * an array. This is what lets "properties.name" read one key without the caller having to
         * materialise and parse the whole bag.
         */
        bool readVariantPath(const Variant& root, const std::string& path, std::size_t start,
                             PropertyValue& value) {
            Variant current = root;
            while (start < path.size()) {
                std::size_t dot = path.find('.', start);
                std::string segment = path.substr(start, dot == std::string::npos ? dot : dot - start);
                start = dot == std::string::npos ? path.size() : dot + 1;

                if (current.getType() == VariantType::VARIANT_TYPE_ARRAY) {
                    char* end = nullptr;
                    long index = std::strtol(segment.c_str(), &end, 10);
                    if (*end != 0 || index < 0 || index >= current.getArraySize()) {
                        return false;
                    }
                    current = current.getArrayElement(static_cast<int>(index));
                } else if (current.getType() == VariantType::VARIANT_TYPE_OBJECT) {
                    if (!current.containsObjectKey(segment)) {
                        return false;
                    }
                    current = current.getObjectElement(segment);
                } else {
                    return false;
                }
            }

            switch (current.getType()) {
            case VariantType::VARIANT_TYPE_BOOL:
                value = PropertyValue::ofBool(current.getBool());
                return true;
            case VariantType::VARIANT_TYPE_INTEGER:
                value = PropertyValue::ofLong(current.getLong());
                return true;
            case VariantType::VARIANT_TYPE_DOUBLE:
                value = PropertyValue::ofDouble(current.getDouble());
                return true;
            case VariantType::VARIANT_TYPE_STRING:
                value = PropertyValue::ofString(current.getString());
                return true;
            default:
                // An object or an array reads as its JSON, so a caller can take a subtree whole.
                value = PropertyValue::ofString(current.toString());
                value.type = PT_VARIANT;
                return true;
            }
        }
    }

    std::shared_ptr<Projection> Context::sourceProjection(const ObjectRef& target,
                                                          Handle handle) const {
        const ClassEntry* classEntry = findClass(target.cppClass);
        if (const PropertyEntry* entry = findProjectionProperty(classEntry)) {
            ObjectRef projection;
            entry->objectGetter(target.obj.get(), projection);
            if (projection.obj) {
                return std::static_pointer_cast<Projection>(projection.obj);
            }
        }
        // Nothing declared: a click info is in map coordinates and only the map knows which.
        const Slot* slot = resolve(handle);
        return slot ? slot->projection : std::shared_ptr<Projection>();
    }

    Result Context::getProperty(Handle handle, const std::string& path, PropertyValue& value,
                                const std::string& projection) const {
        std::lock_guard<std::mutex> lock(_mutex);
        ObjectRef target;
        Result result = RESULT_OK;
        std::size_t variantRest = std::string::npos;
        const PropertyEntry* entry = lookup(handle, path, target, result, &variantRest);
        if (!entry) {
            return result;
        }
        if (!entry->getter) {
            return RESULT_UNSUPPORTED_TYPE;
        }
        entry->getter(target.obj.get(), value);

        // The per-read name wins over the one the running handler asked for; with neither, the
        // value stays in whatever projection its object uses.
        const std::string& wanted = projection.empty() ? tActiveProjection : projection;
        if ((entry->flags & PF_POSITION) && !wanted.empty()) {
            std::shared_ptr<Projection> to = Projections::find(wanted);
            std::shared_ptr<Projection> from = sourceProjection(target, handle);
            if (!to) {
                return RESULT_UNKNOWN_TYPE;
            }
            if (from && from->getName() != to->getName() &&
                !reproject(value.stringValue, *from, *to)) {
                return RESULT_UNSUPPORTED_TYPE;
            }
        }

        if (variantRest != std::string::npos) {
            Variant root;
            try {
                root = Variant::FromString(value.stringValue);
            } catch (const std::exception&) {
                return RESULT_BAD_SPEC;
            }
            if (!readVariantPath(root, path, variantRest, value)) {
                return RESULT_UNKNOWN_PROPERTY;
            }
        }
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
                                         ObjectRef& target, Result& result,
                                         std::size_t* variantRest) const {
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
            // A Variant is where object traversal stops and JSON traversal begins: the caller
            // reads the Variant, then walks what is left of the path inside it.
            if (entry->type == PT_VARIANT && variantRest) {
                *variantRest = dot + 1;
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
