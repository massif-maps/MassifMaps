/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_STYLEPARAMETER_H_
#define _MASSIF_MAPNIKVT_STYLEPARAMETER_H_

#include "Value.h"

#include <memory>
#include <string>
#include <map>

namespace massif::mvt {
    class StyleParameter final {
    public:
        explicit StyleParameter(std::string name, Value defaultValue, std::map<std::string, Value> enumMap, bool selects = false) : _name(std::move(name)), _defaultValue(std::move(defaultValue)), _enumMap(std::move(enumMap)), _selects(selects) { }

        const std::string& getName() const { return _name; }
        const Value& getDefaultValue() const { return _defaultValue; }
        const std::map<std::string, Value>& getEnumMap() const { return _enumMap; }

        /**
         * True when the style declared this parameter as the one that selects a feature, so setting it
         * repaints rather than re-decodes. Opt-in - see resolveSelectionParameter.
         */
        bool selectsFeatures() const { return _selects; }

    private:
        std::string _name;
        Value _defaultValue;
        std::map<std::string, Value> _enumMap;
        bool _selects;
    };
}

#endif
