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
#include <string>

namespace massif::mvt {
    /**
     * The current value of every style parameter, behind one indirection.
     *
     * Decoded tiles capture the store, not the values: a style property that reads nothing but
     * parameters (a colour, a width) is turned into a function that is evaluated at render time,
     * so replacing the values here changes what the next frame draws without re-decoding anything.
     * Properties that feed a filter, a text or a marker choice are still resolved at decode time
     * and still need the tiles re-read - see Property::isLiveCapable.
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

    private:
        mutable std::mutex _mutex;
        std::shared_ptr<const std::map<std::string, Value>> _values;
    };
}

#endif
