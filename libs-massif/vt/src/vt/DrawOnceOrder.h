/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_DRAWONCEORDER_H_
#define _MASSIF_VT_DRAWONCEORDER_H_

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace massif::vt {
    enum class DrawOncePass {
        NONE, // a plain layer
        CORE, // a group's line cores, each stamping the stencil
        RIM   // the group again: antialiased rims where nothing was stamped, and everything not a line
    };

    /** Each run sharing a non-empty group is drawn twice (mapbox's two passes over one layer), top
     *  layer first: the stencil keeps the first colour a pixel gets. Other layers draw once, in order. */
    template <typename T, typename GroupOf>
    std::vector<std::pair<T, DrawOncePass>> drawOnceSchedule(const std::vector<T>& layers, GroupOf groupOf) {
        std::vector<std::pair<T, DrawOncePass>> schedule;
        schedule.reserve(layers.size());
        for (auto runStart = layers.begin(); runStart != layers.end(); ) {
            std::string group = groupOf(*runStart);
            auto runEnd = runStart + 1;
            while (!group.empty() && runEnd != layers.end() && groupOf(*runEnd) == group) {
                runEnd++;
            }
            if (group.empty()) {
                schedule.emplace_back(*runStart, DrawOncePass::NONE);
            } else {
                for (DrawOncePass pass : { DrawOncePass::CORE, DrawOncePass::RIM }) {
                    for (auto it = runEnd; it != runStart; ) {
                        schedule.emplace_back(*--it, pass);
                    }
                }
            }
            runStart = runEnd;
        }
        return schedule;
    }
}

#endif
