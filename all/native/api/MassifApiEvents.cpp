/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#include "api/MassifApi.h"
#include "components/DirectorPtr.h"
#include "components/Exceptions.h"

#include <cstdint>
#include <iterator>
#include <map>
#include <memory>

// Subscription half of MassifApi, split out so it links against Context only and runs in host tests.

namespace massif { namespace api {

    namespace {
        // DirectorPtr, not shared_ptr: a shared_ptr holds only the C++ half, the binding's half would be GC'd.
        std::map<int, DirectorPtr<EventListener> >& listeners() {
            static std::map<int, DirectorPtr<EventListener> > registry;
            return registry;
        }

        int dispatchToListener(void* userData, std::uint32_t target, const char* event,
                               std::uint32_t payload) {
            auto listener = static_cast<EventListener*>(userData);
            return listener->onEvent(static_cast<int>(target), event, static_cast<int>(payload)) ? 1 : 0;
        }

        // offEvent, offAll and target death drop subscriptions without naming them, so sweep against the context.
        void pruneListeners() {
            const std::shared_ptr<Context>& context = Context::GetDefault();
            std::map<int, DirectorPtr<EventListener> >& registry = listeners();
            for (auto it = registry.begin(); it != registry.end(); ) {
                it = context->isSubscribed(static_cast<Subscription>(it->first)) ? std::next(it)
                                                                                : registry.erase(it);
            }
        }
    }

    int MassifApi::on(int handle, const std::string& event,
                      const std::shared_ptr<EventListener>& listener, int delivery, bool coalesce,
                      const std::string& projection, bool consume, int throttleMs) {
        if (!listener) {
            throw NullArgumentException("Null listener");
        }
        Subscription subscription = Context::GetDefault()->subscribe(
            static_cast<Handle>(handle), event, &dispatchToListener, listener.get(), consume,
            static_cast<Delivery>(delivery), coalesce, projection, throttleMs);
        if (subscription != NULL_SUBSCRIPTION) {
            listeners()[static_cast<int>(subscription)] = DirectorPtr<EventListener>(listener);
        }
        // Sweeping on subscribe bounds the registry: an orphaned listener dies at the next subscription.
        pruneListeners();
        return static_cast<int>(subscription);
    }

    void MassifApi::setUiDispatcher(const std::shared_ptr<UiDispatcher>& dispatcher) {
        // Context keeps only a raw pointer; this is what stops the director being collected.
        static DirectorPtr<UiDispatcher> held;
        held = DirectorPtr<UiDispatcher>(dispatcher);
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
