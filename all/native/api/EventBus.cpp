#include "api/EventBus.h"

#include <algorithm>

namespace massif { namespace api {

    EventBus::EventBus() {
        // Slot 0 is never handed out, so NULL_SUBSCRIPTION cannot collide with a real one.
        _entries.resize(1);
    }

    Subscription EventBus::subscribe(std::uint32_t target, const std::string& event,
                                     EventHandler handler, void* userData, bool consume,
                                     Delivery delivery, bool coalesce,
                                     const std::string& projection) {
        if (!handler) {
            return NULL_SUBSCRIPTION;
        }
        std::uint32_t index;
        if (!_freeSlots.empty()) {
            index = _freeSlots.back();
            _freeSlots.pop_back();
        } else {
            index = static_cast<std::uint32_t>(_entries.size());
            _entries.resize(_entries.size() + 1);
        }
        Entry& entry = _entries[index];
        entry.target = target;
        entry.event = event;
        entry.handler = handler;
        entry.userData = userData;
        entry.consume = consume;
        entry.delivery = delivery;
        entry.coalesce = coalesce;
        entry.projection = projection;
        entry.live = true;
        entry.sequence = _nextSequence++;
        return (entry.generation << INDEX_BITS) | index;
    }

    const EventBus::Entry* EventBus::resolve(Subscription subscription) const {
        std::uint32_t index = subscription & INDEX_MASK;
        if (subscription == NULL_SUBSCRIPTION || index >= _entries.size()) {
            return nullptr;
        }
        const Entry& entry = _entries[index];
        if (!entry.live || entry.generation != (subscription >> INDEX_BITS)) {
            return nullptr;
        }
        return &entry;
    }

    void EventBus::kill(std::uint32_t index) {
        Entry& entry = _entries[index];
        entry.live = false;
        entry.handler = nullptr;
        entry.userData = nullptr;
        entry.event.clear();
        entry.projection.clear();
        // Bumping the generation is what turns a stale subscription into an error rather than a
        // cancellation of whatever takes the slot next.
        entry.generation = entry.generation >= MAX_GENERATION ? 1 : entry.generation + 1;
        _freeSlots.push_back(index);
    }

    bool EventBus::unsubscribe(Subscription subscription) {
        std::uint32_t index = subscription & INDEX_MASK;
        if (!resolve(subscription)) {
            return false;
        }
        kill(index);
        return true;
    }

    int EventBus::unsubscribeEvent(std::uint32_t target, const std::string& event) {
        int removed = 0;
        for (std::uint32_t index = 1; index < _entries.size(); index++) {
            const Entry& entry = _entries[index];
            if (entry.live && entry.target == target && entry.event == event) {
                kill(index);
                removed++;
            }
        }
        return removed;
    }

    int EventBus::unsubscribeAll(std::uint32_t target) {
        int removed = 0;
        for (std::uint32_t index = 1; index < _entries.size(); index++) {
            if (_entries[index].live && _entries[index].target == target) {
                kill(index);
                removed++;
            }
        }
        return removed;
    }

    void EventBus::collect(std::uint32_t target, const std::string& event,
                           std::vector<Subscription>& out) const {
        std::vector<std::pair<std::uint64_t, Subscription> > matches;
        for (std::uint32_t index = 1; index < _entries.size(); index++) {
            const Entry& entry = _entries[index];
            if (entry.live && entry.target == target && entry.event == event) {
                matches.emplace_back(entry.sequence, (entry.generation << INDEX_BITS) | index);
            }
        }
        std::sort(matches.begin(), matches.end());
        for (const auto& match : matches) {
            out.push_back(match.second);
        }
    }

    bool EventBus::lookup(Subscription subscription, Dispatch& out) const {
        const Entry* entry = resolve(subscription);
        if (!entry) {
            return false;
        }
        out.handler = entry->handler;
        out.userData = entry->userData;
        out.consume = entry->consume;
        out.delivery = entry->delivery;
        out.coalesce = entry->coalesce;
        out.projection = entry->projection;
        return true;
    }

    bool EventBus::isSubscribed(Subscription subscription) const {
        return resolve(subscription) != nullptr;
    }

    std::size_t EventBus::getSubscriptionCount() const {
        std::size_t count = 0;
        for (const Entry& entry : _entries) {
            if (entry.live) {
                count++;
            }
        }
        return count;
    }

} }
