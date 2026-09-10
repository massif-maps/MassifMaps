#include "Value.h"
#include "ValueConverter.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace massif::mvt {
    namespace {
        Value convertJSON(const rapidjson::Value& json) {
            if (json.IsString()) {
                return Value(std::string(json.GetString(), json.GetStringLength()));
            }
            if (json.IsBool()) {
                return Value(json.GetBool());
            }
            if (json.IsInt64()) {
                return Value(static_cast<long long>(json.GetInt64()));
            }
            if (json.IsNumber()) {
                return Value(json.GetDouble());
            }
            if (json.IsObject()) {
                std::map<std::string, Value> members;
                for (auto it = json.MemberBegin(); it != json.MemberEnd(); it++) {
                    members[std::string(it->name.GetString(), it->name.GetStringLength())] = convertJSON(it->value);
                }
                return Value(std::make_shared<const ValueObject>(std::move(members)));
            }
            if (json.IsArray()) {
                std::vector<Value> elements;
                for (auto it = json.Begin(); it != json.End(); it++) {
                    elements.push_back(convertJSON(*it));
                }
                return Value(std::make_shared<const ValueArray>(std::move(elements)));
            }
            return Value();
        }

        rapidjson::Value convertValue(const Value& value, rapidjson::Document::AllocatorType& allocator) {
            if (auto boolVal = std::get_if<bool>(&value)) {
                return rapidjson::Value(*boolVal);
            }
            if (auto longVal = std::get_if<long long>(&value)) {
                return rapidjson::Value(static_cast<std::int64_t>(*longVal));
            }
            if (auto doubleVal = std::get_if<double>(&value)) {
                return rapidjson::Value(*doubleVal);
            }
            if (auto stringVal = std::get_if<std::string>(&value)) {
                return rapidjson::Value(stringVal->data(), static_cast<rapidjson::SizeType>(stringVal->size()), allocator);
            }
            if (auto objectVal = std::get_if<std::shared_ptr<const ValueObject>>(&value)) {
                rapidjson::Value json(rapidjson::kObjectType);
                if (*objectVal) {
                    for (auto it = (*objectVal)->members.begin(); it != (*objectVal)->members.end(); it++) {
                        rapidjson::Value name(it->first.data(), static_cast<rapidjson::SizeType>(it->first.size()), allocator);
                        json.AddMember(name, convertValue(it->second, allocator), allocator);
                    }
                }
                return json;
            }
            if (auto arrayVal = std::get_if<std::shared_ptr<const ValueArray>>(&value)) {
                rapidjson::Value json(rapidjson::kArrayType);
                if (*arrayVal) {
                    for (auto it = (*arrayVal)->elements.begin(); it != (*arrayVal)->elements.end(); it++) {
                        json.PushBack(convertValue(*it, allocator), allocator);
                    }
                }
                return json;
            }
            return rapidjson::Value();
        }
    }

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

    bool isContainerValue(const Value& value) {
        return std::get_if<std::shared_ptr<const ValueObject>>(&value) || std::get_if<std::shared_ptr<const ValueArray>>(&value);
    }

    std::string valueToJSON(const Value& value) {
        rapidjson::Document doc;
        rapidjson::Value json = convertValue(value, doc.GetAllocator());
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        json.Accept(writer);
        return std::string(buffer.GetString(), buffer.GetSize());
    }

    Value valueFromJSON(const std::string& json) {
        rapidjson::Document doc;
        if (doc.Parse(json.c_str()).HasParseError()) {
            return Value();
        }
        return convertJSON(doc);
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
