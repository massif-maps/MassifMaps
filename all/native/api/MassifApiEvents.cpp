/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#include "api/MassifApi.h"
#include "components/Exceptions.h"

#include <cstdint>
#include <iterator>
#include <map>
#include <memory>

// The subscription half of MassifApi, split out of MassifApi.cpp so it links against Context and
// nothing else - the rest of the class needs Options, Layers and every source constructor, which
// is what kept these functions out of the host tests while `on` silently dropped its consume flag.

namespace massif { namespace api {

    namespace {
        // The listener a subscription belongs to, kept alive for as long as the subscription is.
        std::map<int, std::shared_ptr<EventListener> >& listeners() {
            static std::map<int, std::shared_ptr<EventListener> > registry;
            return registry;
        }

        int dispatchToListener(void* userData, std::uint32_t target, const char* event,
                               std::uint32_t payload) {
            auto listener = static_cast<EventListener*>(userData);
            return listener->onEvent(static_cast<int>(target), event, static_cast<int>(payload)) ? 1 : 0;
        }

        /**
         * Drops the listeners whose subscription has gone.
         *
         * offEvent, offAll and the death of a target remove subscriptions without naming them, so
         * the registry cannot mirror those removals - it is swept against the context instead. Run
         * on every add and every remove, which is the only way the map grows, so an orphan lives
         * until the next call rather than for the process.
         */
        void pruneListeners() {
            const std::shared_ptr<Context>& context = Context::GetDefault();
            std::map<int, std::shared_ptr<EventListener> >& registry = listeners();
            for (auto it = registry.begin(); it != registry.end(); ) {
                it = context->isSubscribed(static_cast<Subscription>(it->first)) ? std::next(it)
                                                                                : registry.erase(it);
            }
        }
    }

    int MassifApi::on(int handle, const std::string& event,
                      const std::shared_ptr<EventListener>& listener, int delivery, bool coalesce,
                      const std::string& projection, bool consume) {
        if (!listener) {
            throw NullArgumentException("Null listener");
        }
        Subscription subscription = Context::GetDefault()->subscribe(
            static_cast<Handle>(handle), event, &dispatchToListener, listener.get(), consume,
            static_cast<Delivery>(delivery), coalesce, projection);
        if (subscription != NULL_SUBSCRIPTION) {
            listeners()[static_cast<int>(subscription)] = listener;
        }
        // Subscribing is the only thing that grows the registry, so sweeping here bounds it: a
        // listener orphaned by a destroy cannot outlive the next subscription.
        pruneListeners();
        return static_cast<int>(subscription);
    }

    void MassifApi::setUiDispatcher(const std::shared_ptr<UiDispatcher>& dispatcher) {
        // Held for as long as it is installed: Context keeps only a raw pointer, so nothing else
        // would stop a director being collected the moment this returns.
        static std::shared_ptr<UiDispatcher> held;
        held = dispatcher;
        if (!dispatcher) {
            Context::GetDefault()->setUiDispatcher(nullptr, nullptr);
            return;
        }
        Context::GetDefault()->setUiDispatcher(
            [](void* userData, void (*)(void*), void*) {
                static_cast<UiDispatcher*>(userData)->post();
            },
            dispatcher.get());
    }

    int MassifApi::drain() {
        return Context::GetDefault()->drainQueue();
    }

    bool MassifApi::off(int subscription) {
        bool removed = Context::GetDefault()->unsubscribe(static_cast<Subscription>(subscription));
        listeners().erase(subscription);
        pruneListeners();
        return removed;
    }

    int MassifApi::offEvent(int handle, const std::string& event) {
        int removed = Context::GetDefault()->unsubscribeEvent(static_cast<Handle>(handle), event);
        pruneListeners();
        return removed;
    }

    int MassifApi::offAll(int handle) {
        int removed = Context::GetDefault()->unsubscribeAll(static_cast<Handle>(handle));
        pruneListeners();
        return removed;
    }

} }
