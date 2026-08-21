/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_EVENTBUS_H_
#define _MASSIF_API_EVENTBUS_H_

#include <cstdint>
#include <string>
#include <vector>

namespace massif { namespace api {

    /**
     * A handle to one subscription. Same 20-bit index / 12-bit generation encoding as an object
     * handle, so calling off() twice is an error rather than cancelling whatever took the slot.
     */
    typedef std::uint32_t Subscription;

    static const Subscription NULL_SUBSCRIPTION = 0;

    /**
     * Which thread a handler runs on.
     *
     * A consuming subscription must be ORIGIN: the SDK asks whether the event was consumed now,
     * and a queued handler answers later. Waiting for it would block the producer on the UI
     * thread, which is a deadlock waiting to happen.
     */
    enum Delivery {
        DELIVERY_ORIGIN = 0,  // wherever the event was produced - the GL or a tile thread
        DELIVERY_UI,
        DELIVERY_BACKGROUND
    };

    /**
     * How an embedder gets a call onto its UI thread. Java and Obj-C register one automatically;
     * NativeScript and WASM supply their own so callbacks land on their loop.
     */
    typedef void (*Dispatcher)(void* userData, void (*function)(void*), void* argument);

    /**
     * Delivered on the thread the subscription asked for.
     *
     * int rather than bool because this typedef IS the C ABI's mm_handler - identical types, so
     * a C handler is passed straight through with no trampoline to keep alive.
     * @return Non-zero when the handler consumed the event, stopping it reaching later handlers.
     *         Only meaningful for a subscription that asked to consume.
     */
    typedef int (*EventHandler)(void* userData, std::uint32_t target, const char* event,
                                std::uint32_t payload);

    /**
     * Everything dispatch needs about one subscription, resolved just before the handler runs.
     */
    struct Dispatch {
        EventHandler handler = nullptr;
        void* userData = nullptr;
        bool consume = false;
        Delivery delivery = DELIVERY_ORIGIN;
        bool coalesce = false;
        // The projection positions are read in for the duration of this handler. Empty leaves
        // them in whatever the source object uses.
        std::string projection;
    };

    /**
     * Who is listening to what.
     *
     * Dispatch order is registration order, so which of two consuming handlers wins is decided by
     * the app rather than by a hash. Subscriptions are dropped when their target is, or the first
     * destroy on an object with a handler is a use-after-free.
     *
     * Not thread-safe on its own; Context holds the lock, and releases it around the handlers -
     * see collect and lookup for why that is two phases.
     */
    class EventBus {
    public:
        EventBus();

        /**
         * Adds a handler. Returns its subscription handle, or NULL_SUBSCRIPTION if the handler is
         * null.
         * @param consume Whether this handler's return value can stop the event.
         */
        Subscription subscribe(std::uint32_t target, const std::string& event, EventHandler handler,
                               void* userData, bool consume, Delivery delivery, bool coalesce,
                               const std::string& projection);

        /**
         * Removes one subscription.
         * @return True when it was live.
         */
        bool unsubscribe(Subscription subscription);

        /**
         * Removes every handler of one event on one target.
         * @return How many were removed.
         */
        int unsubscribeEvent(std::uint32_t target, const std::string& event);

        /**
         * Removes every handler on one target. Called when the target is destroyed.
         * @return How many were removed.
         */
        int unsubscribeAll(std::uint32_t target);

        /**
         * Collects the subscriptions matching a target and event, in registration order.
         *
         * Dispatch is two-phase on purpose: the handlers are app code and must not run under the
         * context lock, but the list cannot be walked unlocked either. The caller collects handles
         * under the lock, then resolves each one - see lookup - immediately before calling it, so
         * a handler removed earlier in the same pass is skipped rather than called.
         */
        void collect(std::uint32_t target, const std::string& event,
                     std::vector<Subscription>& out) const;

        /**
         * Resolves a subscription for dispatch. False when it has been removed since collect.
         */
        bool lookup(Subscription subscription, Dispatch& out) const;

        /**
         * The number of live subscriptions. For tests and leak checks.
         */
        std::size_t getSubscriptionCount() const;

    private:
        struct Entry {
            std::uint32_t target = 0;
            std::string event;
            EventHandler handler = nullptr;
            void* userData = nullptr;
            bool consume = false;
            Delivery delivery = DELIVERY_ORIGIN;
            // Replace a queued event rather than adding another, so a UI-thread handler for a
            // high-frequency event cannot flood the loop.
            bool coalesce = false;
            std::string projection;
            bool live = false;
            std::uint32_t generation = 1;
            // Registration order. Slots are reused, so index order is NOT registration order, and
            // dispatching by index would make which handler wins depend on allocation history.
            std::uint64_t sequence = 0;
        };

        static const int INDEX_BITS = 20;
        static const std::uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1;
        static const std::uint32_t MAX_GENERATION = (1u << (32 - INDEX_BITS)) - 1;

        const Entry* resolve(Subscription subscription) const;
        void kill(std::uint32_t index);

        std::vector<Entry> _entries;
        std::vector<std::uint32_t> _freeSlots;
        std::uint64_t _nextSequence = 1;
    };

} }

#endif
