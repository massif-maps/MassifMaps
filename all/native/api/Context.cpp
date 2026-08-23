#include "api/Context.h"
#include "api/Methods.h"
#include "api/Projections.h"
#include "api/StructCodec.h"
#include "core/BinaryData.h"
#include "core/Variant.h"
#include "projections/Projection.h"
#include "utils/Log.h"

#include <algorithm>

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

        void readVariantRoot(void* obj, PropertyValue& value) {
            value.type = PT_VARIANT;
            value.stringValue = static_cast<Variant*>(obj)->toString();
        }

        // The synthetic property a Variant handle resolves to: the document itself, read-only.
        const PropertyEntry VARIANT_ROOT = { "", PT_VARIANT, PF_READONLY, &readVariantRoot,
                                             nullptr, nullptr };

        /** A call result with no object of its own, as the Variant an async payload carries. */
        Variant toVariant(const PropertyValue& value) {
            switch (value.type) {
            case PT_BOOL:   return Variant(value.boolValue);
            case PT_FLOAT:  return Variant(value.floatValue);
            case PT_STRING: return Variant(value.stringValue);
            case PT_STRUCT:
            case PT_VARIANT:
                try {
                    return Variant::FromString(value.stringValue);
                } catch (const std::exception&) {
                    return Variant(value.stringValue);
                }
            default:        return Variant(value.intValue);
            }
        }

        bool finite(const MapPos& pos) {
            // Mercator sends the poles to infinity, and "inf" is not JSON. Refused rather than
            // handed over as a number that will not parse.
            return std::isfinite(pos.getX()) && std::isfinite(pos.getY()) && std::isfinite(pos.getZ());
        }

        /**
         * Rewrites an encoded MapPos or MapBounds into another projection.
         *
         * The two shapes are told apart by decoding: bounds are a pair of positions, and a
         * position never parses as one. Corner-wise is right for the axis-aligned projections
         * reachable by name here.
         */
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
        registerStaticClasses();
    }

    /*
     * A class whose properties are all static has no instance, and every verb here is addressed by
     * handle - so it gets one at construction, under kind "static" and its short name:
     *
     *   set(findObject("static", "Log"), "showDebug", true)
     *
     * Derived from the table rather than named here, so a new static class is covered without the
     * facade knowing about it. The sentinel exists only to be a non-null address; the generated
     * static thunks do not take an obj at all.
     */
    void Context::registerStaticClasses() {
        for (std::size_t index = 0; index < getClassCount(); index++) {
            const ClassEntry* entry = getClass(index);
            if (!entry || !entry->count) {
                continue;
            }
            bool allStatic = true;
            for (std::size_t property = 0; property < entry->count; property++) {
                allStatic = allStatic && (entry->props[property].flags & PF_STATIC);
            }
            // All of them, not some: a mixed class would hand an instance thunk the sentinel.
            if (!allStatic) {
                continue;
            }
            std::string name = entry->cppClass;
            std::size_t colons = name.rfind("::");
            if (colons != std::string::npos) {
                name = name.substr(colons + 2);
            }
            Handle handle = NULL_HANDLE;
            registerObject("static", name, std::make_shared<int>(0), entry->cppClass, handle);
        }
    }

    Context::~Context() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _stopping = true;
        }
        _callCondition.notify_all();
        for (std::thread& worker : _workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
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
        Slot& slot = _slots[handle & INDEX_MASK];
        slot.spec = spec;
        slot.kind = kind;
        slot.id = id;
        kindIds[id] = handle;
        return RESULT_OK;
    }

    Result Context::registerResult(const std::string& kind, const std::shared_ptr<void>& obj,
                                   const char* cppClass, Handle& handle) {
        std::lock_guard<std::mutex> lock(_mutex);
        // '#' cannot collide with an app's id: create refuses one, and nothing else makes them.
        std::string id = "#" + std::to_string(++_resultCounter);
        handle = allocate(obj, cppClass);
        Slot& slot = _slots[handle & INDEX_MASK];
        slot.kind = kind;
        slot.id = id;
        _ids[kind][id] = handle;
        return RESULT_OK;
    }

    bool Context::destroy(Handle handle) {
        std::string kind, id;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            const Slot* slot = resolve(handle);
            if (!slot) {
                return false;
            }
            kind = slot->kind;
            id = slot->id;
        }
        return unregisterObject(kind, id);
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

    std::shared_ptr<void> Context::getObject(Handle handle, const char* requiredClass) const {
        std::lock_guard<std::mutex> lock(_mutex);
        const Slot* slot = resolve(handle);
        if (!slot || !slot->cppClass || !isSubclassOf(slot->cppClass, requiredClass)) {
            return std::shared_ptr<void>();
        }
        return slot->obj;
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

        // Pending calls die with their target too, or a queued one keeps it alive - through the
        // retain it took - long after the app dropped it.
        cancelCallsLocked(idIt->second);

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
        slot.kind.clear();
        slot.id.clear();
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
            int result = dispatch.handler(dispatch.userData, handle, event.c_str(), payload);
            if (dispatch.consume && result != 0) {
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

    bool Context::isSubscribed(Subscription subscription) const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _events.isSubscribed(subscription);
    }

    Result Context::resolveTarget(Handle handle, const std::string& path, ObjectRef& out) const {
        const Slot* slot = resolve(handle);
        if (!slot) {
            return RESULT_BAD_HANDLE;
        }
        out.obj = slot->obj;
        out.cppClass = slot->cppClass;
        if (path.empty()) {
            return RESULT_OK;
        }

        ObjectRef owner;
        Result result = RESULT_OK;
        const PropertyEntry* entry = lookup(handle, path, owner, result);
        if (!entry) {
            return result;
        }
        if (entry->type != PT_OBJECT || !entry->objectGetter) {
            return RESULT_NOT_TRAVERSABLE;
        }
        entry->objectGetter(owner.obj.get(), out);
        return out.obj ? RESULT_OK : RESULT_NULL_OBJECT;
    }

    Result Context::call(Handle handle, const std::string& method, const std::string& argsJson,
                         PropertyValue& result) {
        // A method may be addressed through a path, the same way a property is:
        // "tileDecoder.setStyleParameter" on a layer. Everything before the last dot walks object
        // properties; without it an app would have to register every intermediate just to call one.
        std::size_t dot = method.rfind('.');
        std::string path = dot == std::string::npos ? std::string() : method.substr(0, dot);
        std::string name = dot == std::string::npos ? method : method.substr(dot + 1);

        std::shared_ptr<void> obj;
        const char* cppClass = nullptr;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            ObjectRef target;
            Result resolved = resolveTarget(handle, path, target);
            if (resolved != RESULT_OK) {
                return resolved;
            }
            obj = target.obj;
            cppClass = target.cppClass;
        }

        MethodInvoke invoke = Methods::findMethod(cppClass, name);
        if (!invoke) {
            return RESULT_UNKNOWN_METHOD;
        }
        CallArgs args;
        if (!CallArgs::parse(argsJson, args)) {
            return RESULT_BAD_SPEC;
        }
        // Unlocked: loadTile does network I/O, and a method reaching back into the context - to
        // register its result, which every object-returning one does - would deadlock.
        Result called;
        try {
            called = invoke(*this, obj.get(), args, result);
        } catch (const std::exception& ex) {
            Log::Errorf("Context::call: '%s' threw: %s", name.c_str(), ex.what());
            return RESULT_REJECTED;
        }
        // A result is expressed in whatever the object that produced it is: a search's features are
        // in its data source's projection. Carrying it over is what makes the result's positions
        // convertible without the caller knowing where they came from. Only for a method addressed
        // directly - an intermediate reached by a path has no handle to read a projection from.
        if (called == RESULT_OK && result.type == PT_OBJECT && path.empty()) {
            Handle produced = static_cast<Handle>(result.intValue);
            if (!getObjectProjection(produced)) {
                if (std::shared_ptr<Projection> projection = getObjectProjection(handle)) {
                    setObjectProjection(produced, projection);
                }
            }
        }
        return called;
    }

    Result Context::callHandle(Handle handle, const std::string& method,
                               const std::string& argsJson, Handle& result) {
        PropertyValue value;
        Result called = call(handle, method, argsJson, value);
        if (called != RESULT_OK) {
            return called;
        }
        if (value.type == PT_OBJECT) {
            result = static_cast<Handle>(value.intValue);
            return RESULT_OK;
        }
        // A scalar has no handle of its own, so it travels as a Variant a path reads out of -
        // the same shape a JSON result already has, and one rule instead of two.
        auto variant = std::make_shared<Variant>(toVariant(value));
        return registerResult("result", variant, "massif::Variant", result);
    }

    Result Context::callAsync(Handle handle, const std::string& method, const std::string& argsJson,
                              const std::string& event, Call* call) {
        if (call) {
            *call = NULL_CALL;
        }
        if (event.empty()) {
            return RESULT_BAD_SPEC;
        }
        CallArgs args;
        if (!CallArgs::parse(argsJson, args)) {
            return RESULT_BAD_SPEC;
        }

        AsyncCall pending;
        pending.target = handle;
        pending.method = method;
        pending.argsJson = argsJson;
        pending.event = event;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            const Slot* slot = resolve(handle);
            if (!slot) {
                return RESULT_BAD_HANDLE;
            }
            // Checked before queueing, so a typo is an error the caller sees rather than a line
            // in a log minutes later. The same path form as call.
            std::size_t dot = method.rfind('.');
            ObjectRef target;
            Result resolved = resolveTarget(handle,
                                            dot == std::string::npos ? std::string()
                                                                     : method.substr(0, dot),
                                            target);
            if (resolved != RESULT_OK) {
                return resolved;
            }
            if (!Methods::findMethod(target.cppClass,
                                     dot == std::string::npos ? method : method.substr(dot + 1))) {
                return RESULT_UNKNOWN_METHOD;
            }
            pending.id = ++_callCounter;
            if (call) {
                *call = pending.id;
            }
            // The target has to outlive the call, or the result has nowhere to be emitted.
            retainLocked(handle);
            _calls.push_back(pending);
            startWorkerIfNeeded();
        }
        // notify_all, not notify_one: waitForCalls blocks on the same condition, and waking it
        // instead of the worker would hang.
        _callCondition.notify_all();
        return RESULT_OK;
    }

    // Grown on demand: most apps make no async call at all, and one that makes them one at a time
    // never needs a second thread. The measure is DISTINCT targets, not calls - three loadTiles on
    // one source are serialised, so a second worker for them would only idle.
    void Context::startWorkerIfNeeded() {
        if (_stopping || _workers.size() >= MAX_WORKERS) {
            return;
        }
        std::vector<Handle> targets;
        for (const AsyncCall& call : _calls) {
            if (std::find(targets.begin(), targets.end(), call.target) == targets.end()) {
                targets.push_back(call.target);
            }
        }
        for (const RunningCall& running : _running) {
            if (std::find(targets.begin(), targets.end(), running.target) == targets.end()) {
                targets.push_back(running.target);
            }
        }
        if (_workers.size() < targets.size()) {
            _workers.push_back(std::thread([this]() { runCalls(); }));
        }
    }

    std::deque<Context::AsyncCall>::iterator Context::claimableCall() {
        for (auto it = _calls.begin(); it != _calls.end(); ++it) {
            bool busy = false;
            for (const RunningCall& running : _running) {
                busy = busy || running.target == it->target;
            }
            if (!busy) {
                return it;
            }
        }
        return _calls.end();
    }

    void Context::runCalls() {
        while (true) {
            AsyncCall pending;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                // Not "a call is queued": one whose target is already busy has to keep waiting, or
                // the per-target order this exists to preserve is lost.
                _callCondition.wait(lock, [this]() {
                    return _stopping || claimableCall() != _calls.end();
                });
                if (_stopping && _calls.empty()) {
                    return;
                }
                auto claimed = claimableCall();
                if (claimed == _calls.end()) {
                    continue;   // stopping, with work left that belongs to another worker
                }
                pending = *claimed;
                _calls.erase(claimed);
                RunningCall running;
                running.id = pending.id;
                running.target = pending.target;
                _running.push_back(running);
            }

            Handle payload = NULL_HANDLE;
            Result result = callHandle(pending.target, pending.method, pending.argsJson, payload);
            if (result != RESULT_OK) {
                Log::Errorf("Context::callAsync: '%s' failed with %d", pending.method.c_str(),
                            static_cast<int>(result));
                payload = NULL_HANDLE;
            }

            bool cancelled = false;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                for (auto it = _running.begin(); it != _running.end(); ++it) {
                    if (it->id == pending.id) {
                        cancelled = it->cancelled;
                        _running.erase(it);
                        break;
                    }
                }
            }
            // Cancelled while it ran: the work could not be stopped, but the result is dropped
            // rather than delivered to a caller that has moved on.
            if (!cancelled) {
                emit(pending.target, pending.event, payload);
            }
            // The payload was the call's result and nobody else owns it; a queued handler holds
            // its own retain through the emit, so this does not free it early.
            if (payload != NULL_HANDLE) {
                destroy(payload);
            }
            release(pending.target);
            _callCondition.notify_all();
        }
    }

    bool Context::cancelCall(Call call) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (auto it = _calls.begin(); it != _calls.end(); ++it) {
                if (it->id != call) {
                    continue;
                }
                // Never started, so the retain it took on the target goes back here.
                releaseLocked(it->target);
                _calls.erase(it);
                _callCondition.notify_all();
                return true;
            }
            if (call == NULL_CALL) {
                return false;
            }
            for (RunningCall& running : _running) {
                if (running.id == call && !running.cancelled) {
                    running.cancelled = true;
                    return true;
                }
            }
            return false;
        }
    }

    int Context::cancelCalls(Handle handle) {
        std::lock_guard<std::mutex> lock(_mutex);
        return cancelCallsLocked(handle);
    }

    int Context::cancelCallsLocked(Handle handle) {
        int cancelled = 0;
        for (auto it = _calls.begin(); it != _calls.end(); ) {
            if (it->target == handle) {
                releaseLocked(it->target);
                it = _calls.erase(it);
                cancelled++;
            } else {
                ++it;
            }
        }
        // A running one, if it is this object's, keeps running but delivers nothing. Only one can
        // be, since calls on a target are serialised, but the loop costs nothing and says so.
        for (RunningCall& running : _running) {
            if (running.target == handle && !running.cancelled) {
                running.cancelled = true;
                cancelled++;
            }
        }
        if (cancelled) {
            _callCondition.notify_all();
        }
        return cancelled;
    }

    std::size_t Context::getPendingCallCount() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _calls.size() + _running.size();
    }

    void Context::waitForCalls() {
        std::unique_lock<std::mutex> lock(_mutex);
        _callCondition.wait(lock, [this]() {
            return _calls.empty() && _running.empty();
        });
    }

    Result Context::getData(Handle handle, const std::string& path,
                            std::shared_ptr<BinaryData>& value) const {
        std::lock_guard<std::mutex> lock(_mutex);
        const Slot* slot = resolve(handle);
        if (!slot) {
            return RESULT_BAD_HANDLE;
        }
        if (path.empty()) {
            if (!slot->cppClass || std::string(slot->cppClass) != "massif::BinaryData") {
                return RESULT_UNSUPPORTED_TYPE;
            }
            value = std::static_pointer_cast<BinaryData>(slot->obj);
            return RESULT_OK;
        }

        ObjectRef target;
        Result result = RESULT_OK;
        const PropertyEntry* entry = lookup(handle, path, target, result);
        if (!entry) {
            return result;
        }
        if (!entry->objectGetter) {
            return RESULT_UNSUPPORTED_TYPE;
        }
        ObjectRef blob;
        entry->objectGetter(target.obj.get(), blob);
        if (!blob.obj) {
            return RESULT_NULL_OBJECT;
        }
        if (!blob.cppClass || std::string(blob.cppClass) != "massif::BinaryData") {
            return RESULT_UNSUPPORTED_TYPE;
        }
        value = std::static_pointer_cast<BinaryData>(blob.obj);
        return RESULT_OK;
    }

    const char* const Context::DOUBLE_VECTOR_CLASS = "std::vector<double>";

    Result Context::getDoubles(Handle handle, std::vector<double>& value) const {
        std::lock_guard<std::mutex> lock(_mutex);
        const Slot* slot = resolve(handle);
        if (!slot) {
            return RESULT_BAD_HANDLE;
        }
        if (!slot->cppClass || std::string(slot->cppClass) != DOUBLE_VECTOR_CLASS) {
            return RESULT_UNSUPPORTED_TYPE;
        }
        value = *std::static_pointer_cast<std::vector<double> >(slot->obj);
        return RESULT_OK;
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
        // Guarded like every other call into the SDK: an accessor is free to validate, and an
        // exception crossing a binding boundary kills the process.
        try {
            entry->getter(target.obj.get(), value);
        } catch (const std::exception& ex) {
            Log::Errorf("Context::getProperty: '%s' threw: %s", path.c_str(), ex.what());
            return RESULT_REJECTED;
        }

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
        // therefore the redraw granularity are exactly those of a direct call - INCLUDING its
        // validation. Options::setZoomRange and friends throw on a value they will not take, and
        // an exception crossing into Java or Objective-C kills the process.
        try {
            entry->setter(target.obj.get(), value);
        } catch (const std::exception& ex) {
            Log::Errorf("Context::setProperty: '%s' rejected: %s", path.c_str(), ex.what());
            return RESULT_REJECTED;
        }
        return RESULT_OK;
    }

    Result Context::setObjectProperty(Handle handle, const std::string& path, Handle value) {
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
        if (entry->type != PT_OBJECT || !entry->objectSetter) {
            return RESULT_UNSUPPORTED_TYPE;
        }

        ObjectRef assigned;
        if (value != NULL_HANDLE) {
            const Slot* slot = resolve(value);
            if (!slot) {
                return RESULT_BAD_HANDLE;
            }
            // Checked BEFORE the cast, which is from shared_ptr<void> and would otherwise be
            // undefined for the wrong class.
            if (!isSubclassOf(slot->cppClass, entry->objectClass)) {
                return RESULT_UNKNOWN_CLASS;
            }
            assigned.obj = slot->obj;
            assigned.cppClass = slot->cppClass;
        }
        // Options::setBaseProjection throws on null, and it is not the only setter that validates.
        try {
            entry->objectSetter(target.obj.get(), assigned);
        } catch (const std::exception& ex) {
            Log::Errorf("Context::setObjectProperty: '%s' rejected: %s", path.c_str(), ex.what());
            return RESULT_REJECTED;
        }
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

        // A Variant handle IS a JSON document, so the whole path - including an empty one, meaning
        // the document itself - is read inside it. This is what makes an async call's scalar
        // result readable without inventing a result class for it.
        if (slot->cppClass && std::string(slot->cppClass) == "massif::Variant" && variantRest) {
            *variantRest = 0;
            return &VARIANT_ROOT;
        }

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
            // reads the Variant, then walks what is left of the path inside it. A STRUCT carries
            // JSON too, so clickInfo.clickType and bounds.0 walk the same way.
            if ((entry->type == PT_VARIANT || entry->type == PT_STRUCT) && variantRest) {
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
