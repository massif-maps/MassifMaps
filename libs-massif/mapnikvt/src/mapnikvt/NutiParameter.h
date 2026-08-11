/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_NUTIPARAMETER_H_
#define _CARTO_MAPNIKVT_NUTIPARAMETER_H_

#include "Value.h"

#include <memory>
#include <string>
#include <map>

namespace carto::mvt {
    class NutiParameter final {
    public:
        explicit NutiParameter(std::string name, Value defaultValue, std::map<std::string, Value> enumMap, bool selects = false) : _name(std::move(name)), _defaultValue(std::move(defaultValue)), _enumMap(std::move(enumMap)), _selects(selects) { }

        const std::string& getName() const { return _name; }
        const Value& getDefaultValue() const { return _defaultValue; }
        const std::map<std::string, Value>& getEnumMap() const { return _enumMap; }

        /**
         * True when the style declared this parameter as the one that SELECTS a feature - it is
         * compared with a feature field, and setting it should repaint rather than decode the tiles
         * again. Opt-in, because it only works for a style written a particular way and nothing
         * else should pay for looking: see resolveSelectionParameter, which does not even walk the
         * rules of a style that declares none.
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
