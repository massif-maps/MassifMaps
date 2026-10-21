#ifndef _MASSIF_FRAMETIMESTAMPS_H_
#define _MASSIF_FRAMETIMESTAMPS_H_

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace massif {

    // A tile id packs frameNr above the zoom ids, and ~98,000 is all a 64-bit id holds.
    constexpr int FRAME_NR_RANGE = 65536;

    // Epoch seconds give minutes mod 65536, anything else a hash: a step keeps its number when the list rolls.
    inline int FrameNrFromTimestamp(const std::string& timestamp) {
        bool numeric = !timestamp.empty() && timestamp.size() <= 18 && std::all_of(timestamp.begin(), timestamp.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (numeric) {
            return static_cast<int>((std::stoull(timestamp) / 60) % FRAME_NR_RANGE);
        }
        std::uint32_t hash = 2166136261u;
        for (unsigned char c : timestamp) {
            hash = (hash ^ c) * 16777619u;
        }
        return static_cast<int>(hash % FRAME_NR_RANGE);
    }

    // One frameNr per timestamp, none shared: the later of two that collide (same minute) takes the next free number.
    inline std::vector<int> AssignFrameNrs(const std::vector<std::string>& timestamps) {
        std::vector<int> frameNrs;
        frameNrs.reserve(timestamps.size());
        for (const std::string& timestamp : timestamps) {
            int frameNr = FrameNrFromTimestamp(timestamp);
            while (std::find(frameNrs.begin(), frameNrs.end(), frameNr) != frameNrs.end()) {
                frameNr = (frameNr + 1) % FRAME_NR_RANGE;
            }
            frameNrs.push_back(frameNr);
        }
        return frameNrs;
    }

}

#endif
