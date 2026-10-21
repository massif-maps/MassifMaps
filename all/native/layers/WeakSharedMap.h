#ifndef _MASSIF_WEAKSHAREDMAP_H_
#define _MASSIF_WEAKSHAREDMAP_H_

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>

namespace massif {

    /**
     * One live object per key: while anyone still holds it, asking again for the key returns that object.
     */
    template <typename Key, typename Value>
    class WeakSharedMap {
    public:
        std::shared_ptr<Value> find(const Key& key) const {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _entries.find(key);
            return it == _entries.end() ? nullptr : it->second.lock();
        }

        // Returns the live object of the key if there is one (dropping the candidate), else adopts the candidate.
        std::shared_ptr<Value> share(const Key& key, const std::shared_ptr<Value>& candidate) {
            std::lock_guard<std::mutex> lock(_mutex);
            std::weak_ptr<Value>& entry = _entries[key];
            if (std::shared_ptr<Value> live = entry.lock()) {
                return live;
            }
            entry = candidate;
            if (_entries.size() > _pruneAt) {
                for (auto it = _entries.begin(); it != _entries.end();) {
                    it = it->second.expired() ? _entries.erase(it) : std::next(it);
                }
                _pruneAt = std::max(PRUNE_SIZE, _entries.size() * 2);
            }
            return candidate;
        }

    private:
        static constexpr std::size_t PRUNE_SIZE = 64;

        std::size_t _pruneAt = PRUNE_SIZE;
        mutable std::mutex _mutex;
        std::map<Key, std::weak_ptr<Value>> _entries;
    };

}

#endif
