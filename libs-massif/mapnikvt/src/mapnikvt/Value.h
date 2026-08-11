/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_VALUE_H_
#define _CARTO_MAPNIKVT_VALUE_H_

#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace carto::mvt {
    struct ValueArray;
    struct ValueObject;

    /**
     * A style value. Beyond the scalars, a value can be an array or an object: a style parameter
     * may hold a table the style indexes into - get([nuti::poi_colors], [class]) - so an app can
     * own a colour per POI class without the style declaring one parameter per class.
     *
     * The containers are shared and immutable, so copying a Value stays cheap.
     */
    using Value = std::variant<std::monostate, bool, long long, double, std::string, std::shared_ptr<const ValueArray>, std::shared_ptr<const ValueObject>>;

    struct ValueArray final {
        std::vector<Value> elements;

        ValueArray() = default;
        explicit ValueArray(std::vector<Value> elements) : elements(std::move(elements)) { }
    };

    struct ValueObject final {
        std::map<std::string, Value> members;

        ValueObject() = default;
        explicit ValueObject(std::map<std::string, Value> members) : members(std::move(members)) { }
    };

    /**
     * The element of an array (by index) or the member of an object (by name). Returns an unset
     * value when the container is neither, or the key is not in it.
     */
    Value getValueElement(const Value& container, const Value& key);

    /**
     * The number of elements of an array or members of an object; 0 for anything else.
     */
    long long getValueSize(const Value& container);
}

#endif
