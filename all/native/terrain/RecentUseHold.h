/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_RECENTUSEHOLD_H_
#define _MASSIF_RECENTUSEHOLD_H_

#include <chrono>
#include <iterator>
#include <unordered_map>

namespace massif {

    // Long enough for the drapes, which resolve a different subset of the view each frame.
    constexpr std::chrono::steady_clock::duration RECENT_USE_HOLD_TIME = std::chrono::seconds(1);

    inline bool isRecentlyUsed(std::chrono::steady_clock::time_point lastUsed, std::chrono::steady_clock::time_point now) {
        return now - lastUsed < RECENT_USE_HOLD_TIME;
    }

    /**
     * What was used in the last RECENT_USE_HOLD_TIME, kept whatever a size-bounded LRU beside it drops: a view needing
     * more than the LRU holds otherwise evicts and reloads its own content forever (04-terrain.md, cache holds).
     */
    template <typename Key, typename Value>
    class RecentUseHold {
    public:
        using Clock = std::chrono::steady_clock;

        void use(const Key& key, const Value& value, Clock::time_point now) {
            Entry& entry = _entries[key];
            entry.value = value;
            entry.lastUsed = now;
        }

        bool find(const Key& key, Clock::time_point now, Value& value) const {
            auto it = _entries.find(key);
            if (it == _entries.end() || !isRecentlyUsed(it->second.lastUsed, now)) {
                return false;
            }
            value = it->second.value;
            return true;
        }

        void expire(Clock::time_point now) {
            for (auto it = _entries.begin(); it != _entries.end(); ) {
                it = (isRecentlyUsed(it->second.lastUsed, now) ? std::next(it) : _entries.erase(it));
            }
        }

        void clear() {
            _entries.clear();
        }

        std::size_t size() const {
            return _entries.size();
        }

    private:
        struct Entry {
            Value value;
            Clock::time_point lastUsed;
        };

        std::unordered_map<Key, Entry> _entries;
    };

}

#endif
