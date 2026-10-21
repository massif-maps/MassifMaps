/* Tile-loading diagnostics for debug builds: "[probe]" lines in logcat, compiled only with -DMASSIF_TILE_PROBE.
 * A "sum" line carrying STALL means tiles are wanted, none is in flight, and not all are valid. */

#ifndef _MASSIF_TILEPROBE_H_
#define _MASSIF_TILEPROBE_H_

#ifdef MASSIF_TILE_PROBE

#include "utils/Log.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>

namespace massif {
    class TileLayer;

    /** "kind@ptr[filter]": which layer a line is about. Defined in TileLayer.cpp, which knows the subclasses. */
    std::string TileProbeTag(const TileLayer* layer);

    namespace TileProbe {
        struct Counters {
            std::atomic<int> stored { 0 };   // tile put in the visible cache
            std::atomic<int> staleDrop { 0 };// decoded with a transformer that changed meanwhile: dropped, NOT refetched by itself
            std::atomic<int> invalidated { 0 };// fetch invalidated by updateTiles while decoding: not stored
            std::atomic<int> canceled { 0 }; // fetch canceled before it decoded
            std::atomic<int> noData { 0 };   // data source returned nothing for the tile
            std::atomic<int> emptyMap { 0 }; // decoded to no tile map at all
        };

        struct State {
            std::mutex mutex;
            std::map<const void*, Counters> counters;
            std::map<std::string, std::pair<std::string, std::chrono::steady_clock::time_point> > last;
        };

        inline State& GetState() {
            static State state;
            return state;
        }

        inline Counters& For(const void* layer) {
            State& state = GetState();
            std::lock_guard<std::mutex> lock(state.mutex);
            return state.counters[layer];
        }

        /** Logs an event unconditionally. */
        template <typename... Args>
        void Event(const TileLayer* layer, const char* what, const char* format, const Args&... args) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), format, args...);
            Log::Infof("[probe] %s %s %s", TileProbeTag(layer).c_str(), what, buffer);
        }
        inline void Event(const TileLayer* layer, const char* what) {
            Log::Infof("[probe] %s %s", TileProbeTag(layer).c_str(), what);
        }

        /**
         * Logs `text` for (layer, slot) when it differs from the last one logged there. Changes that come within
         * 300 ms of the previous line are dropped unless `settled`, so a pan does not flood the log while the
         * state a still map ends in always gets through.
         */
        inline void Report(const TileLayer* layer, const char* slot, const std::string& text, bool settled) {
            State& state = GetState();
            std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(layer)) + slot;
            auto now = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                auto it = state.last.find(key);
                if (it != state.last.end()) {
                    if (it->second.first == text) {
                        return;
                    }
                    if (!settled && now - it->second.second < std::chrono::milliseconds(300)) {
                        return;
                    }
                }
                state.last[key] = std::make_pair(text, now);
            }
            Log::Infof("[probe] %s %s %s", TileProbeTag(layer).c_str(), slot, text.c_str());
        }
    }
}

#define MASSIF_PROBE_EVENT(layer, ...) ::massif::TileProbe::Event(layer, __VA_ARGS__)
#define MASSIF_PROBE_REPORT(layer, slot, text, settled) ::massif::TileProbe::Report(layer, slot, text, settled)
#define MASSIF_PROBE_COUNT(layer, field) (::massif::TileProbe::For(layer).field++)

#else // !MASSIF_TILE_PROBE

#define MASSIF_PROBE_EVENT(layer, ...) ((void)0)
#define MASSIF_PROBE_REPORT(layer, slot, text, settled) ((void)0)
#define MASSIF_PROBE_COUNT(layer, field) ((void)0)

#endif // MASSIF_TILE_PROBE

#endif
