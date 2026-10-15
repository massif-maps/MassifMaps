/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_ANIMATIONPROGRESS_H_
#define _MASSIF_VT_ANIMATIONPROGRESS_H_

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace massif::vt {

    /**
     * A hash of the state a frame's animation step moves: tile blends, label opacities, a spinning
     * model's clock. Two equal hashes mean the step changed nothing. See docs/internals/rendering/01-frame.md.
     */
    class AnimationState {
    public:
        void add(float value) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            mix(bits);
        }

        void add(double value) {
            std::uint64_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            mix(bits);
        }

        std::uint64_t value() const { return _hash; }

    private:
        void mix(std::uint64_t bits) {
            _hash = (_hash ^ bits) * 0x9e3779b97f4a7c15ULL;
            _hash ^= _hash >> 29;
        }

        std::uint64_t _hash = 0xcbf29ce484222325ULL;
    };

    /**
     * Tells an animation still moving from one going nowhere. A fade only ever moves forward, so a
     * frame whose state equals one of the last few has not progressed: asking for yet another frame
     * would draw the same pixels (a step that moved nothing) or flip between a few states for good.
     */
    class AnimationStall {
    public:
        static constexpr std::size_t HISTORY = 4;

        /** Records the state; true when it equals one of the last HISTORY states recorded. */
        bool stalled(std::uint64_t state) {
            bool seen = std::find(_history.begin(), _history.begin() + _count, state) != _history.begin() + _count;
            _history[_next] = state;
            _next = (_next + 1) % HISTORY;
            _count = std::min(_count + 1, HISTORY);
            return seen;
        }

        /** Forgets the history: the animation ended, so the next one starts from nothing. */
        void reset() {
            _count = 0;
            _next = 0;
        }

    private:
        std::array<std::uint64_t, HISTORY> _history = { };
        std::size_t _next = 0;
        std::size_t _count = 0;
    };

    /**
     * What asked for the frames of a map that does not go idle, summed over every tile renderer's frames
     * since the last log line (MapRenderer prints and clears it every 300 frames).
     */
    struct RefreshStats {
        std::atomic<int> frames { 0 };    // tile renderer frames
        std::atomic<int> blend { 0 };     // a tile still fading in or out
        std::atomic<int> label { 0 };     // a label still fading
        std::atomic<int> anchor { 0 };    // a label moved onto terrain or a roof
        std::atomic<int> spinning { 0 };  // a spinning model drawn
        std::atomic<int> stalled { 0 };   // asked, but the step moved nothing: not passed on
    };

    inline RefreshStats& refreshStats() {
        static RefreshStats stats;
        return stats;
    }

}

#endif
