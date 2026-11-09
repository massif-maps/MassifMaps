#ifndef _MASSIF_BYTEBUDGETCACHE_H_
#define _MASSIF_BYTEBUDGETCACHE_H_

#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace massif {

    /**
     * LRU cache of shared values bounded by the bytes they hold, that runs a load once when several threads
     * ask for the same key at the same time. A value bigger than the whole budget is still kept, alone.
     */
    template <typename V>
    class ByteBudgetCache {
    public:
        using Value = std::shared_ptr<V>;
        using Loader = std::function<Value()>;
        using Sizer = std::function<std::size_t(const V&)>;

        ByteBudgetCache(std::size_t maxBytes, Sizer sizer) : _maxBytes(maxBytes), _sizer(std::move(sizer)) { }

        // A null from the loader is handed to the waiting threads but never cached.
        Value get(long long key, const Loader& load) {
            std::promise<Value> promise;
            std::shared_future<Value> pending;
            std::uint64_t generation;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                for (std::size_t i = 0; i < _entries.size(); i++) {
                    if (_entries[i].first == key) {
                        Value value = _entries[i].second;
                        std::rotate(_entries.begin(), _entries.begin() + i, _entries.begin() + i + 1);
                        return value;
                    }
                }
                auto it = _inFlight.find(key);
                if (it != _inFlight.end()) {
                    pending = it->second;
                } else {
                    _inFlight.emplace(key, promise.get_future().share());
                    generation = _generation;
                }
            }
            if (pending.valid()) {
                return pending.get();
            }

            Value value;
            try {
                value = load();
            } catch (...) {
                finish(key, generation, Value(), promise);
                throw;
            }
            finish(key, generation, value, promise);
            return value;
        }

        void clear() {
            std::lock_guard<std::mutex> lock(_mutex);
            _entries.clear();
            _bytes = 0;
            _generation++;
        }

        std::size_t getBytes() const {
            std::lock_guard<std::mutex> lock(_mutex);
            return _bytes;
        }

        std::size_t getCount() const {
            std::lock_guard<std::mutex> lock(_mutex);
            return _entries.size();
        }

    private:
        void finish(long long key, std::uint64_t generation, const Value& value, std::promise<Value>& promise) {
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _inFlight.erase(key);
                if (value && generation == _generation) {
                    _entries.insert(_entries.begin(), std::make_pair(key, value));
                    _bytes += _sizer(*value);
                    while (_entries.size() > 1 && _bytes > _maxBytes) {
                        _bytes -= _sizer(*_entries.back().second);
                        _entries.pop_back();
                    }
                }
            }
            promise.set_value(value);
        }

        const std::size_t _maxBytes;
        const Sizer _sizer;
        mutable std::mutex _mutex;
        std::vector<std::pair<long long, Value> > _entries; // most recent first
        std::unordered_map<long long, std::shared_future<Value> > _inFlight;
        std::size_t _bytes = 0;
        std::uint64_t _generation = 0;
    };

}

#endif
