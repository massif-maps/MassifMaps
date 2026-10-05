/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_STYLEPARAMETERSTORE_H_
#define _MASSIF_MAPNIKVT_STYLEPARAMETERSTORE_H_

#include "Value.h"

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace massif::mvt {
    /**
     * The current value of every style parameter. Decoded tiles capture the store, not the values, so
     * parameter-only properties change on the next frame without a re-decode - see Property::isLiveCapable.
     */
    class StyleParameterStore final {
    public:
        StyleParameterStore() : _values(std::make_shared<const std::map<std::string, Value>>()) { }
        explicit StyleParameterStore(std::map<std::string, Value> values) : _values(std::make_shared<const std::map<std::string, Value>>(std::move(values))) { }

        std::shared_ptr<const std::map<std::string, Value>> getValues() const {
            std::lock_guard<std::mutex> lock(_mutex);
            return _values;
        }

        void setValues(std::map<std::string, Value> values) {
            auto newValues = std::make_shared<const std::map<std::string, Value>>(std::move(values));
            std::lock_guard<std::mutex> lock(_mutex);
            _values = std::move(newValues);
        }

        // The parameters a change repaints instead of re-decoding. Any other one may be folded into a
        // decoded tile; unset, every parameter counts as live.
        void setLiveNames(std::shared_ptr<const std::set<std::string>> liveNames) {
            std::lock_guard<std::mutex> lock(_mutex);
            _liveNames = std::move(liveNames);
        }

        bool isLive(const std::string& name) const {
            std::lock_guard<std::mutex> lock(_mutex);
            return !_liveNames || _liveNames->count(name) > 0;
        }

        // For a parameter whose name is computed per feature
        bool anyLive() const {
            std::lock_guard<std::mutex> lock(_mutex);
            return !_liveNames || !_liveNames->empty();
        }

    private:
        mutable std::mutex _mutex;
        std::shared_ptr<const std::map<std::string, Value>> _values;
        std::shared_ptr<const std::set<std::string>> _liveNames;
    };
}

#endif
