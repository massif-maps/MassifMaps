/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_CONTEXT_H_
#define _MASSIF_API_CONTEXT_H_

#include "api/EventBus.h"
#include "api/PropertyTable.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace massif {
    class BinaryData;
    class Projection;

    namespace api {

    /**
     * An opaque reference to a registered object: 20 bits of slot index, 12 of generation, so a
     * handle held past a destroy is detected rather than addressing whatever took the slot. Fits a
     * uint32_t, and therefore a JavaScript number.
     */
    typedef std::uint32_t Handle;

    static const Handle NULL_HANDLE = 0;

    /**
     * A queued or running async call, so it can be cancelled. A plain counter: ids are never
     * reused, so a finished call is simply not found.
     */
    typedef std::uint32_t Call;

    static const Call NULL_CALL = 0;

    enum Result {
        RESULT_OK = 0,
        RESULT_BAD_HANDLE,       // never registered, or freed and the generation moved on
        RESULT_UNKNOWN_CLASS,    // the object's class declares no properties
        RESULT_UNKNOWN_PROPERTY, // dropped with a warning by callers that tolerate it
        RESULT_READONLY,
        RESULT_UNSUPPORTED_TYPE, // a type with no accessor - a STRUCT outside CODEC_TYPES
        RESULT_DUPLICATE_ID,
        RESULT_NOT_TRAVERSABLE,  // a dotted path crossed something that is not an OBJECT
        RESULT_NULL_OBJECT,      // an OBJECT property on the way was not set
        RESULT_BAD_SPEC,         // not a JSON object, or it does not parse
        RESULT_UNKNOWN_TYPE,     // no factory builds that "type"
        RESULT_UNKNOWN_METHOD,
        RESULT_FAILED,           // the method ran and could not produce a result
        RESULT_REJECTED          // the SDK's own setter refused the value, e.g. a null it needs
    };

    /**
     * The enum name, for a message a human reads.
     * @return The name, or "RESULT_?" for a value outside the enum.
     */
    const char* resultName(Result result);

    /**
     * How a spec key that does not resolve is treated. Unknown keys are dropped with a warning,
     * so a spec written against another SDK version still applies what it can.
     */
    enum SpecTolerance {
        SPEC_TOLERANT = 0,
        SPEC_STRICT
    };

    /**
     * Owns the handle table and the per-kind id registries. The static bindings use the default
     * one; a second context is a second isolated world (tests, a WASM module instance).
     */
    class Context {
    public:
        Context();
        virtual ~Context();

        /**
         * The context the static bindings address. Created on first use.
         */
        static const std::shared_ptr<Context>& GetDefault();

        /**
         * Registers an object under a kind and id.
         * @param kind The namespace, e.g. "source". Ids only collide within a kind.
         * @param id The caller's name for the object.
         * @param obj The object. The context keeps it alive.
         * @param cppClass Its fully qualified C++ name, e.g. "massif::FogOptions".
         * @param handle Set to the new handle on success.
         * @param spec The canonical spec it was built from, when it was built from one, so an
         *             identical create can be recognised as a reuse. Empty when adopted.
         * @return RESULT_OK, or RESULT_DUPLICATE_ID when the id is taken.
         */
        Result registerObject(const std::string& kind, const std::string& id,
                              const std::shared_ptr<void>& obj, const char* cppClass,
                              Handle& handle, const std::string& spec = std::string());

        /**
         * Returns the handle registered under a kind and id, or NULL_HANDLE.
         */
        Handle findObject(const std::string& kind, const std::string& id) const;

        /**
         * The handle of an object the context already holds, found by its address - for a method
         * thunk that emits. A linear scan of the slots: keep it off hot paths.
         * @return NULL_HANDLE when the object is not registered.
         */
        Handle handleOf(const void* obj) const;

        /**
         * Returns the object behind a handle, or null when the handle is stale.
         */
        std::shared_ptr<void> getObject(Handle handle) const;

        /**
         * The same, refused unless the object is of the required class or one of its subclasses,
         * so a method handed the wrong handle does not cast it and read another class' memory.
         * @return The object, or null when the handle is stale or of the wrong class.
         */
        std::shared_ptr<void> getObject(Handle handle, const char* requiredClass) const;

        /**
         * Registers an object under a generated id, for a result the caller owns. Free it with
         * destroy.
         */
        Result registerResult(const std::string& kind, const std::shared_ptr<void>& obj,
                              const char* cppClass, Handle& handle);

        /**
         * Drops the id and, with it, the context's reference to the object. Handles held
         * elsewhere become stale rather than dangling.
         * @return True when the id existed.
         */
        bool unregisterObject(const std::string& kind, const std::string& id);

        /**
         * The same, addressed by handle rather than by kind and id.
         * @return True when the handle was live.
         */
        bool destroy(Handle handle);

        /**
         * Reads a property of a registered object.
         * @param projection The well-known name of the projection to return positions in, e.g.
         *                   "EPSG:4326". Empty falls back to the projection the subscription being
         *                   dispatched asked for, and then to leaving the source projection alone.
         *                   Only PF_POSITION properties are affected.
         */
        Result getProperty(Handle handle, const std::string& path, PropertyValue& value,
                           const std::string& projection = std::string()) const;

        /**
         * The projection an object's positions are in when its class does not say so itself,
         * e.g. a click info, which carries map coordinates but no projection.
         */
        void setObjectProjection(Handle handle, const std::shared_ptr<Projection>& projection);

        /**
         * The projection an object's positions are in: the one its class declares, else the one
         * attached with setObjectProjection, else null.
         */
        std::shared_ptr<Projection> getObjectProjection(Handle handle) const;

        /**
         * Writes a property of a registered object through its setter. The path may end in a bag
         * key ("params.water_color"); a bag with no whole-value setter takes a JSON object.
         * @param projection The well-known name of the projection a position value is in. Empty
         *                   falls back to the running handler's, then to WGS84, as a read does.
         */
        Result setProperty(Handle handle, const std::string& path, const PropertyValue& value,
                           const std::string& projection = std::string());

        /**
         * Writes several properties from one JSON object of path to value, in one crossing. Every
         * key is attempted and the first failure is returned.
         * @param projection Applies to every position among them, as in setProperty.
         */
        Result setProperties(Handle handle, const std::string& json,
                             const std::string& projection = std::string());

        /**
         * Points an object property at another registered object - a layer's data source, a
         * decoder's style, a cache's inner source. The value's class is checked before the thunk
         * casts its type-erased pointer; an unknown class fails closed.
         * @param value The object to point at, or NULL_HANDLE to clear the property.
         * @return RESULT_UNSUPPORTED_TYPE when the property is not an object or has no setter,
         *         RESULT_BAD_HANDLE when the value is stale, RESULT_UNKNOWN_CLASS when it is the
         *         wrong kind of object.
         */
        Result setObjectProperty(Handle handle, const std::string& path, Handle value);

        /**
         * The object an object property points at, as a handle the caller owns - how an app shares
         * a child (e.g. a layer's data source) rather than building it twice.
         * @return NULL_HANDLE when the path does not resolve, is not an object property, or is null.
         */
        Handle getObjectProperty(Handle handle, const std::string& path);

        /**
         * Runs a method on an object. The lock is not held while it runs: loadTile does network
         * I/O, and a method calling back into the context would deadlock.
         * @param method The method name, optionally preceded by a path to the object it belongs
         *               to: "loadTile" on a source, "tileDecoder.setStyleParameter" on a layer.
         * @param argsJson The arguments, as a JSON array. Empty for none.
         * @param result The return value. PT_OBJECT means intValue is a handle the caller owns
         *               and must destroy; anything else is the value itself.
         */
        Result call(Handle handle, const std::string& method, const std::string& argsJson,
                    PropertyValue& result);

        /**
         * The same, with the result always as a handle the caller owns: an object result is that
         * object, anything else a registered Variant. Free it with destroy.
         */
        Result callHandle(Handle handle, const std::string& method, const std::string& argsJson,
                          Handle& result);

        /**
         * The same, on a worker thread, with the result as the payload of an event on the object
         * (0 when the call failed; the reason is logged). The handle, method and argument JSON
         * are validated before anything is queued.
         * @param event The event name to emit the result on, e.g. "loadTile.done".
         * @param call Set to the call's id, for cancelCall. Optional.
         */
        Result callAsync(Handle handle, const std::string& method, const std::string& argsJson,
                         const std::string& event, Call* call = nullptr);

        /**
         * Cancels a queued or running async call: it will not start and no event fires, but a call
         * in flight is not aborted - it finishes and its result is dropped.
         * @return True when the call was queued or running. False when it had already finished.
         */
        bool cancelCall(Call call);

        /**
         * Cancels every queued or running call on an object. Called when it is destroyed, the
         * same way its subscriptions are.
         * @return How many were cancelled.
         */
        int cancelCalls(Handle handle);

        /**
         * Reads a binary property without turning it into a string.
         * @param path The path to a BinaryData property, e.g. "data" on a tile. Empty when the
         *             handle is the blob itself, which is what an async result gives.
         */
        Result getData(Handle handle, const std::string& path,
                       std::shared_ptr<BinaryData>& value) const;

        /**
         * Reads a bulk numeric result, as a method returned it, as a flat array.
         */
        Result getDoubles(Handle handle, std::vector<double>& value) const;

        /**
         * The C++ class name a bulk numeric result is registered under. Not in the property
         * table: it is a container, not a class with properties.
         */
        static const char* const DOUBLE_VECTOR_CLASS;

        /**
         * How many async calls are queued or running. For tests.
         */
        std::size_t getPendingCallCount() const;

        /**
         * Blocks until every queued async call has finished. For tests, and for a shutdown that
         * wants its results.
         */
        void waitForCalls();

        /**
         * The number of live handles. For tests and leak checks.
         */
        std::size_t getObjectCount() const;

        /**
         * Adds an event handler to an object.
         * @param consume Whether the handler's return value can stop the event reaching later
         *                handlers. A consuming handler has to answer synchronously.
         * @param projection The projection this handler's position reads default to. It applies
         *                   for the duration of the call only, so a payload kept and read later
         *                   has to name the projection per read.
         * @param throttleMs Drops events arriving within this many ms of the last one delivered
         *                   to this handler; 0 is off. Refused on a consuming subscription.
         * @return The subscription, or NULL_SUBSCRIPTION when the handle is stale.
         */
        Subscription subscribe(Handle handle, const std::string& event, EventHandler handler,
                               void* userData, bool consume, Delivery delivery = DELIVERY_ORIGIN,
                               bool coalesce = false,
                               const std::string& projection = std::string(), int throttleMs = 0);

        /**
         * Registers how to reach the UI thread. Without one, DELIVERY_UI falls back to running
         * inline and says so once, rather than dropping the event.
         */
        void setUiDispatcher(Dispatcher dispatcher, void* userData);

        /**
         * Holds an object alive independently of its id, so a queued event's payload survives
         * until its handler has run. The handle stays valid until the matching release.
         */
        void retain(Handle handle);

        /**
         * Drops a retain. The slot is freed when the last one goes and the id is gone.
         */
        void release(Handle handle);

        /**
         * Runs the handlers queued for another thread. Called by the dispatcher; a test calls it
         * directly.
         * @return How many were delivered.
         */
        int drainQueue();

        /**
         * How many events are waiting for another thread. For tests.
         */
        std::size_t getQueuedCount() const;

        /**
         * Removes one subscription.
         */
        bool unsubscribe(Subscription subscription);

        /**
         * Removes every handler of one event on one object.
         */
        int unsubscribeEvent(Handle handle, const std::string& event);

        /**
         * Removes every handler on one object.
         */
        int unsubscribeAll(Handle handle);

        /**
         * Delivers an event to an object's handlers, in registration order.
         * @return True when a consuming handler stopped it.
         */
        bool emit(Handle handle, const std::string& event, Handle payload);

        /**
         * The number of live subscriptions. For tests and leak checks.
         */
        std::size_t getSubscriptionCount() const;

        /**
         * Whether a subscription is still live. Lets a binding that pins a listener per
         * subscription find the orphans of bulk removals and target deaths.
         */
        bool isSubscribed(Subscription subscription) const;

    private:
        struct Slot {
            std::shared_ptr<void> obj;
            const char* cppClass = nullptr;
            std::uint32_t generation = 1;
            bool used = false;
            // Non-zero while a queued event still needs this object. The slot is not recycled
            // until the id is gone AND the last retain is released.
            int retainCount = 0;
            bool idDropped = false;
            // The spec this was built from, canonicalised, so an identical create can be
            // recognised as a reuse rather than a conflict.
            std::string spec;
            // Only for an object whose class does not declare a projection of its own.
            std::shared_ptr<Projection> projection;
            // Where the id lives, so a handle can be destroyed without the caller knowing it.
            std::string kind;
            std::string id;
        };

        static const int INDEX_BITS = 20;
        static const std::uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1;
        static const std::uint32_t MAX_GENERATION = (1u << (32 - INDEX_BITS)) - 1;

        Handle allocate(const std::shared_ptr<void>& obj, const char* cppClass);
        const Slot* resolve(Handle handle) const;
        Handle findObjectLocked(const std::string& kind, const std::string& id) const;

    public:
        /**
         * The canonical spec an object was built from, or empty. Used by Spec::create to tell a
         * reuse from a conflict.
         */
        std::string getObjectSpec(Handle handle) const;

    private:
        // Walks a dotted path, leaving the object owning the final segment in target. When the
        // path runs into a Variant, variantRest is set to where the JSON part of it starts; when
        // it runs into a bag property, indexKey is set to the rest of the path.
        const PropertyEntry* lookup(Handle handle, const std::string& path,
                                    ObjectRef& target, Result& result,
                                    std::size_t* variantRest = nullptr,
                                    std::string* indexKey = nullptr) const;

        // What coordinate system target's positions are in: what its class declares, else what
        // was attached to the handle the walk started from.
        std::shared_ptr<Projection> sourceProjection(const ObjectRef& target, Handle handle) const;

        // The object a method belongs to: the handle itself, or what a dotted prefix walks to.
        Result resolveTarget(Handle handle, const std::string& path, ObjectRef& out) const;

        /** Gives every all-static class a handle, so a class with no instance is addressable. */
        void registerStaticClasses();

        mutable std::mutex _mutex;
        std::vector<Slot> _slots;
        std::vector<std::uint32_t> _freeSlots;
        std::unordered_map<std::string, std::unordered_map<std::string, Handle> > _ids;
        EventBus _events;

        struct Queued {
            Subscription subscription = NULL_SUBSCRIPTION;
            Handle target = NULL_HANDLE;
            std::string event;
            Handle payload = NULL_HANDLE;
        };

        void freeSlot(std::uint32_t index);
        void retainLocked(Handle handle);
        void releaseLocked(Handle handle);
        void postDrain();

        std::vector<Queued> _queue;
        Dispatcher _dispatcher = nullptr;
        void* _dispatcherUserData = nullptr;
        bool _drainPosted = false;
        bool _warnedNoDispatcher = false;

        struct AsyncCall {
            Call id = NULL_CALL;
            Handle target = NULL_HANDLE;
            std::string method;
            std::string argsJson;
            std::string event;
        };

        struct RunningCall {
            Call id = NULL_CALL;
            Handle target = NULL_HANDLE;
            bool cancelled = false;
        };

        /**
         * Calls on one object run in order; calls on different objects run in parallel. The event
         * carries the result, not the call id, so per-target order is the only way a caller can
         * tell same-object results apart.
         */
        void startWorkerIfNeeded();
        void runCalls();
        int cancelCallsLocked(Handle handle);
        /** The first queued call whose target is idle, or _calls.end(). */
        std::deque<AsyncCall>::iterator claimableCall();

        // An app's concurrent async work: a search, a route, a tile prime and a profile.
        static const std::size_t MAX_WORKERS = 4;

        std::deque<AsyncCall> _calls;
        std::vector<std::thread> _workers;
        std::condition_variable _callCondition;
        std::vector<RunningCall> _running;
        Call _callCounter = 0;
        bool _stopping = false;
        long long _resultCounter = 0;
    };

} }

#endif
