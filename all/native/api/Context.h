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
     * An opaque reference to a registered object.
     *
     * 20 bits of slot index and 12 bits of generation, so a handle held past a destroy is
     * detected rather than silently addressing whatever took the slot. 1M live objects and 4096
     * reuses of a slot before the generation wraps; it fits a uint32_t, and therefore also a
     * JavaScript number, which is what the C and WASM bindings need.
     */
    typedef std::uint32_t Handle;

    static const Handle NULL_HANDLE = 0;

    enum Result {
        RESULT_OK = 0,
        RESULT_BAD_HANDLE,       // never registered, or freed and the generation moved on
        RESULT_UNKNOWN_CLASS,    // the object's class declares no properties
        RESULT_UNKNOWN_PROPERTY, // dropped with a warning by callers that tolerate it
        RESULT_READONLY,
        RESULT_UNSUPPORTED_TYPE, // STRUCT, and writing an OBJECT, until their accessors land
        RESULT_DUPLICATE_ID,
        RESULT_NOT_TRAVERSABLE,  // a dotted path crossed something that is not an OBJECT
        RESULT_NULL_OBJECT,      // an OBJECT property on the way was not set
        RESULT_BAD_SPEC,         // not a JSON object, or it does not parse
        RESULT_UNKNOWN_TYPE,     // no factory builds that "type"
        RESULT_UNKNOWN_METHOD,
        RESULT_FAILED            // the method ran and could not produce a result
    };

    /**
     * How a spec key that does not resolve is treated. Unknown keys are dropped with a warning,
     * so a spec written against another SDK version still applies what it can.
     */
    enum SpecTolerance {
        SPEC_TOLERANT = 0,
        SPEC_STRICT
    };

    /**
     * Owns the handle table and the per-kind id registries.
     *
     * There is one default context, which is what the static bindings use, but nothing is a raw
     * global: a second context is a second isolated world, which is what tests and a WASM module
     * instance want.
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
         * Returns the object behind a handle, or null when the handle is stale.
         */
        std::shared_ptr<void> getObject(Handle handle) const;

        /**
         * Registers an object under a generated id, for a result the caller owns.
         *
         * A call's result has no name an app would choose, but it still needs a handle - which is
         * how a binary blob crosses the boundary without being serialised. Free it with destroy.
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
         * The same, addressed by handle rather than by kind and id - which is what a caller
         * holding a call result has.
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
         * The projection an object's positions are in when its class does not say so itself.
         *
         * A click info carries map coordinates but has no projection of its own, so it inherits
         * one; a data source names its projection as a property and needs no help.
         */
        void setObjectProjection(Handle handle, const std::shared_ptr<Projection>& projection);

        /**
         * The projection an object's positions are in: the one its class declares, else the one
         * attached with setObjectProjection, else null.
         */
        std::shared_ptr<Projection> getObjectProjection(Handle handle) const;

        /**
         * Writes a property of a registered object. The underlying setter is called, so the
         * change reaches the renderer exactly as a direct call would.
         */
        Result setProperty(Handle handle, const std::string& path, const PropertyValue& value);

        /**
         * Runs a method on an object.
         *
         * The lock is NOT held while it runs: loadTile does network I/O, and a method that called
         * back into the context would otherwise deadlock.
         *
         * @param method The method name, e.g. "loadTile".
         * @param argsJson The arguments, as a JSON array. Empty for none.
         * @param result The return value. PT_OBJECT means intValue is a handle the CALLER OWNS
         *               and must destroy; anything else is the value itself.
         */
        Result call(Handle handle, const std::string& method, const std::string& argsJson,
                    PropertyValue& result);

        /**
         * The same, with the result always as a handle the CALLER OWNS.
         *
         * An object result is that object; anything else is registered as a Variant, so one rule
         * covers both and a binding does not need a result struct. Free it with destroy.
         */
        Result callHandle(Handle handle, const std::string& method, const std::string& argsJson,
                          Handle& result);

        /**
         * The same, on a worker thread, with the result delivered as an event on the object.
         *
         * The result arrives as the event's payload - an object result directly, anything else
         * wrapped in a Variant a path can be read out of. A payload of 0 means the call failed;
         * the reason is logged. Subscribers pick their own delivery thread as usual, so this adds
         * no second callback mechanism.
         *
         * Validation of the handle, the method name and the argument JSON happens here, before
         * anything is queued, so a mistake is reported to the caller rather than to a log.
         *
         * @param event The event name to emit the result on, e.g. "loadTile.done".
         */
        Result callAsync(Handle handle, const std::string& method, const std::string& argsJson,
                         const std::string& event);

        /**
         * Reads a binary property without turning it into a string.
         *
         * @param path The path to a BinaryData property, e.g. "data" on a tile. Empty when the
         *             handle is the blob itself, which is what an async result gives.
         */
        Result getData(Handle handle, const std::string& path,
                       std::shared_ptr<BinaryData>& value) const;

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
         * @return The subscription, or NULL_SUBSCRIPTION when the handle is stale.
         */
        Subscription subscribe(Handle handle, const std::string& event, EventHandler handler,
                               void* userData, bool consume, Delivery delivery = DELIVERY_ORIGIN,
                               bool coalesce = false,
                               const std::string& projection = std::string());

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
        // path runs into a Variant, variantRest is set to where the JSON part of it starts.
        const PropertyEntry* lookup(Handle handle, const std::string& path,
                                    ObjectRef& target, Result& result,
                                    std::size_t* variantRest = nullptr) const;

        // What coordinate system target's positions are in: what its class declares, else what
        // was attached to the handle the walk started from.
        std::shared_ptr<Projection> sourceProjection(const ObjectRef& target, Handle handle) const;

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
            Handle target = NULL_HANDLE;
            std::string method;
            std::string argsJson;
            std::string event;
        };

        // One worker, not a pool: the SDK's own pool needs a per-platform Task.cpp, and running
        // async calls in submission order is what an app expects anyway.
        void startWorker();
        void runCalls();

        std::deque<AsyncCall> _calls;
        std::thread _worker;
        std::condition_variable _callCondition;
        std::size_t _callsRunning = 0;
        bool _stopping = false;
        long long _resultCounter = 0;
    };

} }

#endif
