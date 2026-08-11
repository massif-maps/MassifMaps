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

    std::uint64_t hashValue(const Value& val) {
        constexpr std::uint64_t OFFSET = 1469598103934665603ULL, PRIME = 1099511628211ULL;
        auto mix = [](std::uint64_t hash, const void* data, std::size_t size) {
            const unsigned char* bytes = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < size; i++) {
                hash = (hash ^ bytes[i]) * PRIME;
            }
            return hash;
        };
        auto mixNumber = [&mix](double value) {
            double number = value == 0 ? 0 : value; // -0 and 0 compare equal
            return mix(OFFSET ^ 2, &number, sizeof(number));
        };

        if (auto boolVal = std::get_if<bool>(&val)) {
            return mixNumber(*boolVal ? 1 : 0);
        }
        if (auto longVal = std::get_if<long long>(&val)) {
            return mixNumber(static_cast<double>(*longVal));
        }
        if (auto doubleVal = std::get_if<double>(&val)) {
            return mixNumber(*doubleVal);
        }
        if (auto stringVal = std::get_if<std::string>(&val)) {
            return mix(OFFSET ^ 3, stringVal->data(), stringVal->size());
        }
        if (auto arrayVal = std::get_if<std::shared_ptr<const ValueArray>>(&val)) {
            // Containers compare by identity, so they hash by it too
            const ValueArray* ptr = arrayVal->get();
            return mix(OFFSET ^ 4, &ptr, sizeof(ptr));
        }
        if (auto objectVal = std::get_if<std::shared_ptr<const ValueObject>>(&val)) {
            const ValueObject* ptr = objectVal->get();
            return mix(OFFSET ^ 5, &ptr, sizeof(ptr));
        }
        return OFFSET ^ 1; // unset
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
