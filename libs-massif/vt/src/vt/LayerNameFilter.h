/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_LAYERNAMEFILTER_H_
#define _MASSIF_VT_LAYERNAMEFILTER_H_

#include <mutex>
#include <regex>
#include <string>
#include <unordered_map>

namespace massif::vt {

    /**
     * A full-match regex over layer and style names, remembering each answer: a tile set repeats a few
     * hundred names thousands of times and std::regex_match costs far more than the lookup.
     * See docs/internals/performance-log.md.
     */
    class LayerNameFilter {
    public:
        explicit LayerNameFilter(const std::regex& regex) : _regex(regex) {
        }

        bool matches(const std::string& name) const {
            {
                std::lock_guard<std::mutex> lock(_mutex);
                auto it = _results.find(name);
                if (it != _results.end()) {
                    return it->second;
                }
            }
            bool result = std::regex_match(name, _regex);
            std::lock_guard<std::mutex> lock(_mutex);
            _results.emplace(name, result);
            return result;
        }

    private:
        const std::regex _regex;
        mutable std::mutex _mutex;
        mutable std::unordered_map<std::string, bool> _results;
    };

}

#endif
