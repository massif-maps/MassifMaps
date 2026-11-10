/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DRAPEBAKESETTLE_H_
#define _MASSIF_DRAPEBAKESETTLE_H_

#include <cstddef>
#include <map>

#include <vt/TileId.h>

namespace massif {

    /**
     * Holds the re-bake of a tile that already shows a complete picture while its layers are still landing,
     * so they cost one bake, not one each. Never before a tile's first picture, never past MAX_HOLD_FRAMES.
     */
    class DrapeBakeSettle {
    public:
        // Frames a fingerprint must stay put before its tile re-bakes.
        static const unsigned SETTLE_FRAMES = 3;
        // The longest a tile goes without its re-bake, whatever keeps arriving.
        static const unsigned MAX_HOLD_FRAMES = 10;

        /**
         * Every frame, for every cover tile. A tile seen for the first time is never held: it has no
         * baseline to coalesce against.
         */
        void observe(const vt::TileId& tileId, std::size_t fingerprint, unsigned frame) {
            auto it = _entries.find(tileId);
            if (it == _entries.end()) {
                _entries[tileId] = Entry { fingerprint, frame, frame, false, frame };
                return;
            }
            Entry& entry = it->second;
            entry.seenFrame = frame;
            if (entry.fingerprint != fingerprint) {
                entry.fingerprint = fingerprint;
                entry.changedFrame = frame;
                if (!entry.pending) {
                    entry.pending = true;
                    entry.pendingSince = frame;
                }
            }
        }

        /**
         * Whether a re-bake due this frame should wait.
         */
        bool shouldHold(const vt::TileId& tileId, unsigned frame) const {
            auto it = _entries.find(tileId);
            if (it == _entries.end() || !it->second.pending) {
                return false;
            }
            const Entry& entry = it->second;
            return frame - entry.changedFrame < SETTLE_FRAMES && frame - entry.pendingSince < MAX_HOLD_FRAMES;
        }

        /**
         * The tile was baked: nothing is pending until its fingerprint moves again.
         */
        void baked(const vt::TileId& tileId) {
            auto it = _entries.find(tileId);
            if (it != _entries.end()) {
                it->second.pending = false;
            }
        }

        /**
         * Forgets tiles not seen for `idleFrames`, so the table follows the cover instead of growing with the pan.
         */
        void prune(unsigned frame, unsigned idleFrames) {
            for (auto it = _entries.begin(); it != _entries.end();) {
                if (frame - it->second.seenFrame > idleFrames) {
                    it = _entries.erase(it);
                } else {
                    it++;
                }
            }
        }

        std::size_t size() const {
            return _entries.size();
        }

    private:
        struct Entry {
            std::size_t fingerprint;
            unsigned changedFrame;
            unsigned pendingSince;
            bool pending;
            unsigned seenFrame;
        };

        std::map<vt::TileId, Entry> _entries;
    };

}

#endif
