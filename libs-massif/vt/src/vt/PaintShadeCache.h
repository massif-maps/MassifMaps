/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_PAINTSHADECACHE_H_
#define _MASSIF_VT_PAINTSHADECACHE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace massif::vt {

    /**
     * The bookkeeping of the terrain paint's shaded-DEM textures, one per elevation texture: which
     * picture a slot holds and when it must be shaded again. GL-free, the renderer owns the textures.
     * See docs/internals/rendering/07-hillshade-contours.md.
     */
    class PaintShadeCache {
    public:
        struct Entry {
            unsigned int texture = 0;       // the shaded picture, 0 until the first one
            int width = 0;
            int height = 0;
            std::uint64_t version = 0;      // the elevation content it was shaded from
            std::size_t fingerprint = 0;    // the paint appearance it was shaded with
            std::uint64_t used = 0;
        };

        explicit PaintShadeCache(std::size_t capacity) : _capacity(capacity) {
        }

        // current: the slot already holds what is asked; else shade into it and markShaded.
        // The texture of a slot pushed out for room is appended to released, for the caller to delete.
        Entry& acquire(unsigned int elevationTexture, std::uint64_t version, std::size_t fingerprint, std::vector<unsigned int>& released, bool& current) {
            auto it = _entries.find(elevationTexture);
            if (it == _entries.end()) {
                while (_entries.size() >= _capacity && evictOldest(released)) {
                }
                it = _entries.emplace(elevationTexture, Entry()).first;
            }
            Entry& entry = it->second;
            entry.used = ++_clock;
            current = entry.texture != 0 && entry.version == version && entry.fingerprint == fingerprint;
            return entry;
        }

        static void markShaded(Entry& entry, std::uint64_t version, std::size_t fingerprint) {
            entry.version = version;
            entry.fingerprint = fingerprint;
        }

        void clear(std::vector<unsigned int>& released) {
            for (const auto& item : _entries) {
                if (item.second.texture != 0) {
                    released.push_back(item.second.texture);
                }
            }
            _entries.clear();
        }

        // After a context loss the names are dead: nothing to delete.
        void forget() {
            _entries.clear();
        }

        std::size_t size() const {
            return _entries.size();
        }

    private:
        bool evictOldest(std::vector<unsigned int>& released) {
            auto oldest = _entries.end();
            for (auto it = _entries.begin(); it != _entries.end(); it++) {
                if (oldest == _entries.end() || it->second.used < oldest->second.used) {
                    oldest = it;
                }
            }
            if (oldest == _entries.end()) {
                return false;
            }
            if (oldest->second.texture != 0) {
                released.push_back(oldest->second.texture);
            }
            _entries.erase(oldest);
            return true;
        }

        const std::size_t _capacity;
        std::uint64_t _clock = 0;
        std::map<unsigned int, Entry> _entries;
    };

}

#endif
