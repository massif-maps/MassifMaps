/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_EVENTBUS_H_
#define _MASSIF_API_EVENTBUS_H_

#include <chrono>
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
     * Which thread a handler runs on. A consuming subscription must be ORIGIN: the SDK needs the
     * answer now, and waiting on a queued handler would block the producer on the UI thread.
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
     * Delivered on the thread the subscription asked for. int, not bool: this is the C ABI's
     * mm_handler, so a C handler passes straight through with no trampoline.
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
     * Who is listening to what. Dispatch follows registration order; subscriptions must be dropped
     * with their target, or its destroy is a use-after-free. Not thread-safe: Context holds the lock
     * and releases it around the handlers (see collect).
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
                               const std::string& projection, int throttleMs);

        /**
         * Whether a throttled subscription is due, and marks it delivered when it is. It writes the
         * entry, so it cannot work on a Dispatch copy; call it under the same lock as lookup.
         */
        bool due(Subscription subscription, std::chrono::steady_clock::time_point now);

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
         * Collects the subscriptions matching a target and event, in registration order. Handlers
         * must not run under the context lock, so the caller collects under it and resolves each one
         * with lookup just before calling it: a handler removed earlier in the pass is skipped.
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

        /**
         * Whether a subscription is still live. Lets a binding that pins something per subscription
         * find the orphans of bulk removals and target deaths.
         */
        bool isSubscribed(Subscription subscription) const;

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
            // 0 is off. Dropping is the point - a queued handler would read a payload the emit
            // has already freed - so this is a window, not a timer.
            int throttleMs = 0;
            std::chrono::steady_clock::time_point lastDelivery;
            std::string projection;
            bool live = false;
            std::uint32_t generation = 1;
            // Registration order: slots are reused, so index order is not.
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
