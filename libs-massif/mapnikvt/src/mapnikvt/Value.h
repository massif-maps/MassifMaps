/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_VALUE_H_
#define _MASSIF_MAPNIKVT_VALUE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace massif::mvt {
    struct ValueArray;
    struct ValueObject;

    /**
     * A style value: a scalar, or an array/object a style parameter can hold as a table (get([param::poi_colors], [class])).
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

    /** Whether the value is an array or an object, so it has no scalar reading. */
    bool isContainerValue(const Value& value);

    /**
     * A value as JSON, and back, for where a container is carried as text (Mapnik XML, the string parameter API).
     */
    std::string valueToJSON(const Value& value);
    Value valueFromJSON(const std::string& json);

    /**
     * The number of elements of an array or members of an object; 0 for anything else.
     */
    long long getValueSize(const Value& container);

    /**
     * A hash that agrees with '=': numbers hash as doubles whatever alternative holds them, and a string never
     * hashes like a number. Above 2^53 two long longs can collide, as they already compare equal as doubles.
     */
    std::uint64_t hashValue(const Value& val);
}

#endif
