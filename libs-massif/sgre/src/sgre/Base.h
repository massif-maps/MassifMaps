/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_SGRE_BASE_H_
#define _CARTO_SGRE_BASE_H_

#include <string>
#include <map>
#include <functional>

#include <boost/variant.hpp>

#include <cglib/vec.h>

#include <picojson/picojson.h>

namespace carto { namespace sgre {
    using Point = cglib::vec3<double>;

    using FeatureFilter = picojson::object;

    using FloatParameter = boost::variant<boost::blank, float, std::string>;

    struct FloatParameterEvaluator : boost::static_visitor<float> {
        FloatParameterEvaluator() = delete;
        explicit FloatParameterEvaluator(const std::map<std::string, float>& paramValues, float defaultValue) : _paramValues(paramValues), _defaultValue(defaultValue) { }

        float operator() (boost::blank) const { return _defaultValue; }
        float operator() (float value) const { return value; }
        float operator() (const std::string& paramName) const { auto it = _paramValues.find(paramName); return (it != _paramValues.end() ? it->second : _defaultValue); }

    private:
        std::map<std::string, float> _paramValues;
        float _defaultValue;
    };
} }

namespace std {
    template <>
    struct hash<picojson::value> {
        size_t operator() (const picojson::value& val) const {
            return hash<string>()(val.serialize());
        }
    };
}

#endif
