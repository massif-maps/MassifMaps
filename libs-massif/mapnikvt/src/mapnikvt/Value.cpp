#include "Value.h"
#include "ValueConverter.h"

namespace carto::mvt {
    Value getValueElement(const Value& container, const Value& key) {
        if (auto object = std::get_if<std::shared_ptr<const ValueObject>>(&container)) {
            if (*object) {
                auto it = (*object)->members.find(ValueConverter<std::string>::convert(key));
                if (it != (*object)->members.end()) {
                    return it->second;
                }
            }
            return Value();
        }
        if (auto array = std::get_if<std::shared_ptr<const ValueArray>>(&container)) {
            if (*array) {
                long long index = ValueConverter<long long>::convert(key);
                if (index >= 0 && index < static_cast<long long>((*array)->elements.size())) {
                    return (*array)->elements[static_cast<std::size_t>(index)];
                }
            }
            return Value();
        }
        return Value();
    }

    long long getValueSize(const Value& container) {
        if (auto object = std::get_if<std::shared_ptr<const ValueObject>>(&container)) {
            return *object ? static_cast<long long>((*object)->members.size()) : 0;
        }
        if (auto array = std::get_if<std::shared_ptr<const ValueArray>>(&container)) {
            return *array ? static_cast<long long>((*array)->elements.size()) : 0;
        }
        return 0;
    }
}
